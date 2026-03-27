#include "jydeviceadapter.h"
#include "jydevicetype.h"
#include "5711waveformconfig.h"
#include <QDateTime>
#include "JY5320Core.h"
#include "../../logger.h"
#include "JY5710.h"
#include "JY8902.h"
#include <cmath>
#include <vector>
#include <QDebug>

namespace {
qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

class JY532xAdapter final : public JYDeviceAdapter
{
public:
    explicit JY532xAdapter(JYDeviceKind kind)
        : m_kind(kind)
    {}

    JYDeviceKind kind() const override { return m_kind; }

    bool configure(const JYDeviceConfig &config, QString *error) override
    {
        if (m_handle) {
            close(error);
        }

        m_cfg = config.cfg532x;
        Logger::log(QStringLiteral("532x configure begin: kind=%1 slot=%2 channels=%3 sampleRate=%4 samplesPerRead=%5")
                        .arg(static_cast<int>(m_kind))
                        .arg(m_cfg.slotNumber)
                        .arg(m_cfg.channelCount)
                        .arg(m_cfg.sampleRate)
                        .arg(m_cfg.samplesPerRead),
                    Logger::Level::Info);
        if (JY5320_Open(m_cfg.slotNumber, &m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5320_Open failed");
            m_handle = nullptr;
            return false;
        }

        m_channels.resize(m_cfg.channelCount);
        m_rangeLow.resize(m_cfg.channelCount, m_cfg.lowRange);
        m_rangeHigh.resize(m_cfg.channelCount, m_cfg.highRange);
        m_bandwidth.resize(m_cfg.channelCount, static_cast<JY5320_AI_BandWidth>(m_cfg.bandwidth));
        for (int i = 0; i < m_cfg.channelCount; ++i) {
            m_channels[i] = static_cast<unsigned char>(i);
        }

        if (JY5320_AI_EnableChannel(m_handle, m_cfg.channelCount, m_channels.data(), m_rangeLow.data(), m_rangeHigh.data(), m_bandwidth.data()) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_EnableChannel failed");
            close(nullptr);
            return false;
        }

        if (JY5320_AI_SetMode(m_handle, JY5320_AI_SampleMode::JY5320_AI_Continuous) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_SetMode failed");
            close(nullptr);
            return false;
        }

        if (JY5320_AI_SetStartTriggerType(m_handle, JY5320_AI_TriggerType::JY5320_AI_Soft) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_SetStartTriggerType failed");
            close(nullptr);
            return false;
        }

        double actualRate = 0.0;
        if (JY5320_AI_SetSampleRate(m_handle, m_cfg.sampleRate, &actualRate) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_SetSampleRate failed");
            close(nullptr);
            return false;
        }

        if (JY5320_SetDeviceStatusLed(m_handle, true, 10) != 0) {
            if (error) *error = QStringLiteral("JY5320_SetDeviceStatusLed failed");
            close(nullptr);
            return false;
        }

        if (JY5320_AI_Start(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_Start failed");
            close(nullptr);
            return false;
        }

        m_lastTransferredSamples = 0;
        m_hasTransferredBaseline = false;
        m_firstReadLogged = false;

        const int dataLen = m_cfg.samplesPerRead * m_cfg.channelCount;
        m_buffer.resize(static_cast<size_t>(dataLen));
        Logger::log(QStringLiteral("532x configure complete: kind=%1 slot=%2")
                        .arg(static_cast<int>(m_kind))
                        .arg(m_cfg.slotNumber),
                    Logger::Level::Info);
        return true;
    }

    bool start(QString *error) override
    {
        Q_UNUSED(error)
        Logger::log(QStringLiteral("532x start: kind=%1")
                        .arg(static_cast<int>(m_kind)),
                    Logger::Level::Info);
        return true;
    }

    bool trigger(QString *error) override
    {
        if (!m_handle) {
            if (error) *error = QStringLiteral("device not open");
            return false;
        }
        if (JY5320_AI_SendSoftTrigger(m_handle, JY5320_TriggerMode::JY5320_StartTrigger) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_SendSoftTrigger failed");
            return false;
        }
        m_firstReadLogged = false;
        Logger::log(QStringLiteral("532x trigger sent: kind=%1")
                        .arg(static_cast<int>(m_kind)),
                    Logger::Level::Info);
        return true;
    }

    bool stop(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY5320_SetDeviceStatusLed(m_handle, false, 0);
        if (JY5320_AI_Stop(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_Stop failed");
            return false;
        }
        Logger::log(QStringLiteral("532x stop complete: kind=%1")
                        .arg(static_cast<int>(m_kind)),
                    Logger::Level::Info);
        return true;
    }

    bool close(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY5320_SetDeviceStatusLed(m_handle, false, 0);
        JY5320_AI_Stop(m_handle);
        if (JY5320_Close(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5320_Close failed");
            m_handle = nullptr;
            return false;
        }
        m_handle = nullptr;
        Logger::log(QStringLiteral("532x close complete: kind=%1")
                        .arg(static_cast<int>(m_kind)),
                    Logger::Level::Info);
        return true;
    }

    bool read(JYDataPacket *out, QString *error) override
    {
        if (!out || !m_handle) {
            if (error) *error = QStringLiteral("device not ready");
            return false;
        }

        unsigned long long availableSamples = 0;
        unsigned long long transferedSamples = 0;
        bool overRun = false;
        if (JY5320_AI_CheckBufferStatus(m_handle, &availableSamples, &transferedSamples, &overRun) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_CheckBufferStatus failed");
            return false;
        }

        if (!m_hasTransferredBaseline) {
            m_lastTransferredSamples = transferedSamples;
            m_hasTransferredBaseline = true;
        }

        const unsigned long long needSamples = static_cast<unsigned long long>(m_cfg.samplesPerRead);
        // qDebug() << "Available Samples: " << availableSamples << ", Needed Samples: " << needSamples << ", transferedSamples: " << transferedSamples;
        if (availableSamples < needSamples) {
            return false;
        }

        unsigned long long deltaTransferred = 0;
        if (transferedSamples >= m_lastTransferredSamples) {
            deltaTransferred = transferedSamples - m_lastTransferredSamples;
        } else {
            m_lastTransferredSamples = transferedSamples;
        }

        unsigned int actualRead = 0;
        const unsigned int requestLen = static_cast<unsigned int>(m_cfg.samplesPerRead);
        const int timeout = m_cfg.timeoutMs;
        if (JY5320_AI_ReadData(m_handle, m_buffer.data(), requestLen, timeout, &actualRead) != 0) {
            if (error) *error = QStringLiteral("JY5320_AI_ReadData failed");
            return false;
        }
        if (actualRead == 0 && deltaTransferred > 0) {
            actualRead = static_cast<unsigned int>(qMin<unsigned long long>(deltaTransferred, requestLen));
        }
        if (actualRead == 0) {
            Logger::log(QStringLiteral("Read returned 0 samples: kind=%1 req=%2 timeout=%3 avail=%4")
                            .arg(static_cast<int>(m_kind))
                            .arg(requestLen)
                            .arg(timeout)
                            .arg(availableSamples),
                        Logger::Level::Warn);
            return false;
        }

        m_lastTransferredSamples = transferedSamples;

        out->kind = m_kind;
        out->channelCount = m_cfg.channelCount;
        out->samplesPerChannel = static_cast<int>(actualRead);
        out->sampleRateHz = m_cfg.sampleRate;
        out->startSampleIndex = m_totalSamples;
        out->data.clear();
        unsigned int total = actualRead * static_cast<unsigned int>(m_cfg.channelCount);
        if (total > static_cast<unsigned int>(m_buffer.size())) {
            Logger::log(QStringLiteral("Read size clamp: kind=%1 total=%2 buf=%3")
                            .arg(static_cast<int>(m_kind))
                            .arg(total)
                            .arg(static_cast<unsigned int>(m_buffer.size())),
                        Logger::Level::Warn);
            total = static_cast<unsigned int>(m_buffer.size());
        }
        out->data.reserve(static_cast<int>(total));
        for (unsigned int i = 0; i < total; ++i) {
            out->data.push_back(m_buffer[i]);
        }
        out->timestampMs = nowMs();
        m_totalSamples += actualRead;
        if (!m_firstReadLogged) {
            m_firstReadLogged = true;
            Logger::log(QStringLiteral("532x first read complete: kind=%1 samples=%2 channels=%3")
                            .arg(static_cast<int>(m_kind))
                            .arg(actualRead)
                            .arg(m_cfg.channelCount),
                        Logger::Level::Info);
        }
        return true;
    }

private:
    JYDeviceKind m_kind;
    JY532xConfig m_cfg;
    JY5320_DeviceHandle m_handle = nullptr;
    QVector<unsigned char> m_channels;
    QVector<double> m_rangeLow;
    QVector<double> m_rangeHigh;
    QVector<JY5320_AI_BandWidth> m_bandwidth;
    std::vector<double> m_buffer;
    unsigned long long m_lastTransferredSamples = 0;
    bool m_hasTransferredBaseline = false;
    bool m_firstReadLogged = false;
    quint64 m_totalSamples = 0;
};

class JY5711Adapter final : public JYDeviceAdapter
{
public:
    JYDeviceKind kind() const override { return JYDeviceKind::PXIe5711; }

    bool configure(const JYDeviceConfig &config, QString *error) override
    {
        if (m_handle) {
            close(nullptr);
        }

        m_cfg = config.cfg5711;
        Logger::log(QStringLiteral("5711 configure begin: slot=%1 channels=%2 waveforms=%3 sampleRate=%4")
                        .arg(m_cfg.slotNumber)
                        .arg(m_cfg.channelCount)
                        .arg(m_cfg.waveforms.size())
                        .arg(m_cfg.sampleRate),
                    Logger::Level::Info);
        if (m_cfg.channelCount <= 0) {
            m_cfg.channelCount = 1;
        }

        m_channels.resize(m_cfg.channelCount);
        m_rangeLow.resize(m_cfg.channelCount, m_cfg.lowRange);
        m_rangeHigh.resize(m_cfg.channelCount, m_cfg.highRange);

        for (int i = 0; i < m_cfg.channelCount; ++i) {
            m_channels[i] = static_cast<unsigned char>(i);
        }

        if (JY5710_Open(m_cfg.slotNumber, &m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5710_Open failed");
            m_handle = nullptr;
            return false;
        }

        if (JY5710_AO_EnableChannel(m_handle, m_cfg.channelCount, m_channels.data(), m_rangeLow.data(), m_rangeHigh.data()) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_EnableChannel failed");
            close(nullptr);
            return false;
        }

        if (JY5710_AO_SetMode(m_handle, JY5710_AO_UpdateMode::JY5710_AO_ContinuousWrapping) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_SetMode failed");
            close(nullptr);
            return false;
        }

        double actualRate = 0.0;
        if (JY5710_AO_SetUpdateRate(m_handle, m_cfg.sampleRate, &actualRate) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_SetUpdateRate failed");
            close(nullptr);
            return false;
        }

        if (JY5710_AO_SetStartTriggerType(m_handle, JY5710_AO_TriggerType::JY5710_AO_Soft) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_SetStartTriggerType failed");
            close(nullptr);
            return false;
        }

        const int samples = static_cast<int>(std::ceil(m_cfg.sampleRate));
        if (samples <= 0) {
            if (error) *error = QStringLiteral("invalid sample rate");
            return false;
        }

        m_waveBuffer.resize(static_cast<size_t>(samples * m_cfg.channelCount));
        generateWaveforms(samples);

        unsigned int actualWrite = 0;
        if (JY5710_AO_WriteData(m_handle, m_waveBuffer.data(), static_cast<unsigned int>(m_waveBuffer.size()), 1000, &actualWrite) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_WriteData failed");
            close(nullptr);
            return false;
        }

        Logger::log(QStringLiteral("5711 configure complete: slot=%1 writePoints=%2")
                        .arg(m_cfg.slotNumber)
                        .arg(actualWrite),
                    Logger::Level::Info);

        return true;
    }

    bool start(QString *error) override
    {
        if (!m_handle) {
            if (error) *error = QStringLiteral("device not open");
            return false;
        }
        if (JY5710_AO_Start(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_Start failed");
            return false;
        }
        Logger::log(QStringLiteral("5711 output start complete"), Logger::Level::Info);
        return true;
    }

    bool trigger(QString *error) override
    {
        if (!m_handle) {
            if (error) *error = QStringLiteral("device not open");
            return false;
        }
        if (JY5710_AO_SendSoftTrigger(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_SendSoftTrigger failed");
            return false;
        }
        Logger::log(QStringLiteral("5711 output trigger sent"), Logger::Level::Info);
        return true;
    }

    bool stop(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY5710_SetDeviceStatusLed(m_handle, false, 0);
        if (JY5710_AO_Stop(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5710_AO_Stop failed");
            return false;
        }
        Logger::log(QStringLiteral("5711 output stop complete"), Logger::Level::Info);
        return true;
    }

    bool close(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY5710_SetDeviceStatusLed(m_handle, false, 0);
        JY5710_AO_Stop(m_handle);
        if (JY5710_Close(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY5710_Close failed");
            m_handle = nullptr;
            return false;
        }
        m_handle = nullptr;
        Logger::log(QStringLiteral("5711 close complete"), Logger::Level::Info);
        return true;
    }

    bool read(JYDataPacket *out, QString *error) override
    {
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

private:
    void generateWaveforms(int samples)
    {
        std::vector<std::unique_ptr<Waveform>> waveforms;
        waveforms.resize(static_cast<size_t>(m_cfg.channelCount));
        for (const auto &rawCfg : m_cfg.waveforms) {
            const JY5711WaveformConfig cfg = rawCfg.normalized();
            if (cfg.channel < 0 || cfg.channel >= m_cfg.channelCount) {
                continue;
            }
            double amplitude = cfg.amplitude;
            if (cfg.type == PXIe5711_testtype::HighLevelWave || cfg.type == PXIe5711_testtype::LowLevelWave) {
                amplitude = 0.201 * amplitude - 0.01107;  //调理板卡转换
            }
            waveforms[static_cast<size_t>(cfg.channel)] =
                PXIe5711_create_waveform(cfg.type,
                                         amplitude,
                                         cfg.frequency,
                                         cfg.dutyCycle,
                                         cfg.pulseVLow,
                                         cfg.pulseVHigh,
                                         cfg.pulseTDelay,
                                         cfg.pulseTOn,
                                         cfg.pulseTPeriod,
                                         cfg.pulseUseTiming);
        }

        for (int i = 0; i < samples; ++i) {
            for (int ch = 0; ch < m_cfg.channelCount; ++ch) {
                const int idx = i * m_cfg.channelCount + ch;
                if (waveforms[static_cast<size_t>(ch)]) {
                    m_waveBuffer[idx] = waveforms[static_cast<size_t>(ch)]->generate(i, static_cast<int>(m_cfg.sampleRate));
                } else {
                    m_waveBuffer[idx] = 0.0;
                }
            }
        }
    }

    JY5711Config m_cfg;
    JY5710_DeviceHandle m_handle = nullptr;
    QVector<unsigned char> m_channels;
    QVector<double> m_rangeLow;
    QVector<double> m_rangeHigh;
    std::vector<double> m_waveBuffer;
};

class JY8902Adapter final : public JYDeviceAdapter
{
public:
    JYDeviceKind kind() const override { return JYDeviceKind::PXIe8902; }

    bool configure(const JYDeviceConfig &config, QString *error) override
    {
        if (m_handle) {
            close(nullptr);
        }

        m_cfg = config.cfg8902;
        Logger::log(QStringLiteral("8902 configure: slot=%1 sampleCount=%2 func=%3 range=%4 aperture=%5 delay=%6")
                        .arg(m_cfg.slotNumber)
                        .arg(m_cfg.sampleCount)
                        .arg(m_cfg.measurementFunction)
                        .arg(m_cfg.range)
                        .arg(m_cfg.apertureTime)
                        .arg(m_cfg.triggerDelay),
                    Logger::Level::Info);
        if (JY8902_Open(m_cfg.slotNumber, &m_handle) != 0) {
            if (error) *error = QStringLiteral("JY8902_Open failed");
            m_handle = nullptr;
            return false;
        }

        if (JY8902_DMM_SetSampleMode(m_handle, JY8902_DMM_SampleMode::JY8902_ContinuousMultiPoint) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetSampleMode failed");
            close(nullptr);
            return false;
        }

        if (JY8902_DMM_PowerLineFrequency(m_handle, JY8902_DMM_PowerFrequency::JY8902_50_Hz) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_PowerLineFrequency failed");
            close(nullptr);
            return false;
        }

        if (JY8902_DMM_SetMeasurementFunction(m_handle, static_cast<JY8902_DMM_MeasurementFunction>(m_cfg.measurementFunction)) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetMeasurementFunction failed");
            close(nullptr);
            return false;
        }

        if (m_cfg.range >= 0) {
            const int func = m_cfg.measurementFunction;
            if (func == JY8902_DMM_MeasurementFunction::JY8902_DC_Volts) {
                if (JY8902_DMM_SetDCVolt(m_handle, static_cast<JY8902_DMM_DC_VoltRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_SetDCVolt failed");
                    close(nullptr);
                    return false;
                }
            } else if (func == JY8902_DMM_MeasurementFunction::JY8902_AC_Volts) {
                if (JY8902_DMM_SetACVolt(m_handle, static_cast<JY8902_DMM_AC_VoltRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_SetACVolt failed");
                    close(nullptr);
                    return false;
                }
            } else if (func == JY8902_DMM_MeasurementFunction::JY8902_DC_Current) {
                if (JY8902_DMM_SetDCCurrent(m_handle, static_cast<JY8902_DMM_DC_CurrentRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_SetDCCurrent failed");
                    close(nullptr);
                    return false;
                }
            } else if (func == JY8902_DMM_MeasurementFunction::JY8902_AC_Current) {
                if (JY8902_DMM_SetACCurrent(m_handle, static_cast<JY8902_DMM_AC_CurrentRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_SetACCurrent failed");
                    close(nullptr);
                    return false;
                }
            } else if (func == JY8902_DMM_MeasurementFunction::JY8902_2_Wire_Resistance) {
                if (JY8902_DMM_Set2WireResistance(m_handle, static_cast<JY8902_DMM_2_Wire_ResistanceRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_Set2WireResistance failed");
                    close(nullptr);
                    return false;
                }
            } else if (func == JY8902_DMM_MeasurementFunction::JY8902_4_Wire_Resistance) {
                if (JY8902_DMM_Set4WireResistance(m_handle, static_cast<JY8902_DMM_4_Wire_ResistanceRange>(m_cfg.range)) != 0) {
                    if (error) *error = QStringLiteral("JY8902_DMM_Set4WireResistance failed");
                    close(nullptr);
                    return false;
                }
            }
        }

        if (JY8902_DMM_SetApertureUnit(m_handle, JY8902_DMM_ApetureUint::JY8902_Second) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetApertureUnit failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_SetApertureTime(m_handle, m_cfg.apertureTime) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetApertureTime failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_SetMultiSample(m_handle, static_cast<unsigned int>(m_cfg.sampleCount), JY8902_DMM_SampleTrigger::JY8902_Sample_Immediately, m_cfg.apertureTime) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetMultiSample failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_DisableCalibration(m_handle, true) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_DisableCalibration failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_SetTriggerType(m_handle, JY8902_DMM_TriggerType::JY8902_Soft) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetTriggerType failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_SetTriggerDelay(m_handle, m_cfg.triggerDelay) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SetTriggerDelay failed");
            close(nullptr);
            return false;
        }
        if (JY8902_SetDeviceStatusLed(m_handle, true, 10) != 0) {
            if (error) *error = QStringLiteral("JY8902_SetDeviceStatusLed failed");
            close(nullptr);
            return false;
        }
        if (JY8902_DMM_Start(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_Start failed");
            close(nullptr);
            return false;
        }

        m_readyAtMs = nowMs() + m_firstReadDelayMs;
        m_firstReadLogged = false;

        m_buffer.resize(static_cast<size_t>(m_cfg.sampleCount));
        Logger::log(QStringLiteral("8902 configure complete: slot=%1 sampleCount=%2")
                        .arg(m_cfg.slotNumber)
                        .arg(m_cfg.sampleCount),
                    Logger::Level::Info);
        return true;
    }

    bool start(QString *error) override
    {
        Q_UNUSED(error)
        Logger::log(QStringLiteral("8902 start"), Logger::Level::Info);
        return true;
    }

    bool trigger(QString *error) override
    {
        if (!m_handle) {
            if (error) *error = QStringLiteral("device not open");
            return false;
        }
        if (JY8902_DMM_SendSoftTrigger(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_SendSoftTrigger failed");
            return false;
        }
        m_readyAtMs = nowMs() + m_firstReadDelayMs;
        m_firstReadLogged = false;
        Logger::log(QStringLiteral("8902 trigger sent"), Logger::Level::Info);
        return true;
    }

    bool stop(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY8902_SetDeviceStatusLed(m_handle, false, 0);
        if (JY8902_DMM_Stop(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_Stop failed");
            return false;
        }
        Logger::log(QStringLiteral("8902 stop complete"), Logger::Level::Info);
        return true;
    }

    bool close(QString *error) override
    {
        if (!m_handle) {
            return true;
        }
        JY8902_SetDeviceStatusLed(m_handle, false, 0);
        JY8902_DMM_Stop(m_handle);
        if (JY8902_Close(m_handle) != 0) {
            if (error) *error = QStringLiteral("JY8902_Close failed");
            m_handle = nullptr;
            return false;
        }
        m_handle = nullptr;
        Logger::log(QStringLiteral("8902 close complete"), Logger::Level::Info);
        return true;
    }

    bool read(JYDataPacket *out, QString *error) override
    {
        if (!out || !m_handle) {
            if (error) *error = QStringLiteral("device not ready");
            return false;
        }

        const qint64 now = nowMs();
        if (now < m_readyAtMs) {
            return false;
        }
        if (m_lastReadMs > 0 && (now - m_lastReadMs) < 15) {
            return false;
        }
        m_lastReadMs = now;

        unsigned long long transferedSamplesStart = 0;
        int actualRead = 0;
        if (JY8902_DMM_ReadMultiPoint(m_handle, m_buffer.data(), m_cfg.sampleCount, m_cfg.timeoutMs, &actualRead) != 0) {
            if (error) *error = QStringLiteral("JY8902_DMM_ReadMultiPoint failed");
            return false;
        }
        m_lastTransferredSamples = transferedSamplesStart;

        out->kind = JYDeviceKind::PXIe8902;
        out->channelCount = 1;
        out->samplesPerChannel = actualRead;
        out->sampleRateHz = (m_cfg.apertureTime > 0.0) ? (1.0 / m_cfg.apertureTime) : 0.0;
        out->startSampleIndex = m_totalSamples;
        out->data.clear();
        out->data.reserve(actualRead);
        for (int i = 0; i < actualRead; ++i) {
            out->data.push_back(m_buffer[static_cast<size_t>(i)]);
        }
        out->timestampMs = nowMs();
        m_totalSamples += static_cast<quint64>(actualRead);
        if (!m_firstReadLogged && actualRead > 0) {
            m_firstReadLogged = true;
            Logger::log(QStringLiteral("8902 first read complete: samples=%1 sampleRate=%2")
                            .arg(actualRead)
                            .arg(out->sampleRateHz),
                        Logger::Level::Info);
        }
        return true;
    }

private:
    JY8902Config m_cfg;
    JY8902_DeviceHandle m_handle = nullptr;
    std::vector<double> m_buffer;
    qint64 m_lastReadMs = 0;
    qint64 m_readyAtMs = 0;
    const qint64 m_firstReadDelayMs = 500;
    unsigned long long m_lastTransferredSamples = 0;
    bool m_hasTransferredBaseline = false;
    bool m_firstReadLogged = false;
    quint64 m_totalSamples = 0;
};
}

std::unique_ptr<JYDeviceAdapter> createJY532xAdapter(JYDeviceKind kind)
{
    return std::make_unique<JY532xAdapter>(kind);
}

std::unique_ptr<JYDeviceAdapter> createJY5711Adapter()
{
    return std::make_unique<JY5711Adapter>();
}

std::unique_ptr<JYDeviceAdapter> createJY8902Adapter()
{
    return std::make_unique<JY8902Adapter>();
}
