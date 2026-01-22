#ifndef PORTDEFINITIONS_H
#define PORTDEFINITIONS_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>

// 设备端口定义
namespace PortDefinitions {

enum class PortType {
    ANALOG_INPUT,
    ANALOG_OUTPUT,
    DIGITAL_INPUT,
    DIGITAL_OUTPUT,
    DMM_CHANNEL,
};

// JY5711 端口分配
struct JY5711Ports {
    static constexpr int ANALOG_OUTPUT_START = 0;
    static constexpr int ANALOG_OUTPUT_END = 15;
    static constexpr int DIGITAL_OUTPUT_START = 16;
    static constexpr int DIGITAL_OUTPUT_END = 31;
    static constexpr int TOTAL_PORTS = 32;
};

// JY5323 端口分配
struct JY5323Ports {
    static constexpr int ANALOG_INPUT_START = 0;
    static constexpr int ANALOG_INPUT_END = 31;
    static constexpr int TOTAL_PORTS = 32;
};

// JY5322 端口分配
struct JY5322Ports {
    static constexpr int DIGITAL_INPUT_START = 0;
    static constexpr int DIGITAL_INPUT_END = 15;
    static constexpr int TOTAL_PORTS = 16;
};

// JY8902 万用表端口分配
struct JY8902Ports {
    static constexpr int DMM_CHANNEL_START = 0;
    static constexpr int DMM_CHANNEL_END = 0;  // 2线法测量：通道0和通道1
    static constexpr int TOTAL_PORTS = 1;
};

} // namespace PortDefinitions

#endif // PORTDEFINITIONS_H
