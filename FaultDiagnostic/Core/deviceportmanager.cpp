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

DevicePortManager::AllocationState DevicePortManager::defaultState()
{
    AllocationState state;
    state.cfg5711 = build5711InitConfig();
    state.cfg5322 = build532xInitConfig(JYDeviceKind::PXIe5322);
    state.cfg5323 = build532xInitConfig(JYDeviceKind::PXIe5323);
    state.cfg8902 = build8902InitConfig();

    state.nextCurrentOut = PortDefinitions::JY5711Ports::Current_OUTPUT_START;
    state.nextVoltageOut = PortDefinitions::JY5711Ports::Voltage_OUTPUT_START;
    state.nextCurrentIn = PortDefinitions::JY5323Ports::Current_INPUT_START;
    state.nextVoltageIn = PortDefinitions::JY5322Ports::Voltage_INPUT_START;
    state.nextDmm = PortDefinitions::JY8902Ports::DMM_CHANNEL_START;
    return state;
}

bool DevicePortManager::allocate(const QVector<TPSPortRequest> &requests,
                                 QVector<TPSPortBinding> *bindings,
                                 QString *error)
{
    AllocationState state = defaultState();
    return allocate(requests, &state, bindings, error);
}

bool DevicePortManager::allocate(const QVector<TPSPortRequest> &requests,
                                 AllocationState *state,
                                 QVector<TPSPortBinding> *bindings,
                                 QString *error)
{
    if (!bindings) {
        if (error) {
            *error = QStringLiteral("bindings is null");
        }
        return false;
    }

    if (!state) {
        if (error) {
            *error = QStringLiteral("allocation state is null");
        }
        return false;
    }

    bindings->clear();

    QVector<TPSPortRequest> ordered;
    ordered.reserve(requests.size());
    for (const auto &request : requests) {
        if (request.type == TPSPortType::CurrentInput || request.type == TPSPortType::VoltageInput) {
            ordered.push_back(request);
        }
    }
    for (const auto &request : requests) {
        if (request.type != TPSPortType::CurrentInput
            && request.type != TPSPortType::VoltageInput
            && request.type != TPSPortType::DmmChannel) {
            ordered.push_back(request);
        }
    }
    for (const auto &request : requests) {
        if (request.type == TPSPortType::DmmChannel) {
            ordered.push_back(request);
        }
    }

    for (const auto &request : ordered) {
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
                    if (state->nextCurrentOut > PortDefinitions::JY5711Ports::Current_OUTPUT_END) {
                        if (error) {
                            *error = QStringLiteral("current output ports exhausted");
                        }
                        return false;
                    }
                    binding.deviceKind = JYDeviceKind::PXIe5711;
                    binding.channel = state->nextCurrentOut++;
                    break;
                case TPSPortType::VoltageOutput:
                    if (state->nextVoltageOut > PortDefinitions::JY5711Ports::Voltage_OUTPUT_END) {
                        if (error) {
                            *error = QStringLiteral("voltage output ports exhausted");
                        }
                        return false;
                    }
                    binding.deviceKind = JYDeviceKind::PXIe5711;
                    binding.channel = state->nextVoltageOut++;
                    break;
                case TPSPortType::CurrentInput:
                    if (state->nextCurrentIn > PortDefinitions::JY5323Ports::Current_INPUT_END) {
                        if (error) {
                            *error = QStringLiteral("current input ports exhausted");
                        }
                        return false;
                    }
                    binding.deviceKind = JYDeviceKind::PXIe5323;
                    binding.channel = state->nextCurrentIn++;
                    break;
                case TPSPortType::VoltageInput:
                    if (state->nextVoltageIn > PortDefinitions::JY5322Ports::Voltage_INPUT_END) {
                        if (error) {
                            *error = QStringLiteral("voltage input ports exhausted");
                        }
                        return false;
                    }
                    binding.deviceKind = JYDeviceKind::PXIe5322;
                    binding.channel = state->nextVoltageIn++;
                    break;
                case TPSPortType::DmmChannel:
                    if (state->nextDmm > PortDefinitions::JY8902Ports::DMM_CHANNEL_END) {
                        if (error) {
                            *error = QStringLiteral("dmm ports exhausted");
                        }
                        return false;
                    }
                    binding.deviceKind = JYDeviceKind::PXIe8902;
                    binding.channel = state->nextDmm++;
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

