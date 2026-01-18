#include "dshowcamerautil.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <cstring>

namespace {

// Local mirror of VIDEO_STREAM_CONFIG_CAPS so we can read frame interval range
// without depending on specific Windows SDK headers.
struct VideoStreamConfigCapsLocal {
    GUID guid;
    ULONG VideoStandard;
    SIZE InputSize;
    SIZE MinCroppingSize;
    SIZE MaxCroppingSize;
    int CropGranularityX;
    int CropGranularityY;
    int CropAlignX;
    int CropAlignY;
    SIZE MinOutputSize;
    SIZE MaxOutputSize;
    int OutputGranularityX;
    int OutputGranularityY;
    int StretchTapsX;
    int StretchTapsY;
    int ShrinkTapsX;
    int ShrinkTapsY;
    LONGLONG MinFrameInterval;
    LONGLONG MaxFrameInterval;
    LONG MinBitsPerSecond;
    LONG MaxBitsPerSecond;
};

static bool isFourccSubtypeGuid(const GUID &g)
{
    static const GUID kFourccBase = {0x00000000, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    return g.Data2 == kFourccBase.Data2 && g.Data3 == kFourccBase.Data3 && memcmp(g.Data4, kFourccBase.Data4, sizeof(g.Data4)) == 0;
}

static int fourccFromGuid(const GUID &subtype)
{
    if (!isFourccSubtypeGuid(subtype)) {
        return 0;
    }
    return static_cast<int>(subtype.Data1);
}

static QString fourccToString(int fourcc)
{
    if (fourcc == 0) {
        return QStringLiteral("UNKNOWN");
    }
    const char c1 = static_cast<char>(fourcc & 0xFF);
    const char c2 = static_cast<char>((fourcc >> 8) & 0xFF);
    const char c3 = static_cast<char>((fourcc >> 16) & 0xFF);
    const char c4 = static_cast<char>((fourcc >> 24) & 0xFF);
    char buf[5] = {c1, c2, c3, c4, 0};
    return QString::fromLatin1(buf, 4);
}

static void freeMediaType(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) {
        return;
    }

    if (pmt->cbFormat != 0) {
        CoTaskMemFree((PVOID)pmt->pbFormat);
        pmt->cbFormat = 0;
        pmt->pbFormat = nullptr;
    }
    if (pmt->pUnk != nullptr) {
        pmt->pUnk->Release();
        pmt->pUnk = nullptr;
    }
    CoTaskMemFree(pmt);
}

static bool pinIsCategory(IPin *pin, const GUID &category)
{
    if (!pin) {
        return false;
    }
    IKsPropertySet *pKs = nullptr;
    HRESULT hr = pin->QueryInterface(IID_IKsPropertySet, (void **)&pKs);
    if (FAILED(hr) || !pKs) {
        return false;
    }

    GUID pinCategory = GUID_NULL;
    DWORD returned = 0;
    hr = pKs->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0, &pinCategory, sizeof(GUID), &returned);
    pKs->Release();

    return SUCCEEDED(hr) && returned == sizeof(GUID) && pinCategory == category;
}

static IPin *findCaptureOutputPin(IBaseFilter *filter)
{
    if (!filter) {
        return nullptr;
    }

    IEnumPins *enumPins = nullptr;
    HRESULT hr = filter->EnumPins(&enumPins);
    if (FAILED(hr) || !enumPins) {
        return nullptr;
    }

    IPin *pin = nullptr;
    while (enumPins->Next(1, &pin, nullptr) == S_OK) {
        PIN_DIRECTION dir;
        if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == PINDIR_OUTPUT) {
            // Prefer the actual capture pin if categories are exposed.
            if (pinIsCategory(pin, PIN_CATEGORY_CAPTURE)) {
                enumPins->Release();
                return pin; // refcount already owned by us
            }

            // Otherwise accept any output pin that supports IAMStreamConfig.
            IAMStreamConfig *sc = nullptr;
            if (SUCCEEDED(pin->QueryInterface(IID_IAMStreamConfig, (void **)&sc)) && sc) {
                sc->Release();
                enumPins->Release();
                return pin;
            }
        }
        pin->Release();
        pin = nullptr;
    }

    enumPins->Release();
    return nullptr;
}

static QList<DShowCameraUtil::StreamCapability> enumerateCapabilitiesFromStreamConfig(IAMStreamConfig *streamConfig)
{
    QList<DShowCameraUtil::StreamCapability> result;
    if (!streamConfig) {
        return result;
    }

    int count = 0;
    int size = 0;
    HRESULT hr = streamConfig->GetNumberOfCapabilities(&count, &size);
    if (FAILED(hr) || count <= 0) {
        return result;
    }

    // Video capture pins typically return VIDEO_STREAM_CONFIG_CAPS.
    QByteArray capsBuffer;
    capsBuffer.resize(size);

    QSet<QString> seen;

    for (int i = 0; i < count; ++i) {
        AM_MEDIA_TYPE *pmt = nullptr;
        hr = streamConfig->GetStreamCaps(i, &pmt, reinterpret_cast<BYTE *>(capsBuffer.data()));
        if (FAILED(hr) || !pmt) {
            continue;
        }

        int w = 0;
        int h = 0;

        // Don't depend on VIDEOINFOHEADER2 being available in all SDK/header combos.
        // Both VIDEOINFOHEADER and VIDEOINFOHEADER2 end with a BITMAPINFOHEADER named bmiHeader.
        if ((pmt->formattype == FORMAT_VideoInfo || pmt->formattype == FORMAT_VideoInfo2)
            && pmt->pbFormat && pmt->cbFormat >= sizeof(BITMAPINFOHEADER)) {
            const BYTE *base = reinterpret_cast<const BYTE *>(pmt->pbFormat);
            const auto *bmi = reinterpret_cast<const BITMAPINFOHEADER *>(base + (pmt->cbFormat - sizeof(BITMAPINFOHEADER)));
            w = static_cast<int>(bmi->biWidth);
            h = static_cast<int>(bmi->biHeight);
        }

        // Height may be negative for top-down DIBs.
        if (h < 0) {
            h = -h;
        }

        if (w > 0 && h > 0) {
            DShowCameraUtil::StreamCapability cap;
            cap.size = QSize(w, h);

            cap.fourcc = fourccFromGuid(pmt->subtype);
            cap.formatName = fourccToString(cap.fourcc);

            // FPS range from caps (100ns units per frame):
            // - MinFrameInterval -> fastest -> maxFps
            // - MaxFrameInterval -> slowest -> minFps
            if (size >= static_cast<int>(sizeof(VideoStreamConfigCapsLocal))) {
                const auto *caps = reinterpret_cast<const VideoStreamConfigCapsLocal *>(capsBuffer.constData());
                const double maxFps = (caps->MinFrameInterval > 0) ? (10000000.0 / static_cast<double>(caps->MinFrameInterval)) : 0.0;
                const double minFps = (caps->MaxFrameInterval > 0) ? (10000000.0 / static_cast<double>(caps->MaxFrameInterval)) : 0.0;
                cap.minFps = minFps;
                cap.maxFps = maxFps;
                if (cap.minFps > 0.0 && cap.maxFps > 0.0 && cap.minFps > cap.maxFps) {
                    std::swap(cap.minFps, cap.maxFps);
                }
            }

            const QString key = QStringLiteral("%1x%2_%3_%4_%5")
                                    .arg(w)
                                    .arg(h)
                                    .arg(cap.fourcc)
                                    .arg(cap.minFps, 0, 'f', 3)
                                    .arg(cap.maxFps, 0, 'f', 3);

            if (!seen.contains(key)) {
                seen.insert(key);
                result.push_back(cap);
            }
        }

        freeMediaType(pmt);
    }

    std::sort(result.begin(), result.end(), [](const DShowCameraUtil::StreamCapability &a, const DShowCameraUtil::StreamCapability &b) {
        const qint64 areaA = static_cast<qint64>(a.size.width()) * a.size.height();
        const qint64 areaB = static_cast<qint64>(b.size.width()) * b.size.height();
        if (areaA != areaB) {
            return areaA < areaB;
        }
        if (a.size.width() != b.size.width()) {
            return a.size.width() < b.size.width();
        }
        if (a.size.height() != b.size.height()) {
            return a.size.height() < b.size.height();
        }
        if (a.fourcc != b.fourcc) {
            return a.fourcc < b.fourcc;
        }
        return a.maxFps < b.maxFps;
    });

    return result;
}

} // namespace

namespace DShowCameraUtil {

QList<DeviceInfo> enumerateDevices()
{
    QList<DeviceInfo> devices;

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = (initHr == S_OK || initHr == S_FALSE);

    ICreateDevEnum *devEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **)&devEnum);
    if (FAILED(hr) || !devEnum) {
        if (shouldUninit) {
            CoUninitialize();
        }
        return devices;
    }

    IEnumMoniker *enumMoniker = nullptr;
    hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
    devEnum->Release();
    devEnum = nullptr;

    if (hr != S_OK || !enumMoniker) {
        if (shouldUninit) {
            CoUninitialize();
        }
        return devices;
    }

    IMoniker *moniker = nullptr;
    while (enumMoniker->Next(1, &moniker, nullptr) == S_OK) {
        DeviceInfo info;

        IPropertyBag *propBag = nullptr;
        hr = moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void **)&propBag);
        if (SUCCEEDED(hr) && propBag) {
            VARIANT var;
            VariantInit(&var);

            if (SUCCEEDED(propBag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR) {
                info.friendlyName = QString::fromWCharArray(var.bstrVal);
            }
            VariantClear(&var);

            VariantInit(&var);
            if (SUCCEEDED(propBag->Read(L"DevicePath", &var, nullptr)) && var.vt == VT_BSTR) {
                info.devicePath = QString::fromWCharArray(var.bstrVal);
            }
            VariantClear(&var);

            propBag->Release();
            propBag = nullptr;
        }

        if (info.friendlyName.isEmpty()) {
            info.friendlyName = QStringLiteral("Camera");
        }

        IBaseFilter *filter = nullptr;
        hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void **)&filter);
        if (SUCCEEDED(hr) && filter) {
            IPin *pin = findCaptureOutputPin(filter);
            if (pin) {
                IAMStreamConfig *streamConfig = nullptr;
                if (SUCCEEDED(pin->QueryInterface(IID_IAMStreamConfig, (void **)&streamConfig)) && streamConfig) {
                    info.capabilities = enumerateCapabilitiesFromStreamConfig(streamConfig);
                    streamConfig->Release();
                    streamConfig = nullptr;
                }
                pin->Release();
                pin = nullptr;
            }
            filter->Release();
            filter = nullptr;
        }

        devices.push_back(info);

        moniker->Release();
        moniker = nullptr;
    }

    enumMoniker->Release();
    enumMoniker = nullptr;

    if (shouldUninit) {
        CoUninitialize();
    }

    return devices;
}

} // namespace DShowCameraUtil

#else

namespace DShowCameraUtil {
QList<DeviceInfo> enumerateDevices()
{
    return {};
}
} // namespace DShowCameraUtil

#endif
