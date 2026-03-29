#ifndef WAVEFORMGENERATE_H
#define WAVEFORMGENERATE_H

#include <QMap>
#include <QString>
#include <QVector>

#include <cmath>
#include <memory>

#define M_PI              3.1415926535898

class Waveform {
public:
    virtual ~Waveform() = default;
    virtual double generate(int sampleIndex, int samplesRate) = 0;
};

class SineWave : public Waveform {
    double amplitude;
    double frequency;
public:
    SineWave(double amp, double freq) : amplitude(amp), frequency(freq) {}
    double generate(int sampleIndex, int samplesRate) override
    {
        const double phaseIncrement = 2.0 * M_PI * frequency / samplesRate;
        return amplitude * std::sin(phaseIncrement * (sampleIndex % samplesRate));
    }
};

class SquareWave : public Waveform {
    double amplitude;
    double frequency;
    double dutyCycle;
public:
    SquareWave(double amp, double freq, double duty)
        : amplitude(amp), frequency(freq), dutyCycle(duty) {}

    double generate(int sampleIndex, int samplesRate) override
    {
        const double cycleIncrement = frequency / samplesRate;
        const double position = cycleIncrement * (sampleIndex % samplesRate);
        return (position - std::floor(position) < dutyCycle) ? amplitude : -amplitude;
    }
};

class TriangleWave : public Waveform {
    double amplitude;
    double frequency;
public:
    TriangleWave(double amp, double freq) : amplitude(amp), frequency(freq) {}

    double generate(int sampleIndex, int samplesRate) override
    {
        const double cycleIncrement = frequency / samplesRate;
        const double position = cycleIncrement * (sampleIndex % samplesRate);
        const double phase = position - std::floor(position);
        return (phase < 0.5) ? (4.0 * amplitude * phase - amplitude)
                             : (-4.0 * amplitude * phase + 3.0 * amplitude);
    }
};

class StepWave : public Waveform {
    double amplitude;
public:
    explicit StepWave(double amp) : amplitude(amp) {}
    double generate(int, int) override { return amplitude; }
};

class HighLevelWave : public Waveform {
    double amplitude;
public:
    explicit HighLevelWave(double amp) : amplitude(amp) {}
    double generate(int, int) override { return amplitude; }
};

class LowLevelWave : public Waveform {
    double amplitude;
public:
    explicit LowLevelWave(double amp) : amplitude(amp) {}
    double generate(int, int) override { return -amplitude; }
};

class PulseWave : public Waveform {
    double amplitude;
    double frequency;
    bool useTimingModel;
    double vLow;
    double vHigh;
    double tDelaySec;
    double tOnSec;
    double tPeriodSec;
public:
    PulseWave(double amp, double freq)
        : amplitude(amp)
        , frequency(freq)
        , useTimingModel(false)
        , vLow(0.0)
        , vHigh(amp)
        , tDelaySec(0.0)
        , tOnSec(0.0)
        , tPeriodSec(0.0)
    {
    }

    PulseWave(double low, double high, double delaySec, double onSec, double periodSec)
        : amplitude(high)
        , frequency((periodSec > 0.0) ? (1.0 / periodSec) : 0.0)
        , useTimingModel(true)
        , vLow(low)
        , vHigh(high)
        , tDelaySec(delaySec)
        , tOnSec(onSec)
        , tPeriodSec(periodSec)
    {
    }

    double generate(int sampleIndex, int samplesRate) override
    {
        if (useTimingModel) {
            if (samplesRate <= 0 || tPeriodSec <= 0.0) {
                return vLow;
            }
            const int samplesPerPeriod = qMax(1, static_cast<int>(std::round(tPeriodSec * samplesRate)));
            const int samplesDelay = qBound(0, static_cast<int>(std::round(tDelaySec * samplesRate)), samplesPerPeriod);
            const int samplesOn = qBound(0, static_cast<int>(std::round(tOnSec * samplesRate)), samplesPerPeriod - samplesDelay);
            const int position = sampleIndex % samplesPerPeriod;
            return (position >= samplesDelay && position < samplesDelay + samplesOn) ? vHigh : vLow;
        }

        const double cycleIncrement = frequency / samplesRate;
        const double position = cycleIncrement * (sampleIndex % samplesRate);
        return (position - std::floor(position) < 0.5) ? amplitude : 0.0;
    }
};

class RampWave : public Waveform {
    double amplitude;
    double frequency;
public:
    RampWave(double amp, double freq) : amplitude(amp), frequency(freq) {}

    double generate(int sampleIndex, int samplesRate) override
    {
        if (frequency >= 1.0) {
            const double cycleIncrement = frequency / samplesRate;
            const double position = cycleIncrement * (sampleIndex % samplesRate);
            return amplitude * position;
        }

        const double duration = 1.0 / frequency;
        const int totalSamples = static_cast<int>(duration * samplesRate);
        const double position = static_cast<double>(sampleIndex % totalSamples) / totalSamples;
        return amplitude * position;
    }
};

enum class PXIe5711_testtype {
    HighLevelWave,
    LowLevelWave,
    SineWave,
    SquareWave,
    StepWave,
    TriangleWave,
    PulseWave,
    RampWave,
};

struct PXIe5711ParamSpec {
    QString key;
    QString label;
    double minValue;
    double maxValue;
    double defaultValue;
    int decimals;
    QString suffix;
};

inline QString PXIe5711_testtype_to_string(PXIe5711_testtype testtype)
{
    switch (testtype) {
    case PXIe5711_testtype::HighLevelWave: return QStringLiteral("HighLevelWave");
    case PXIe5711_testtype::LowLevelWave: return QStringLiteral("LowLevelWave");
    case PXIe5711_testtype::SineWave: return QStringLiteral("SineWave");
    case PXIe5711_testtype::SquareWave: return QStringLiteral("SquareWave");
    case PXIe5711_testtype::StepWave: return QStringLiteral("StepWave");
    case PXIe5711_testtype::TriangleWave: return QStringLiteral("TriangleWave");
    case PXIe5711_testtype::PulseWave: return QStringLiteral("PulseWave");
    case PXIe5711_testtype::RampWave: return QStringLiteral("RampWave");
    }
    return QStringLiteral("Unknown");
}

inline QVector<PXIe5711_testtype> PXIe5711_waveform_options()
{
    return {
        PXIe5711_testtype::HighLevelWave,
        PXIe5711_testtype::LowLevelWave,
        PXIe5711_testtype::SineWave,
        PXIe5711_testtype::SquareWave,
        PXIe5711_testtype::StepWave,
        PXIe5711_testtype::TriangleWave,
        PXIe5711_testtype::PulseWave,
        PXIe5711_testtype::RampWave,
    };
}

inline QVector<PXIe5711ParamSpec> PXIe5711_waveform_param_specs(PXIe5711_testtype type)
{
    switch (type) {
    case PXIe5711_testtype::HighLevelWave:
    case PXIe5711_testtype::LowLevelWave:
    case PXIe5711_testtype::StepWave:
        return {
            {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
            {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
        };
    case PXIe5711_testtype::SineWave:
    case PXIe5711_testtype::TriangleWave:
    case PXIe5711_testtype::RampWave:
        return {
            {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
            {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
            {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
        };
    case PXIe5711_testtype::SquareWave:
        return {
            {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 1.0, 3, QStringLiteral(" V")},
            {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
            {QStringLiteral("dutyCycle"), QStringLiteral("占空比"), 0.0, 1.0, 0.5, 3, QStringLiteral("")},
            {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
        };
    case PXIe5711_testtype::PulseWave:
        return {
            {QStringLiteral("amplitude"), QStringLiteral("幅值"), -10.0, 10.0, 5.0, 3, QStringLiteral(" V")},
            {QStringLiteral("frequency"), QStringLiteral("频率"), 1.0, 100000.0, 1000.0, 1, QStringLiteral(" Hz")},
            {QStringLiteral("dutyCycle"), QStringLiteral("占空比"), 0.0, 1.0, 0.5, 3, QStringLiteral("")},
            {QStringLiteral("pulseVLow"), QStringLiteral("低电平"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            {QStringLiteral("pulseVHigh"), QStringLiteral("高电平"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
            {QStringLiteral("pulseTDelay"), QStringLiteral("延时"), 0.0, 10.0, 0.0, 6, QStringLiteral(" s")},
            {QStringLiteral("pulseTOn"), QStringLiteral("高电平时长"), 0.0, 10.0, 0.0, 6, QStringLiteral(" s")},
            {QStringLiteral("pulseTPeriod"), QStringLiteral("周期"), 0.0, 10.0, 0.0, 6, QStringLiteral(" s")},
            {QStringLiteral("offset"), QStringLiteral("偏置"), -10.0, 10.0, 0.0, 3, QStringLiteral(" V")},
        };
    }
    return {};
}

inline QMap<QString, double> PXIe5711_default_param_map(PXIe5711_testtype type)
{
    QMap<QString, double> values;
    const auto specs = PXIe5711_waveform_param_specs(type);
    for (const auto &spec : specs) {
        values.insert(spec.key, spec.defaultValue);
    }
    return values;
}

inline double PXIe5711_param_value(const QMap<QString, double> &values,
                                   const QString &key,
                                   double fallback = 0.0)
{
    const auto it = values.constFind(key);
    return (it == values.cend()) ? fallback : it.value();
}

inline std::unique_ptr<Waveform> PXIe5711_create_waveform(PXIe5711_testtype type,
                                                          double amplitude,
                                                          double frequency,
                                                          double dutyCycle,
                                                          double pulseVLow = 0.0,
                                                          double pulseVHigh = 0.0,
                                                          double pulseTDelaySec = 0.0,
                                                          double pulseTOnSec = 0.0,
                                                          double pulseTPeriodSec = 0.0,
                                                          bool pulseUseTiming = false)
{
    switch (type) {
    case PXIe5711_testtype::SineWave:
        return std::make_unique<SineWave>(amplitude, frequency);
    case PXIe5711_testtype::SquareWave:
        return std::make_unique<SquareWave>(amplitude, frequency, dutyCycle);
    case PXIe5711_testtype::TriangleWave:
        return std::make_unique<TriangleWave>(amplitude, frequency);
    case PXIe5711_testtype::StepWave:
        return std::make_unique<StepWave>(amplitude);
    case PXIe5711_testtype::HighLevelWave:
        return std::make_unique<HighLevelWave>(amplitude);
    case PXIe5711_testtype::LowLevelWave:
        return std::make_unique<LowLevelWave>(amplitude);
    case PXIe5711_testtype::PulseWave:
        if (pulseUseTiming) {
            return std::make_unique<PulseWave>(pulseVLow, pulseVHigh, pulseTDelaySec, pulseTOnSec, pulseTPeriodSec);
        }
        return std::make_unique<PulseWave>(amplitude, frequency);
    case PXIe5711_testtype::RampWave:
        return std::make_unique<RampWave>(amplitude, frequency);
    }
    return nullptr;
}

#endif // WAVEFORMGENERATE_H
