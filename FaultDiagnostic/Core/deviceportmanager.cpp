#include "deviceportmanager.h"

#include "../../IODevices/JYDevices/jydeviceconfigutils.h"
#include "../../IODevices/portdefinitions.h"

namespace {
QString portTypeName(TPSPortType type)
{
    switch (type) {
        case TPSPortType::CurrentOutput:
            return QStringLiteral("currentOut");
        case TPSPortType::VoltageOutput:
            return QStringLiteral("voltageOut");
        case TPSPortType::CurrentInput:
            return QStringLiteral("currentIn");
        case TPSPortType::VoltageInput:
            return QStringLiteral("voltageIn");
        case TPSPortType::DmmChannel:
            return QStringLiteral("dmm");
    }
    return QStringLiteral("port");
}

QString resourcePrefix(JYDeviceKind kind)
{
    switch (kind) {
        case JYDeviceKind::PXIe5322:
            return QStringLiteral("PXIe5322");
        case JYDeviceKind::PXIe5323:
            return QStringLiteral("PXIe5323");
        case JYDeviceKind::PXIe5711:
            return QStringLiteral("PXIe5711");
        case JYDeviceKind::PXIe8902:
            return QStringLiteral("PXIe8902");
    }
    return QString();
}
}

bool DevicePortManager::allocate(const QVector<TPSPortRequest> &requests,
                                 QVector<TPSPortBinding> *bindings,
                                 QString *error)
{
    if (!bindings) {
        if (error) {
            *error = QStringLiteral("bindings is null");
        }
        return false;
    }

    bindings->clear();

        const JYDeviceConfig cfg5711 = build5711InitConfig();
        const JYDeviceConfig cfg5322 = build532xInitConfig(JYDeviceKind::PXIe5322);
        const JYDeviceConfig cfg5323 = build532xInitConfig(JYDeviceKind::PXIe5323);
        const JYDeviceConfig cfg8902 = build8902InitConfig();

        int nextCurrentOut = PortDefinitions::JY5711Ports::Current_OUTPUT_START;
        int nextVoltageOut = PortDefinitions::JY5711Ports::Voltage_OUTPUT_START;
        int nextCurrentIn = PortDefinitions::JY5323Ports::Current_INPUT_START;
        int nextVoltageIn = PortDefinitions::JY5322Ports::Voltage_INPUT_START;
        int nextDmm = PortDefinitions::JY8902Ports::DMM_CHANNEL_START;

        for (const auto &request : requests) {
            const int count = qMax(0, request.count);
            for (int i = 0; i < count; ++i) {
                TPSPortBinding binding;
                binding.type = request.type;

                QString identifier = request.identifiers.value(i);
                if (identifier.isEmpty() && !request.identifiers.isEmpty()) {
                    identifier = QStringLiteral("%1_%2").arg(request.identifiers.first()).arg(i + 1);
                }
                if (identifier.isEmpty()) {
                    identifier = QStringLiteral("%1%2").arg(portTypeName(request.type)).arg(i + 1);
                }
                binding.identifier = identifier;

                switch (request.type) {
                    case TPSPortType::CurrentOutput:
                        if (nextCurrentOut > PortDefinitions::JY5711Ports::Current_OUTPUT_END) {
                            if (error) {
                                *error = QStringLiteral("current output ports exhausted");
                            }
                            return false;
                        }
                        binding.deviceKind = JYDeviceKind::PXIe5711;
                        binding.channel = nextCurrentOut++;
                        binding.slot = cfg5711.cfg5711.slotNumber;
                        break;
                    case TPSPortType::VoltageOutput:
                        if (nextVoltageOut > PortDefinitions::JY5711Ports::Voltage_OUTPUT_END) {
                            if (error) {
                                *error = QStringLiteral("voltage output ports exhausted");
                            }
                            return false;
                        }
                        binding.deviceKind = JYDeviceKind::PXIe5711;
                        binding.channel = nextVoltageOut++;
                        binding.slot = cfg5711.cfg5711.slotNumber;
                        break;
                    case TPSPortType::CurrentInput:
                        if (nextCurrentIn > PortDefinitions::JY5323Ports::Current_INPUT_END) {
                            if (error) {
                                *error = QStringLiteral("current input ports exhausted");
                            }
                            return false;
                        }
                        binding.deviceKind = JYDeviceKind::PXIe5323;
                        binding.channel = nextCurrentIn++;
                        binding.slot = cfg5323.cfg532x.slotNumber;
                        break;
                    case TPSPortType::VoltageInput:
                        if (nextVoltageIn > PortDefinitions::JY5322Ports::Voltage_INPUT_END) {
                            if (error) {
                                *error = QStringLiteral("voltage input ports exhausted");
                            }
                            return false;
                        }
                        binding.deviceKind = JYDeviceKind::PXIe5322;
                        binding.channel = nextVoltageIn++;
                        binding.slot = cfg5322.cfg532x.slotNumber;
                        break;
                    case TPSPortType::DmmChannel:
                        if (nextDmm > PortDefinitions::JY8902Ports::DMM_CHANNEL_END) {
                            if (error) {
                                *error = QStringLiteral("dmm ports exhausted");
                            }
                            return false;
                        }
                        binding.deviceKind = JYDeviceKind::PXIe8902;
                        binding.channel = nextDmm++;
                        binding.slot = cfg8902.cfg8902.slotNumber;
                        break;
                }

                binding.resourceId = QStringLiteral("%1.CH%2")
                                         .arg(resourcePrefix(binding.deviceKind))
                                         .arg(binding.channel);
                bindings->push_back(binding);
            }
        }

        return true;
    }
