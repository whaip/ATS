#ifndef WAVEFORMGENERATE_H
#define WAVEFORMGENERATE_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <cmath>
#include <functional>
#include <memory>

#ifndef M_PI
#define M_PI 3.1415926535898
#endif

class Waveform {
public:
    virtual ~Waveform() = default;
    virtual double generate(int sampleIndex, int samplesRate) = 0;
};

class SineWave : public Waveform {
public:
    SineWave(double amp, double freq)
        : m_amplitude(amp)
        , m_frequency(freq)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        const double phaseIncrement = 2.0 * M_PI * m_frequency / samplesRate;
        return m_amplitude * std::sin(phaseIncrement * (sampleIndex % samplesRate));
    }

private:
    double m_amplitude;
    double m_frequency;
};

class SquareWave : public Waveform {
public:
    SquareWave(double lowLevel, double highLevel, double freq, double duty)
        : m_lowLevel(lowLevel)
        , m_highLevel(highLevel)
        , m_frequency(freq)
        , m_dutyCycle(duty)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        const double cycleIncrement = m_frequency / samplesRate;
        const double position = cycleIncrement * (sampleIndex % samplesRate);
        return (position - std::floor(position) < m_dutyCycle) ? m_highLevel : m_lowLevel;
    }

private:
    double m_lowLevel;
    double m_highLevel;
    double m_frequency;
    double m_dutyCycle;
};

class TriangleWave : public Waveform {
public:
    TriangleWave(double amp, double freq)
        : m_amplitude(amp)
        , m_frequency(freq)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        const double cycleIncrement = m_frequency / samplesRate;
        const double position = cycleIncrement * (sampleIndex % samplesRate);
        const double phase = position - std::floor(position);
        return (phase < 0.5) ? (4.0 * m_amplitude * phase - m_amplitude)
                             : (-4.0 * m_amplitude * phase + 3.0 * m_amplitude);
    }

private:
    double m_amplitude;
    double m_frequency;
};

class StepWave : public Waveform {
public:
    explicit StepWave(double amp)
        : m_amplitude(amp)
    {
    }

    double generate(int, int) override
    {
        return m_amplitude;
    }

private:
    double m_amplitude;
};

class HighLevelWave : public Waveform {
public:
    explicit HighLevelWave(double amp)
        : m_amplitude(amp)
    {
    }

    double generate(int, int) override
    {
        return m_amplitude;
    }

private:
    double m_amplitude;
};

class LowLevelWave : public Waveform {
public:
    explicit LowLevelWave(double amp)
        : m_amplitude(amp)
    {
    }

    double generate(int, int) override
    {
        return -m_amplitude;
    }

private:
    double m_amplitude;
};

class PulseWave : public Waveform {
public:
    PulseWave(double low, double high, double delaySec, double onSec, double periodSec)
        : m_vLow(low)
        , m_vHigh(high)
        , m_tDelaySec(delaySec)
        , m_tOnSec(onSec)
        , m_tPeriodSec(periodSec)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        if (samplesRate <= 0 || m_tPeriodSec <= 0.0) {
            return m_vLow;
        }
        const int samplesPerPeriod = qMax(1, static_cast<int>(std::round(m_tPeriodSec * samplesRate)));
        const int samplesDelay = qBound(0, static_cast<int>(std::round(m_tDelaySec * samplesRate)), samplesPerPeriod);
        const int samplesOn = qBound(0, static_cast<int>(std::round(m_tOnSec * samplesRate)), samplesPerPeriod - samplesDelay);
        const int position = sampleIndex % samplesPerPeriod;
        return (position >= samplesDelay && position < samplesDelay + samplesOn) ? m_vHigh : m_vLow;
    }

private:
    double m_vLow;
    double m_vHigh;
    double m_tDelaySec;
    double m_tOnSec;
    double m_tPeriodSec;
};

class RampWave : public Waveform {
public:
    RampWave(double amp, double freq)
        : m_amplitude(amp)
        , m_frequency(freq)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        if (m_frequency >= 1.0) {
            const double cycleIncrement = m_frequency / samplesRate;
            const double position = cycleIncrement * (sampleIndex % samplesRate);
            return m_amplitude * position;
        }

        const double duration = 1.0 / m_frequency;
        const int totalSamples = static_cast<int>(duration * samplesRate);
        const double position = static_cast<double>(sampleIndex % totalSamples) / totalSamples;
        return m_amplitude * position;
    }

private:
    double m_amplitude;
    double m_frequency;
};

struct PXIe5711ParamSpec {
    QString key;
    QString label;
    double minValue = 0.0;
    double maxValue = 0.0;
    double defaultValue = 0.0;
    int decimals = 3;
    QString suffix;
};

struct PXIe5711WaveformDefinition {
    QString id;
    QString displayName;
    QVector<PXIe5711ParamSpec> params;
    std::function<std::unique_ptr<Waveform>(const QMap<QString, double> &)> factory;
};

inline double PXIe5711_param_value(const QMap<QString, double> &values,
                                   const QString &key,
                                   double fallback = 0.0)
{
    const auto it = values.constFind(key);
    return (it == values.cend()) ? fallback : it.value();
}

inline const QVector<PXIe5711WaveformDefinition> &PXIe5711_waveform_registry()
{
    static const QVector<PXIe5711WaveformDefinition> registry = {
        {
            QStringLiteral("HighLevelWave"),
            QStringLiteral("高电平"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<HighLevelWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0));
            }
        },
        {
            QStringLiteral("LowLevelWave"),
            QStringLiteral("低电平"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<LowLevelWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0));
            }
        },
        {
            QStringLiteral("SineWave"),
            QStringLiteral("正弦波"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<SineWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0),
                    PXIe5711_param_value(params, QStringLiteral("frequency"), 1000.0));
            }
        },
        {
            QStringLiteral("SquareWave"),
            QStringLiteral("方波"),
            {
                {QStringLiteral("squareVLow"), QStringLiteral("低电平"), -10.0, 10.0, -1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("squareVHigh"), QStringLiteral("高电平"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
                {QStringLiteral("dutyCycle"), QStringLiteral("占空比"), 0.0, 1.0, 0.5, 3, QStringLiteral("")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<SquareWave>(
                    PXIe5711_param_value(params, QStringLiteral("squareVLow"), -1.0),
                    PXIe5711_param_value(params, QStringLiteral("squareVHigh"), 1.0),
                    PXIe5711_param_value(params, QStringLiteral("frequency"), 1000.0),
                    PXIe5711_param_value(params, QStringLiteral("dutyCycle"), 0.5));
            }
        },
        {
            QStringLiteral("StepWave"),
            QStringLiteral("阶跃"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<StepWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0));
            }
        },
        {
            QStringLiteral("TriangleWave"),
            QStringLiteral("三角波"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<TriangleWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0),
                    PXIe5711_param_value(params, QStringLiteral("frequency"), 1000.0));
            }
        },
        {
            QStringLiteral("PulseWave"),
            QStringLiteral("脉冲波"),
            {
                {QStringLiteral("pulseVLow"), QStringLiteral("低电平"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
                {QStringLiteral("pulseVHigh"), QStringLiteral("高电平"), -10.0, 10.0, 5.0, 3, QStringLiteral(" V")},
                {QStringLiteral("pulseTDelay"), QStringLiteral("延时"), 0.0, 10.0, 0.0, 6, QStringLiteral(" s")},
                {QStringLiteral("pulseTOn"), QStringLiteral("高电平时长"), 0.0, 10.0, 0.0005, 6, QStringLiteral(" s")},
                {QStringLiteral("pulseTPeriod"), QStringLiteral("周期"), 0.0, 10.0, 0.001, 6, QStringLiteral(" s")},
            },
            [](const QMap<QString, double> &params) {
                const double pulseVLow = PXIe5711_param_value(params, QStringLiteral("pulseVLow"), 0.0);
                const double pulseVHigh = PXIe5711_param_value(params, QStringLiteral("pulseVHigh"), 5.0);
                const double pulseTDelay = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTDelay"), 0.0));
                const double pulseTOn = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTOn"), 0.0));
                const double pulseTPeriod = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTPeriod"), 0.0));
                return std::make_unique<PulseWave>(pulseVLow, pulseVHigh, pulseTDelay, pulseTOn, pulseTPeriod);
            }
        },
        {
            QStringLiteral("RampWave"),
            QStringLiteral("斜坡波"),
            {
                {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
                {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
                {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            },
            [](const QMap<QString, double> &params) {
                return std::make_unique<RampWave>(
                    PXIe5711_param_value(params, QStringLiteral("amplitude"), 1.0),
                    PXIe5711_param_value(params, QStringLiteral("frequency"), 1000.0));
            }
        },
    };
    return registry;
}

inline const PXIe5711WaveformDefinition *PXIe5711_find_waveform(const QString &waveformId)
{
    for (const auto &definition : PXIe5711_waveform_registry()) {
        if (definition.id.compare(waveformId, Qt::CaseInsensitive) == 0) {
            return &definition;
        }
    }
    return nullptr;
}

inline QString PXIe5711_default_waveform_id()
{
    return QStringLiteral("SineWave");
}

inline QString PXIe5711_resolve_waveform_id(const QString &token)
{
    for (const auto &definition : PXIe5711_waveform_registry()) {
        if (definition.id.compare(token, Qt::CaseInsensitive) == 0
            || definition.displayName.compare(token, Qt::CaseInsensitive) == 0) {
            return definition.id;
        }
    }
    return PXIe5711_default_waveform_id();
}

inline QString PXIe5711_waveform_display_name(const QString &waveformId)
{
    const auto *definition = PXIe5711_find_waveform(waveformId);
    return definition ? definition->displayName : waveformId;
}

inline QVector<QString> PXIe5711_waveform_ids()
{
    QVector<QString> ids;
    const auto &registry = PXIe5711_waveform_registry();
    ids.reserve(registry.size());
    for (const auto &definition : registry) {
        ids.push_back(definition.id);
    }
    return ids;
}

inline QVector<PXIe5711ParamSpec> PXIe5711_waveform_param_specs(const QString &waveformId)
{
    const auto *definition = PXIe5711_find_waveform(waveformId);
    return definition ? definition->params : QVector<PXIe5711ParamSpec>{};
}

inline QMap<QString, double> PXIe5711_default_param_map(const QString &waveformId)
{
    QMap<QString, double> values;
    const auto specs = PXIe5711_waveform_param_specs(waveformId);
    for (const auto &spec : specs) {
        values.insert(spec.key, spec.defaultValue);
    }
    return values;
}

inline QMap<QString, double> PXIe5711_merge_params(const QString &waveformId,
                                                   const QMap<QString, double> &values)
{
    QMap<QString, double> merged = PXIe5711_default_param_map(waveformId);
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (merged.contains(it.key())) {
            merged.insert(it.key(), it.value());
        }
    }
    return merged;
}

inline std::unique_ptr<Waveform> PXIe5711_create_waveform(const QString &waveformId,
                                                          const QMap<QString, double> &params)
{
    const auto *definition = PXIe5711_find_waveform(waveformId);
    if (!definition || !definition->factory) {
        return nullptr;
    }
    return definition->factory(PXIe5711_merge_params(waveformId, params));
}

inline QMap<QString, double> PXIe5711_make_params(std::initializer_list<std::pair<const char *, double>> items)
{
    QMap<QString, double> params;
    for (const auto &item : items) {
        params.insert(QString::fromLatin1(item.first), item.second);
    }
    return params;
}

#endif // WAVEFORMGENERATE_H
