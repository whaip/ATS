#ifndef JYDEVICETYPE_H
#define JYDEVICETYPE_H

#include <QString>
#include <QVector>
#include <QMap>

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
	QVector<double> data;
	qint64 timestampMs = 0;
};

struct JYAlignedBatch {
	QMap<JYDeviceKind, JYDataPacket> packets;
	qint64 timestampMs = 0;
};

#endif // JYDEVICETYPE_H
