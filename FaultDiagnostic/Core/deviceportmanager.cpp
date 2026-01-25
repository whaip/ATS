#include "deviceportmanager.h"

#include "../../IODevices/JYDevices/jydeviceconfigutils.h"

DevicePortManager::Allocation DevicePortManager::allocate(const DevicePortPlanner::Request &request)
{
    Allocation allocation;

    int next5711Channel = 0;
    int next5322Channel = 0;
    int next5323Channel = 0;

    allocation.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    allocation.cfg5711 = build5711InitConfig();
    allocation.cfg8902 = build8902InitConfig();

    if (request.needResistance) {
        ResourceMapping resistance;
        resistance.signalType = QStringLiteral("resistance");
        resistance.binding.kind = JYDeviceKind::PXIe8902;
        resistance.binding.channel = 1;
        resistance.binding.slot = allocation.cfg8902.cfg8902.slotNumber;
        resistance.binding.resourceId = QStringLiteral("PXIe8902.CH1");
        allocation.mappings.push_back(resistance);
        allocation.requests.push_back(SignalRequest{QStringLiteral("req_res"), QStringLiteral("resistance"), 1000.0, QStringLiteral("ohm")});
        allocation.cfg8902.kind = JYDeviceKind::PXIe8902;
        allocation.cfg8902.cfg8902.sampleCount = 20;
    } else {
        allocation.cfg8902.cfg8902.sampleCount = 0;
        allocation.cfg8902.cfg8902.slotNumber = -1;
    }

    if (!request.outputs5711.isEmpty()) {
        allocation.cfg5711.kind = JYDeviceKind::PXIe5711;
        allocation.cfg5711.cfg5711.enabledChannels.clear();
        allocation.cfg5711.cfg5711.waveforms.clear();

        for (int i = 0; i < request.outputs5711.size(); ++i) {
            const auto &output = request.outputs5711[i];
            const int channel = next5711Channel++;
            allocation.cfg5711.cfg5711.enabledChannels.push_back(channel);

            ResourceMapping mapping;
            mapping.signalType = (output.mode == DevicePortPlanner::OutputMode::Current)
                ? QStringLiteral("current_output")
                : QStringLiteral("voltage_output");
            mapping.binding.kind = JYDeviceKind::PXIe5711;
            mapping.binding.channel = channel;
            mapping.binding.slot = allocation.cfg5711.cfg5711.slotNumber;
            mapping.binding.resourceId = QStringLiteral("PXIe5711.CH%1").arg(channel);
            allocation.mappings.push_back(mapping);

            const QString reqId = (output.mode == DevicePortPlanner::OutputMode::Current)
                ? QStringLiteral("req_iout_%1").arg(channel)
                : QStringLiteral("req_vout_%1").arg(channel);
            const QString unit = (output.mode == DevicePortPlanner::OutputMode::Current)
                ? QStringLiteral("A")
                : QStringLiteral("V");
            allocation.requests.push_back(SignalRequest{reqId, mapping.signalType, output.amplitude, unit});

            allocation.cfg5711.cfg5711.waveforms.push_back(JY5711WaveformConfig{channel,
                                                                                PXIe5711_testtype::HighLevelWave,
                                                                                output.amplitude,
                                                                                output.frequency,
                                                                                output.duty});
        }
        allocation.cfg5711.cfg5711.channelCount = allocation.cfg5711.cfg5711.enabledChannels.size();
    }

    const int capture532xCount = qMax(request.capture5322Ports, request.capture5323Ports);
    if (capture532xCount > 0) {
        allocation.cfg532x.kind = JYDeviceKind::PXIe5322;
        allocation.cfg532x.cfg532x.channelCount = capture532xCount;
        allocation.cfg532x.cfg532x.samplesPerRead = 512;
    }

    if (request.capture5322Ports > 0) {
        for (int i = 0; i < request.capture5322Ports; ++i) {
            const int channel = next5322Channel++;
            ResourceMapping capture;
            capture.signalType = QStringLiteral("capture_5322");
            capture.binding.kind = JYDeviceKind::PXIe5322;
            capture.binding.channel = channel;
            capture.binding.slot = build532xInitConfig(JYDeviceKind::PXIe5322).cfg532x.slotNumber;
            capture.binding.resourceId = QStringLiteral("PXIe5322.CH%1").arg(channel);
            allocation.mappings.push_back(capture);
            allocation.requests.push_back(SignalRequest{QStringLiteral("req_5322_%1").arg(channel),
                                                        QStringLiteral("capture_5322"),
                                                        0.0,
                                                        QStringLiteral("")});
        }
    }

    if (request.capture5323Ports > 0) {
        for (int i = 0; i < request.capture5323Ports; ++i) {
            const int channel = next5323Channel++;
            ResourceMapping capture;
            capture.signalType = QStringLiteral("capture_5323");
            capture.binding.kind = JYDeviceKind::PXIe5323;
            capture.binding.channel = channel;
            capture.binding.slot = build532xInitConfig(JYDeviceKind::PXIe5323).cfg532x.slotNumber;
            capture.binding.resourceId = QStringLiteral("PXIe5323.CH%1").arg(channel);
            allocation.mappings.push_back(capture);
            allocation.requests.push_back(SignalRequest{QStringLiteral("req_5323_%1").arg(channel),
                                                        QStringLiteral("capture_5323"),
                                                        0.0,
                                                        QStringLiteral("")});
        }
    }

    return allocation;
}
