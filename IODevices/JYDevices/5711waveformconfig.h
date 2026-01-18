#ifndef WAVEFORMGENERATE_H
#define WAVEFORMGENERATE_H

#include <QByteArray>
#include <QString>
#include <vector>
#include <memory>
#include <QVector>
#include <math.h>
#include <QTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QFileDialog>
#include <QDir>
#include <QMenu>
#include <QAction>

#define PI              3.1415926535898
using namespace std;

class Waveform {
public:
    virtual double generate(int sampleIndex, int SamplesRate) = 0;
};

class SineWave : public Waveform {
    double amplitude;
    double frequency;
public:
    SineWave(double amp, double freq) : amplitude(amp), frequency(freq) {}
    double generate(int sampleIndex, int SamplesRate) override {
        double phaseIncrement = 2 * M_PI * frequency / SamplesRate;
        return amplitude * sin(phaseIncrement * (sampleIndex % SamplesRate));
    }
};

class SquareWave : public Waveform {
    double amplitude;
    double frequency;
    double dutyCycle; // 占空比
public:
    SquareWave(double amp, double freq, double duty) : amplitude(amp), frequency(freq), dutyCycle(duty) {}
    double generate(int sampleIndex, int SamplesRate) override {
        double cycleIncrement = frequency / SamplesRate;
        double position = cycleIncrement * (sampleIndex % SamplesRate);
        if (position - floor(position) < dutyCycle) {
            return amplitude;
        } else {
            return -amplitude;
        }
    }
};

class TriangleWave : public Waveform {
    double amplitude;
    double frequency;
public:
    TriangleWave(double amp, double freq) : amplitude(amp), frequency(freq) {}
    double generate(int sampleIndex, int SamplesRate) override {
        double cycleIncrement = frequency / SamplesRate;
        double position = cycleIncrement * (sampleIndex % SamplesRate) ;
        double phase = position - floor(position);
        if (phase < 0.5) {
            return 4 * amplitude * phase - amplitude;
        } else {
            return -4 * amplitude * phase + 3 * amplitude;
        }
    }
};

class StepWave : public Waveform {
    double amplitude;
public:
    StepWave(double amp) : amplitude(amp) {}
    double generate(int sampleIndex, int SamplesRate) override {
        return amplitude;
    }
};

class HighLevelWave : public Waveform {
    double amplitude;
public:
    HighLevelWave(double amp) : amplitude(amp) {}
    double generate(int sampleIndex, int SamplesRate) override {
        return amplitude;
    }
};

class LowLevelWave : public Waveform {
    double amplitude;
public:
    LowLevelWave(double amp) : amplitude(amp) {}
    double generate(int sampleIndex, int SamplesRate) override {
        return -amplitude;
    }
};

class PulseWave : public Waveform {
    double amplitude;
    double frequency;
public:
    PulseWave(double amp, double freq) : amplitude(amp), frequency(freq) {}
    double generate(int sampleIndex, int SamplesRate) override {
        double cycleIncrement = frequency / SamplesRate;
        double position = cycleIncrement * (sampleIndex % SamplesRate);
        if (position - floor(position) < 0.5) {
            return amplitude;
        } else {
            return 0.0;
        }
    }
};

class RampWave : public Waveform {
    double amplitude;
    double frequency;
public:
    RampWave(double amp, double freq) : amplitude(amp), frequency(freq) {}
    double generate(int sampleIndex, int SamplesRate) override {
        if(frequency >= 1)
        {
            double cycleIncrement = frequency / SamplesRate;
            double position = cycleIncrement * (sampleIndex % SamplesRate);
            return amplitude * position;
        }else{
            double duration = 1 / frequency;
            int totalsamoles = duration * SamplesRate;
            double position = (sampleIndex % totalsamoles) / totalsamoles;
            return amplitude * position;
        }
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

inline QString PXIe5711_testtype_to_string(PXIe5711_testtype testtype) {
    switch (testtype) {
        case PXIe5711_testtype::HighLevelWave:
            return "HighLevelWave";
        case PXIe5711_testtype::LowLevelWave:
            return "LowLevelWave";
        case PXIe5711_testtype::SineWave:
            return "SineWave";
        case PXIe5711_testtype::SquareWave:
            return "SquareWave";
        case PXIe5711_testtype::StepWave:
            return "StepWave";
        case PXIe5711_testtype::TriangleWave:
            return "TriangleWave";
        case PXIe5711_testtype::PulseWave:
            return "PulseWave";
        case PXIe5711_testtype::RampWave:
            return "RampWave";
    }
    return "Unknown";
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

inline std::unique_ptr<Waveform> PXIe5711_create_waveform(PXIe5711_testtype type, double amplitude, double frequency, double dutyCycle)
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
            return std::make_unique<PulseWave>(amplitude, frequency);
        case PXIe5711_testtype::RampWave:
            return std::make_unique<RampWave>(amplitude, frequency);
    }
    return nullptr;
}

#endif // WAVEFORMGENERATE_H
