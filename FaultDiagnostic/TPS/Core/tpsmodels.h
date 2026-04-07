#ifndef TPSMODELS_H
#define TPSMODELS_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "../../../IODevices/JYDevices/jydevicetype.h"

// 一条 UTR 测试项，描述当前任务内某个元件的策略参数。
struct UTRItem {
    QString componentRef;
    QString planId;
    QMap<QString, QVariant> parameters;
};

// TPS 执行输入，通常对应一次运行中的一批待测项。
struct TPSRequest {
    QString runId;
    QString boardId;
    QDateTime createdAt;
    QVector<UTRItem> items;
};

// TPS 执行输出，只描述策略执行结果和采集阶段指标，不负责故障判定。
struct TPSResult {
    QString runId;
    bool success = false;
    QString summary;
    QMap<QString, QVariant> metrics;
};

// TPS 参数在界面中的基础类型。
enum class TPSParamType {
    String = 0,
    Integer,
    Double,
    Boolean,
    Enum
};

// TPS 所需端口类型，会映射到具体设备资源。
enum class TPSPortType {
    CurrentOutput = 0,
    VoltageOutput,
    CurrentInput,
    VoltageInput,
    DmmChannel
};

// 单个参数的定义，供配置窗口动态生成表单。
struct TPSParamDefinition {
    QString key;
    QString label;
    TPSParamType type = TPSParamType::String;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    QVariant stepValue;
    QStringList enumOptions;
    QString unit;
    bool required = true;
};

// 插件声明自己需要多少个什么类型的端口。
struct TPSPortRequest {
    TPSPortType type = TPSPortType::CurrentOutput;
    int count = 1;
    QStringList identifiers;
};

// 端口分配审核通过后的实际绑定结果。
struct TPSPortBinding {
    QString identifier;
    TPSPortType type = TPSPortType::CurrentOutput;
    JYDeviceKind deviceKind = JYDeviceKind::PXIe5322;
    int channel = 0;
    QString resourceId;
};

// 运行编排层使用的信号请求，表示本次测试希望驱动哪些激励/采集资源。
struct TPSSignalRequest {
    QString id;
    QString signalType;
    double value = 0.0;
    QString unit;
};

// 工作流引导信息，给接线和测温 ROI 选择界面使用。
struct TPSWorkflowGuide {
    QStringList wiringSteps;
    QStringList roiSteps;
    QMap<QString, QVariant> extensions;
};

// TPS 插件产出的完整设备计划，连接策略层和设备层。
struct TPSDevicePlan {
    QVector<TPSPortBinding> bindings;
    QVector<TPSSignalRequest> requests;
    QStringList wiringSteps;
    QString temperatureGuide;
    TPSWorkflowGuide guide;
    JYDeviceConfig cfg532x;
    JYDeviceConfig cfg5711;
    JYDeviceConfig cfg8902;
};

// TPS 插件对参数和端口的总体需求描述。
struct TPSPluginRequirement {
    QVector<TPSParamDefinition> parameters;
    QVector<TPSPortRequest> ports;
};

#endif // TPSMODELS_H
