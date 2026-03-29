#ifndef JYDEVICETYPE_H
#define JYDEVICETYPE_H

#include <QString>
#include <QVector>
#include <QMap>
#include <QtGlobal>
#include <cmath>

#include "5711waveformconfig.h"

enum class JYDeviceKind {
	PXIe5322,
	PXIe5323,
	PXIe5711,
	PXIe8902
};

enum class JYDeviceState {
	Closed,
	Configured,
	Armed,
	Running,
	Faulted
};

struct JY532xConfig {
	int slotNumber = 0;
	int channelCount = 16;
	double sampleRate = 1000000.0;
	int samplesPerRead = 1024;
	int timeoutMs = 1000;
	double lowRange = -10.0;
	double highRange = 10.0;
	int bandwidth = 0;
};

struct JY5711WaveformConfig {
	int channel = 0;
	PXIe5711_testtype type = PXIe5711_testtype::HighLevelWave;
	double amplitude = 0.0;
	double frequency = 0.0;
	double dutyCycle = 0.5;
	double offset = 0.0;
	QMap<QString, double> params;

	double pulseVLow = 0.0;
	double pulseVHigh = 0.0;
	double pulseTDelay = 0.0;
	double pulseTOn = 0.0;
	double pulseTPeriod = 0.0;
	bool pulseUseTiming = false;

	void setLegacyPulse(double high, double freqHz, double duty)
	{
		type = PXIe5711_testtype::PulseWave;
		amplitude = high;
		frequency = qMax(0.0, freqHz);
		dutyCycle = qBound(0.0, duty, 1.0);
		pulseUseTiming = false;
		const JY5711WaveformConfig synced = normalized();
		pulseVLow = synced.pulseVLow;
		pulseVHigh = synced.pulseVHigh;
		pulseTDelay = synced.pulseTDelay;
		pulseTOn = synced.pulseTOn;
		pulseTPeriod = synced.pulseTPeriod;
		syncParamsFromLegacy();
	}

	void setPulseTiming(double vLow, double vHigh, double tDelaySec, double tOnSec, double tPeriodSec)
	{
		type = PXIe5711_testtype::PulseWave;
		pulseVLow = vLow;
		pulseVHigh = vHigh;
		pulseTDelay = qMax(0.0, tDelaySec);
		pulseTOn = qMax(0.0, tOnSec);
		pulseTPeriod = qMax(0.0, tPeriodSec);
		pulseUseTiming = true;
		const JY5711WaveformConfig synced = normalized();
		amplitude = synced.amplitude;
		frequency = synced.frequency;
		dutyCycle = synced.dutyCycle;
		pulseTOn = synced.pulseTOn;
		syncParamsFromLegacy();
	}

	void syncParamsFromLegacy()
	{
		if (params.isEmpty()) {
			params = PXIe5711_default_param_map(type);
		}
		params.insert(QStringLiteral("amplitude"), amplitude);
		params.insert(QStringLiteral("frequency"), frequency);
		params.insert(QStringLiteral("dutyCycle"), dutyCycle);
		params.insert(QStringLiteral("offset"), offset);
		params.insert(QStringLiteral("pulseVLow"), pulseVLow);
		params.insert(QStringLiteral("pulseVHigh"), pulseVHigh);
		params.insert(QStringLiteral("pulseTDelay"), pulseTDelay);
		params.insert(QStringLiteral("pulseTOn"), pulseTOn);
		params.insert(QStringLiteral("pulseTPeriod"), pulseTPeriod);
		params.insert(QStringLiteral("pulseUseTiming"), pulseUseTiming ? 1.0 : 0.0);
	}

	void syncLegacyFromParams()
	{
		if (params.isEmpty()) {
			syncParamsFromLegacy();
			return;
		}

		offset = PXIe5711_param_value(params, QStringLiteral("offset"), offset);
		if (type == PXIe5711_testtype::PulseWave) {
			pulseVLow = PXIe5711_param_value(params, QStringLiteral("pulseVLow"), pulseVLow);
			pulseVHigh = PXIe5711_param_value(params, QStringLiteral("pulseVHigh"), pulseVHigh);
			pulseTDelay = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTDelay"), pulseTDelay));
			pulseTOn = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTOn"), pulseTOn));
			pulseTPeriod = qMax(0.0, PXIe5711_param_value(params, QStringLiteral("pulseTPeriod"), pulseTPeriod));
			amplitude = PXIe5711_param_value(params, QStringLiteral("amplitude"), amplitude);
			frequency = PXIe5711_param_value(params, QStringLiteral("frequency"), frequency);
			dutyCycle = PXIe5711_param_value(params, QStringLiteral("dutyCycle"), dutyCycle);
			const bool hasTiming = (pulseTPeriod > 0.0)
				|| (pulseTOn > 0.0)
				|| (pulseTDelay > 0.0)
				|| (std::fabs(pulseVLow) > 1e-12)
				|| (std::fabs(pulseVHigh) > 1e-12);
			pulseUseTiming = hasTiming;
			if (pulseUseTiming) {
				if (std::fabs(pulseVHigh) <= 1e-12) {
					pulseVHigh = amplitude;
				}
				frequency = (pulseTPeriod > 0.0) ? (1.0 / pulseTPeriod) : 0.0;
				dutyCycle = (pulseTPeriod > 0.0) ? (pulseTOn / pulseTPeriod) : 0.0;
				amplitude = pulseVHigh;
			}
			return;
		}

		amplitude = PXIe5711_param_value(params, QStringLiteral("amplitude"), amplitude);
		frequency = PXIe5711_param_value(params, QStringLiteral("frequency"), frequency);
		dutyCycle = PXIe5711_param_value(params, QStringLiteral("dutyCycle"), dutyCycle);
		pulseUseTiming = false;
	}

	JY5711WaveformConfig normalized() const
	{
		JY5711WaveformConfig cfg = *this;
		cfg.syncLegacyFromParams();
		if (cfg.type != PXIe5711_testtype::PulseWave) {
			cfg.pulseUseTiming = false;
			cfg.syncParamsFromLegacy();
			return cfg;
		}

		if (!cfg.pulseUseTiming) {
			const double safeFreq = qMax(0.0, cfg.frequency);
			const double safeDuty = qBound(0.0, cfg.dutyCycle, 1.0);
			const double period = (safeFreq > 0.0) ? (1.0 / safeFreq) : 0.0;
			cfg.pulseVLow = 0.0;
			cfg.pulseVHigh = cfg.amplitude;
			cfg.pulseTDelay = 0.0;
			cfg.pulseTPeriod = period;
			cfg.pulseTOn = (period > 0.0) ? (safeDuty * period) : 0.0;
			cfg.syncParamsFromLegacy();
			return cfg;
		}

		cfg.pulseTDelay = qMax(0.0, cfg.pulseTDelay);
		cfg.pulseTOn = qMax(0.0, cfg.pulseTOn);
		cfg.pulseTPeriod = qMax(0.0, cfg.pulseTPeriod);
		if (cfg.pulseTPeriod > 0.0) {
			const double available = qMax(0.0, cfg.pulseTPeriod - cfg.pulseTDelay);
			cfg.pulseTOn = qBound(0.0, cfg.pulseTOn, available);
			cfg.frequency = 1.0 / cfg.pulseTPeriod;
			cfg.amplitude = cfg.pulseVHigh;
			cfg.dutyCycle = (cfg.pulseTPeriod > 0.0) ? (cfg.pulseTOn / cfg.pulseTPeriod) : 0.0;
		}
		cfg.syncParamsFromLegacy();
		return cfg;
	}
};

struct JY5711Config {
	int slotNumber = 0;
	int channelCount = 1;
	double sampleRate = 1000000.0;
	double lowRange = -10.0;
	double highRange = 10.0;
	QVector<int> enabledChannels;
	QVector<JY5711WaveformConfig> waveforms;
};

struct JY8902Config {
	int slotNumber = 0;
	int sampleCount = 20;
	int timeoutMs = 1000;
	int measurementFunction = 0;
	int range = -1;
	double apertureTime = 0.02;
	double triggerDelay = 0.1;
};

struct JYDeviceConfig {
	JYDeviceKind kind = JYDeviceKind::PXIe5322;
	JY532xConfig cfg532x;
	JY5711Config cfg5711;
	JY8902Config cfg8902;
};

struct JYDataPacket {
	JYDeviceKind kind = JYDeviceKind::PXIe5322;
	int channelCount = 0;
	int samplesPerChannel = 0;
	double sampleRateHz = 0.0;
	quint64 startSampleIndex = 0;
	QVector<double> data;
	qint64 timestampMs = 0;
};

struct JYAlignedBatch {
	QMap<JYDeviceKind, JYDataPacket> packets;
	qint64 timestampMs = 0;
};

#endif // JYDEVICETYPE_H
