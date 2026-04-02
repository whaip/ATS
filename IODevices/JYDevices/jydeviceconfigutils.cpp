#include "jydeviceconfigutils.h"

QString jyDeviceStateText(JYDeviceState state, const QString &message)
{
    switch (state) {
    case JYDeviceState::Configured:
    case JYDeviceState::Armed:
        return QStringLiteral("已初始化");
    case JYDeviceState::Running:
        return QStringLiteral("运行中");
    case JYDeviceState::Faulted:
        return message.isEmpty() ? QStringLiteral("初始化失败") : message;
    case JYDeviceState::Closed:
    default:
        return QStringLiteral("未初始化");
    }
}

JYDeviceConfig build532xInitConfig(JYDeviceKind kind)
{
    JYDeviceConfig config;
    config.kind = kind;
    config.cfg532x.slotNumber = (kind == JYDeviceKind::PXIe5322) ? 5 : 3;
    config.cfg532x.channelCount = (kind == JYDeviceKind::PXIe5322) ? 16 : 32;
    config.cfg532x.sampleRate = (kind == JYDeviceKind::PXIe5322) ? 1000000.0 : 200000.0;
    config.cfg532x.samplesPerRead = 1;
    config.cfg532x.timeoutMs = 1000;
    config.cfg532x.lowRange = -10.0;
    config.cfg532x.highRange = 10.0;
    config.cfg532x.bandwidth = 0;
    return config;
}

JYDeviceConfig build5711InitConfig()
{
    JYDeviceConfig config;
    config.kind = JYDeviceKind::PXIe5711;
    config.cfg5711.slotNumber = 0;
    config.cfg5711.channelCount = 1;
    config.cfg5711.sampleRate = 1000000.0;
    config.cfg5711.lowRange = -10.0;
    config.cfg5711.highRange = 10.0;
    config.cfg5711.enabledChannels = {0};
    JY5711WaveformConfig wf;
    wf.channel = 0;
    wf.waveformId = QStringLiteral("HighLevelWave");
    wf.params = PXIe5711_default_param_map(wf.waveformId);
    wf.params.insert(QStringLiteral("amplitude"), 0.0);
    wf.ensureValid();
    config.cfg5711.waveforms = {wf};
    return config;
}

JYDeviceConfig build8902InitConfig()
{
    JYDeviceConfig config;
    config.kind = JYDeviceKind::PXIe8902;
    config.cfg8902.slotNumber = 0;
    config.cfg8902.sampleCount = 1;
    config.cfg8902.timeoutMs = 1000;
    config.cfg8902.measurementFunction = 0;
    config.cfg8902.range = -1;
    config.cfg8902.apertureTime = 0.02;
    config.cfg8902.triggerDelay = 0.1;
    return config;
}
