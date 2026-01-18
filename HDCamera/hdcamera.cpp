#include "hdcamera.h"
#include "ui_hdcamera.h"
#include "camerastationclient.h"
#include "zoomablegraphicsview.h"
#include "dshowcamerautil.h"
#include "../logger.h"
#include "../ComponentsDetect/yolostationclient.h"
#include "../ComponentsDetect/yolomodel.h"
#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDateTime>
#include <QSignalBlocker>
#include <QApplication>
#include <QPainter>
#include <QPen>
#include <opencv2/core/utils/logger.hpp>
#include <QToolButton>
#include <QTimer>
#include <QShowEvent>

#include <QSet>
#include <algorithm>

namespace {
cv::Scalar classColorForBgr(int cls)
{
    const auto &model = YOLOModel::getInstance();
    const auto &colors = model->get_colors();
    if (cls >= 0 && cls < static_cast<int>(colors.size())) {
        return colors[cls];
    }
    return cv::Scalar(0, 255, 0);
}

cv::Mat qimageToBgrMat(const QImage &img)
{
    if (img.isNull()) {
        return {};
    }

    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.bits()), static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

QImage bgrMatToQImage(const cv::Mat &bgr)
{
    if (bgr.empty()) {
        return {};
    }

    if (bgr.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        QImage out(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return out.copy();
    }

    if (bgr.type() == CV_8UC1) {
        QImage out(bgr.data, bgr.cols, bgr.rows, static_cast<int>(bgr.step), QImage::Format_Grayscale8);
        return out.copy();
    }

    if (bgr.type() == CV_8UC4) {
        cv::Mat rgba;
        cv::cvtColor(bgr, rgba, cv::COLOR_BGRA2RGBA);
        QImage out(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step), QImage::Format_RGBA8888);
        return out.copy();
    }

    cv::Mat bgr8;
    bgr.convertTo(bgr8, CV_8U);
    return bgrMatToQImage(bgr8);
}

QImage renderOverlayHdcamera(const QImage &frame,
                            const QList<CompLabel> &labels,
                            const QPolygonF &pcbQuad,
                            bool drawQuad)
{
    if (frame.isNull()) {
        return frame;
    }

    cv::Mat bgr = qimageToBgrMat(frame);
    if (bgr.empty()) {
        return frame;
    }

    const int w = bgr.cols;
    const int h = bgr.rows;

    // Auto scale thickness for high-res frames.
    const int thickness = std::clamp(static_cast<int>(std::round(std::min(w, h) / 600.0)), 2, 8);
    const double baseScale = std::clamp(std::min(w, h) / 1000.0, 0.5, 2.5);
    const double fontScale = baseScale * (0.85 + 0.10 * thickness);

    if (drawQuad && pcbQuad.size() >= 4) {
        std::vector<cv::Point> poly;
        poly.reserve(static_cast<size_t>(pcbQuad.size()));
        for (const auto &p : pcbQuad) {
            poly.emplace_back(cv::Point(static_cast<int>(std::round(p.x())), static_cast<int>(std::round(p.y()))));
        }
        cv::polylines(bgr, poly, true, cv::Scalar(0, 0, 255), thickness, cv::LINE_AA);
    }

    for (const auto &l : labels) {
        const int x1 = std::clamp(static_cast<int>(std::round(l.x)), 0, w - 1);
        const int y1 = std::clamp(static_cast<int>(std::round(l.y)), 0, h - 1);
        const int x2 = std::clamp(static_cast<int>(std::round(l.x + l.w)), 0, w - 1);
        const int y2 = std::clamp(static_cast<int>(std::round(l.y + l.h)), 0, h - 1);
        if (x2 <= x1 + 1 || y2 <= y1 + 1) {
            continue;
        }

        const cv::Scalar color = classColorForBgr(l.cls);
        cv::rectangle(bgr, cv::Point(x1, y1), cv::Point(x2, y2), color, thickness, cv::LINE_AA);

        // Only label text in HDCamera.
        if (!l.label.isEmpty()) {
            const std::string s = l.label.toStdString();
            const int textY = std::max(0, y1 - 8);
            cv::putText(bgr, s, cv::Point(x1, textY), cv::FONT_HERSHEY_SIMPLEX, fontScale, color, thickness, cv::LINE_AA);
        }
    }

    return bgrMatToQImage(bgr);
}

QString loadResourceText(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString resolveHdcameraQssPath()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    return theme == QStringLiteral("light")
        ? QStringLiteral(":/styles/hdcamera_light.qss")
        : QStringLiteral(":/styles/hdcamera_dark.qss");
}

void setExclusiveButtons(QAbstractButton *a, QAbstractButton *b, QAbstractButton *c)
{
    auto *group = new QButtonGroup(a);
    group->setExclusive(true);
    if (a) {
        a->setCheckable(true);
        group->addButton(a);
    }
    if (b) {
        b->setCheckable(true);
        group->addButton(b);
    }
    if (c) {
        c->setCheckable(true);
        group->addButton(c);
    }
}

void setExclusiveButtons(QAbstractButton *a, QAbstractButton *b)
{
    auto *group = new QButtonGroup(a);
    group->setExclusive(true);
    if (a) {
        a->setCheckable(true);
        group->addButton(a);
    }
    if (b) {
        b->setCheckable(true);
        group->addButton(b);
    }
}

QString defaultSavePath(const QString &baseName, const QString &ext)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString safeBase = baseName.isEmpty() ? QStringLiteral("image") : baseName;
    return dir + QLatin1Char('/') + safeBase + QLatin1Char('_') + ts + QLatin1Char('.') + ext;
}

bool saveImageWithDialog(QWidget *parent, const QImage &img, const QString &baseName)
{
    if (img.isNull()) {
        QMessageBox::warning(parent, QStringLiteral("保存失败"), QStringLiteral("当前没有可保存的图像。"));
        return false;
    }

    const QString filter = QStringLiteral("PNG (*.png);;JPG (*.jpg *.jpeg);;BMP (*.bmp)");
    const QString suggested = defaultSavePath(baseName, QStringLiteral("png"));
    const QString filePath = QFileDialog::getSaveFileName(parent, QStringLiteral("选择保存位置"), suggested, filter);
    if (filePath.isEmpty()) {
        return false;
    }

    if (!img.save(filePath)) {
        QMessageBox::warning(parent, QStringLiteral("保存失败"), QStringLiteral("无法保存到：%1").arg(filePath));
        return false;
    }
    return true;
}
}

HDCamera::HDCamera(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HDCamera)
{
    ui->setupUi(this);

    const QString qss = loadResourceText(resolveHdcameraQssPath());
    if (!qss.isEmpty()) {
        setStyleSheet(qss);
    }

    Logger::log(QStringLiteral("HDCamera created"), Logger::Level::Info);

    m_camera = new CameraStationClient(this);

    connect(CameraStation::instance(), &CameraStation::configureFinished, this,
            [this](bool ok, int deviceIndex, int w, int h, int fps, int fourcc, const QString &detail) {
                Logger::log(QStringLiteral("Camera configure finished: dev=%1 %2x%3@%4 fmt=%5 -> %6 (%7)")
                                .arg(deviceIndex)
                                .arg(w)
                                .arg(h)
                                .arg(fps)
                                .arg(fourcc)
                                .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                                .arg(detail),
                            ok ? Logger::Level::Info : Logger::Level::Warn);
            });

    initCameraSettingsUi();

    connect(m_camera, &CameraStationClient::imageCaptured, this, [this](const ImageData &img) {
        if (!ui || !ui->previewArea) {
            return;
        }
        if (!img.image.isNull()) {
            m_lastPreviewFrame = img.image;
            // When component detection is enabled, feed frames to worker and show rendered result.
            // Otherwise, show raw preview.
            if (m_componentDetectEnabled && m_compWorker) {
                m_compWorker->submitFrame(img.image, img.timestampMs);
            } else {
                ui->previewArea->setImage(img.image);
            }
        }
    });

    if (ui->btnMenuToggle) {
        connect(ui->btnMenuToggle, &QToolButton::clicked, this, &HDCamera::toggleMenuVisible);
    }

    if (ui->btnSaveOriginalImage) {
        connect(ui->btnSaveOriginalImage, &QPushButton::clicked, this, [this]() {
            saveImageWithDialog(this, m_lastPreviewFrame, QStringLiteral("original"));
        });
    }

    if (ui->btnSaveCurrentImage) {
        connect(ui->btnSaveCurrentImage, &QPushButton::clicked, this, [this]() {
            QImage img;
            if (m_componentDetectEnabled && !m_lastDetectFrame.isNull()) {
                img = renderComponentDetectOverlay(m_lastDetectFrame, m_lastDetectLabels, m_lastDetectQuad);
            }
            if (img.isNull()) {
                img = m_lastPreviewFrame;
            }
            saveImageWithDialog(this, img, QStringLiteral("preview"));
        });
    }

    if (ui->chkPreviewEnabled) {
        connect(ui->chkPreviewEnabled, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_camera) {
                return;
            }
            if (on) {
                Logger::log(QStringLiteral("Preview enabled -> start camera"), Logger::Level::Info);
                applyCameraSettingsNow();
                m_camera->setEnabled(true);
            } else {
                Logger::log(QStringLiteral("Preview disabled -> stop camera"), Logger::Level::Info);
                m_camera->setEnabled(false);
            }
        });
    }

    if (ui->btnMoreSettings) {
        connect(ui->btnMoreSettings, &QPushButton::clicked, this, [this]() {
            setMenuPage(1);
        });
    }
    if (ui->btnMoreArrow) {
        connect(ui->btnMoreArrow, &QToolButton::clicked, this, [this]() {
            setMenuPage(1);
        });
    }
    if (ui->btnBack) {
        connect(ui->btnBack, &QToolButton::clicked, this, [this]() {
            if (ui && ui->menuStack && ui->menuStack->currentIndex() != 0) {
                setMenuPage(0);
            } else {
                setMenuVisible(false);
            }
        });
    }

    if (ui->menuStack) {
        connect(ui->menuStack, &QStackedWidget::currentChanged, this, [this](int index) {
            setMenuPage(index);
        });
    }

    // Component detection toggle
    if (ui->btnComponentDetect) {
        ui->btnComponentDetect->setCheckable(true);
        connect(ui->btnComponentDetect, &QPushButton::toggled, this, [this](bool on) {
            m_componentDetectEnabled = on;
            ensureComponentDetectWorker();
            if (m_compWorker) {
                m_compWorker->setEnabled(on);
            }
        });
    }

    // PCB extract modes:
    // - btnUsePCBExtract: use extracted image for inference, annotate on original, NO extract quad
    // - btnextractWithHomography: use extracted image, annotate on original, WITH extract quad
    // Buttons are made mutually exclusive but allow "none".
    if (ui->btnUsePCBExtract) {
        ui->btnUsePCBExtract->setCheckable(true);
        connect(ui->btnUsePCBExtract, &QPushButton::toggled, this, [this](bool on) {
            if (!ui) return;
            if (on && ui->btnextractWithHomography) {
                const QSignalBlocker b(ui->btnextractWithHomography);
                ui->btnextractWithHomography->setChecked(false);
            }
            m_usePcbExtract = on;
            m_drawExtractQuad = false;
            applyComponentDetectSettings();
        });
    }

    if (ui->btnextractWithHomography) {
        ui->btnextractWithHomography->setCheckable(true);
        connect(ui->btnextractWithHomography, &QPushButton::toggled, this, [this](bool on) {
            if (!ui) return;
            if (on && ui->btnUsePCBExtract) {
                const QSignalBlocker b(ui->btnUsePCBExtract);
                ui->btnUsePCBExtract->setChecked(false);
            }
            m_usePcbExtract = on;
            m_drawExtractQuad = on;
            applyComponentDetectSettings();
        });
    }

    setMenuVisible(true);
    setMenuPage(0);
}

HDCamera::~HDCamera()
{
    delete ui;
}

void HDCamera::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    // When switching away and back, make sure preview keeps running.
    if (ui && ui->chkPreviewEnabled && ui->chkPreviewEnabled->isChecked() && m_camera) {
        applyCameraSettingsNow();
        m_camera->setEnabled(true);
    }

    if (!ui || !ui->previewArea) {
        return;
    }

    if (m_componentDetectEnabled) {
        refreshComponentDetectPreview();
    } else if (!m_lastPreviewFrame.isNull()) {
        ui->previewArea->setImage(m_lastPreviewFrame);
    }
}

void HDCamera::ensureComponentDetectWorker()
{
    if (m_compWorker) {
        return;
    }

    m_compWorker = new YoloStationClient(QStringLiteral("HDCamera"), this);

    applyComponentDetectSettings();

    connect(m_compWorker, &YoloStationClient::resultReady, this,
            [this](const QImage &frame, const QList<CompLabel> &labels, const QPolygonF &pcbQuad, qint64, double) {
                if (!ui || !ui->previewArea) {
                    return;
                }
                if (!m_componentDetectEnabled) {
                    return;
                }
                if (!frame.isNull()) {
                    m_lastDetectFrame = frame;
                    m_lastDetectLabels = labels;
                    m_lastDetectQuad = pcbQuad;
                    refreshComponentDetectPreview();
                }
            }, Qt::QueuedConnection);
}

void HDCamera::applyComponentDetectSettings()
{
    if (!m_compWorker) {
        return;
    }

    // Inference input switch
    m_compWorker->setUsePcbExtract(m_usePcbExtract);

    refreshComponentDetectPreview();
}

void HDCamera::refreshComponentDetectPreview()
{
    if (!ui || !ui->previewArea) {
        return;
    }
    if (!m_componentDetectEnabled) {
        return;
    }
    if (m_lastDetectFrame.isNull()) {
        return;
    }
    ui->previewArea->setImage(renderComponentDetectOverlay(m_lastDetectFrame, m_lastDetectLabels, m_lastDetectQuad));
}

QImage HDCamera::renderComponentDetectOverlay(const QImage &frame,
                                             const QList<CompLabel> &labels,
                                             const QPolygonF &pcbQuad) const
{
    return renderOverlayHdcamera(frame, labels, pcbQuad, m_drawExtractQuad);
}

void HDCamera::setMenuVisible(bool visible)
{
    m_menuVisible = visible;

    if (ui && ui->menuPanel) {
        ui->menuPanel->setVisible(visible);
    }
    if (ui && ui->btnMenuToggle) {
        ui->btnMenuToggle->raise();
        ui->btnMenuToggle->setText(visible ? QStringLiteral("≡") : QStringLiteral("≡"));
    }
}

bool HDCamera::isMenuVisible() const
{
    return m_menuVisible;
}

void HDCamera::toggleMenuVisible()
{
    setMenuVisible(!isMenuVisible());
}

void HDCamera::setMenuPage(int index)
{
    if (!ui || !ui->menuStack) {
        return;
    }

    const int safeIndex = (index < 0 || index >= ui->menuStack->count()) ? 0 : index;
    if (ui->menuStack->currentIndex() != safeIndex) {
        ui->menuStack->setCurrentIndex(safeIndex);
    }

    if (ui->labelTitle) {
        ui->labelTitle->setText(safeIndex == 0 ? QStringLiteral("摄像头控制") : QStringLiteral("相机参数控制"));
    }
}

void HDCamera::initCameraSettingsUi()
{
    if (!ui) {
        return;
    }

    m_applySettingsTimer = new QTimer(this);
    m_applySettingsTimer->setSingleShot(true);
    m_applySettingsTimer->setInterval(250);
    connect(m_applySettingsTimer, &QTimer::timeout, this, [this]() {
        applyCameraSettingsNow();
    });

    if (!ui->comboCamera || !ui->comboResolution || !ui->comboFps || !ui->comboPixelFormat) {
        return;
    }

    refreshCameraList();
    refreshResolutionListForCurrentCamera();
    refreshPixelFormatListForCurrentSelection();
    refreshFpsListForCurrentSelection();

    {
        QSignalBlocker b3(ui->comboFps);
        ui->comboFps->clear();
        const QList<int> fpsList = {5, 10, 15, 20, 30, 60};
        for (int f : fpsList) {
            ui->comboFps->addItem(QStringLiteral("%1 fps").arg(f), f);
        }
        // Default to 30 if present
        for (int i = 0; i < ui->comboFps->count(); ++i) {
            if (ui->comboFps->itemData(i).toInt() == 30) {
                ui->comboFps->setCurrentIndex(i);
                break;
            }
        }
    }

    {
        QSignalBlocker b4(ui->comboPixelFormat);
        ui->comboPixelFormat->clear();
        // Filled by refreshPixelFormatListForCurrentSelection()
    }

    connect(ui->comboCamera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshResolutionListForCurrentCamera();
        refreshPixelFormatListForCurrentSelection();
        refreshFpsListForCurrentSelection();
        scheduleApplyCameraSettings();
    });
    connect(ui->comboResolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshPixelFormatListForCurrentSelection();
        refreshFpsListForCurrentSelection();
        scheduleApplyCameraSettings();
    });
    connect(ui->comboFps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        scheduleApplyCameraSettings();
    });
    connect(ui->comboPixelFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshFpsListForCurrentSelection();
        scheduleApplyCameraSettings();
    });
}

void HDCamera::refreshCameraList()
{
    if (!ui || !ui->comboCamera) {
        return;
    }

    m_capabilitiesByDevice.clear();

    QSignalBlocker b1(ui->comboCamera);
    ui->comboCamera->clear();

#ifdef _WIN32
    const auto devices = DShowCameraUtil::enumerateDevices();
    for (int i = 0; i < devices.size(); ++i) {
        const auto &dev = devices.at(i);
        const QString name = dev.friendlyName.isEmpty() ? QStringLiteral("摄像头 %1").arg(i)
                                                        : QStringLiteral("%1").arg(dev.friendlyName);
        ui->comboCamera->addItem(name, i);
        m_capabilitiesByDevice.push_back(dev.capabilities);
    }
#endif

    if (ui->comboCamera->count() == 0) {
        ui->comboCamera->addItem(QStringLiteral("摄像头 0"), 0);
        m_capabilitiesByDevice.push_back({});
    }

    ui->comboCamera->setCurrentIndex(0);
}

void HDCamera::refreshResolutionListForCurrentCamera()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution) {
        return;
    }

    QSignalBlocker b2(ui->comboResolution);
    ui->comboResolution->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    QList<QSize> resolutions;
    if (devIdx >= 0 && devIdx < m_capabilitiesByDevice.size()) {
        QSet<quint64> seen;
        const auto &caps = m_capabilitiesByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (!c.size.isValid()) {
                continue;
            }
            const quint64 key = (static_cast<quint64>(static_cast<quint32>(c.size.width())) << 32)
                                | static_cast<quint32>(c.size.height());
            if (!seen.contains(key)) {
                seen.insert(key);
                resolutions.push_back(c.size);
            }
        }

        std::sort(resolutions.begin(), resolutions.end(), [](const QSize &a, const QSize &b) {
            const qint64 areaA = static_cast<qint64>(a.width()) * a.height();
            const qint64 areaB = static_cast<qint64>(b.width()) * b.height();
            if (areaA != areaB) {
                return areaA < areaB;
            }
            if (a.width() != b.width()) {
                return a.width() < b.width();
            }
            return a.height() < b.height();
        });
    }

    // Fallback: keep a few common presets so UI is usable even if driver doesn't expose IAMStreamConfig.
    if (resolutions.isEmpty()) {
        resolutions = {
            QSize(640, 480),
            QSize(800, 600),
            QSize(1024, 768),
            QSize(1280, 720),
            QSize(1600, 1200),
            QSize(1920, 1080),
            QSize(2048, 1536),
            QSize(2560, 1440),
            QSize(3840, 2160),
            QSize(4096, 2160),
            QSize(7680, 4320),
        };
    }

    for (const QSize &s : resolutions) {
        ui->comboResolution->addItem(QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()), s);
    }

    // Default to 1920x1080 if present; else pick the largest.
    int defaultIndex = -1;
    for (int i = 0; i < ui->comboResolution->count(); ++i) {
        if (ui->comboResolution->itemData(i).toSize() == QSize(1920, 1080)) {
            defaultIndex = i;
            break;
        }
    }
    if (defaultIndex < 0 && ui->comboResolution->count() > 0) {
        defaultIndex = ui->comboResolution->count() - 1;
    }
    if (defaultIndex >= 0) {
        ui->comboResolution->setCurrentIndex(defaultIndex);
    }
}

void HDCamera::refreshPixelFormatListForCurrentSelection()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat) {
        return;
    }

    QSignalBlocker b(ui->comboPixelFormat);
    ui->comboPixelFormat->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    const QSize size = ui->comboResolution->currentData().toSize();

    struct FormatEntry {
        int fourcc = 0;
        QString name;
        double maxFps = 0.0;
    };

    QSet<int> seenFourcc;
    QList<FormatEntry> formats;

    if (devIdx >= 0 && devIdx < m_capabilitiesByDevice.size() && size.isValid()) {
        const auto &caps = m_capabilitiesByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (c.size != size) {
                continue;
            }
            const int f = c.fourcc;
            if (!seenFourcc.contains(f)) {
                seenFourcc.insert(f);
                FormatEntry e;
                e.fourcc = f;
                e.name = c.formatName.isEmpty() ? QStringLiteral("UNKNOWN") : c.formatName;
                e.maxFps = (c.maxFps > 0.0) ? c.maxFps : 0.0;
                formats.push_back(e);
            } else {
                for (auto &e : formats) {
                    if (e.fourcc == f) {
                        if (c.maxFps > e.maxFps) {
                            e.maxFps = c.maxFps;
                        }
                        break;
                    }
                }
            }
        }
    }

    // If we couldn't get formats, fall back to a small safe set.
    if (formats.isEmpty()) {
        ui->comboPixelFormat->addItem(QStringLiteral("默认"), 0);
        ui->comboPixelFormat->addItem(QStringLiteral("MJPG"), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        ui->comboPixelFormat->addItem(QStringLiteral("YUY2"), cv::VideoWriter::fourcc('Y', 'U', 'Y', '2'));
        ui->comboPixelFormat->setCurrentIndex(0);
        return;
    }

    const int mjpg = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    const bool isHighRes = size.width() >= 4000 || size.height() >= 3000;

    // Sort by max FPS (desc). For high-res, prefer MJPG first.
    std::sort(formats.begin(), formats.end(), [&](const FormatEntry &a, const FormatEntry &b) {
        if (isHighRes) {
            const bool aIsMjpg = (a.fourcc == mjpg);
            const bool bIsMjpg = (b.fourcc == mjpg);
            if (aIsMjpg != bIsMjpg) {
                return aIsMjpg;
            }
        }
        if (a.maxFps != b.maxFps) {
            return a.maxFps > b.maxFps;
        }
        return a.name < b.name;
    });

    for (const auto &e : formats) {
        ui->comboPixelFormat->addItem(e.name, e.fourcc);
    }

    ui->comboPixelFormat->setCurrentIndex(0);
}

void HDCamera::refreshFpsListForCurrentSelection()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat || !ui->comboFps) {
        return;
    }

    QSignalBlocker b(ui->comboFps);
    const int prev = ui->comboFps->currentData().toInt();
    ui->comboFps->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    const QSize size = ui->comboResolution->currentData().toSize();
    const int fourcc = ui->comboPixelFormat->currentData().toInt();

    double minFps = 0.0;
    double maxFps = 0.0;

    if (devIdx >= 0 && devIdx < m_capabilitiesByDevice.size() && size.isValid()) {
        const auto &caps = m_capabilitiesByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (c.size != size) {
                continue;
            }
            if (c.fourcc != fourcc) {
                continue;
            }
            if (c.maxFps > 0.0) {
                maxFps = (maxFps <= 0.0) ? c.maxFps : std::max(maxFps, c.maxFps);
            }
            if (c.minFps > 0.0) {
                minFps = (minFps <= 0.0) ? c.minFps : std::min(minFps, c.minFps);
            }
        }
    }

    const QList<int> common = {5, 10, 15, 20, 30, 60};
    QList<int> fpsList;

    auto within = [&](int f) -> bool {
        if (maxFps > 0.0 && static_cast<double>(f) > (maxFps + 0.001)) {
            return false;
        }
        if (minFps > 0.0 && static_cast<double>(f) < (minFps - 0.001)) {
            return false;
        }
        return true;
    };

    for (int f : common) {
        if (within(f)) {
            fpsList.push_back(f);
        }
    }

    // If enumeration gave a maxFps but common list filtered everything, fall back to an integer near max.
    if (fpsList.isEmpty() && maxFps > 0.0) {
        const int f = std::max(1, static_cast<int>(std::floor(maxFps + 0.001)));
        fpsList.push_back(f);
    }
    if (fpsList.isEmpty()) {
        fpsList = common;
    }

    for (int f : fpsList) {
        ui->comboFps->addItem(QStringLiteral("%1 fps").arg(f), f);
    }

    // Restore previous if still present, else choose max available.
    int idx = -1;
    for (int i = 0; i < ui->comboFps->count(); ++i) {
        if (ui->comboFps->itemData(i).toInt() == prev) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && ui->comboFps->count() > 0) {
        idx = ui->comboFps->count() - 1;
    }
    if (idx >= 0) {
        ui->comboFps->setCurrentIndex(idx);
    }
}

void HDCamera::scheduleApplyCameraSettings()
{
    if (!m_applySettingsTimer) {
        return;
    }
    m_applySettingsTimer->start();
}

void HDCamera::applyCameraSettingsNow()
{
    if (!ui || !m_camera) {
        return;
    }
    if (!ui->comboCamera || !ui->comboResolution || !ui->comboFps || !ui->comboPixelFormat) {
        return;
    }

    CameraStation::Config cfg;
    cfg.deviceIndex = ui->comboCamera->currentData().toInt();
    const QSize res = ui->comboResolution->currentData().toSize();
    if (res.isValid()) {
        cfg.width = res.width();
        cfg.height = res.height();
    }
    cfg.fps = ui->comboFps->currentData().toInt();
    cfg.fourcc = ui->comboPixelFormat->currentData().toInt();

    // Log the enumerated fps range for the current selection (if available).
    double minFps = 0.0;
    double maxFps = 0.0;
    const int devIdx = ui->comboCamera->currentData().toInt();
    if (devIdx >= 0 && devIdx < m_capabilitiesByDevice.size() && res.isValid()) {
        const auto &caps = m_capabilitiesByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (c.size != res || c.fourcc != cfg.fourcc) {
                continue;
            }
            if (c.maxFps > 0.0) {
                maxFps = (maxFps <= 0.0) ? c.maxFps : std::max(maxFps, c.maxFps);
            }
            if (c.minFps > 0.0) {
                minFps = (minFps <= 0.0) ? c.minFps : std::min(minFps, c.minFps);
            }
        }
    }

    m_camera->requestConfigure(cfg);
    Logger::log(
        QStringLiteral("Apply camera settings (queued): dev=%1 %2x%3@%4 fmt=%5 enumFps=[%6,%7]")
            .arg(cfg.deviceIndex)
            .arg(cfg.width)
            .arg(cfg.height)
            .arg(cfg.fps)
            .arg(cfg.fourcc)
            .arg(minFps, 0, 'f', 2)
            .arg(maxFps, 0, 'f', 2),
        Logger::Level::Info);
}
