#ifndef TPSMODELS_H
#define TPSMODELS_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

#include "../../../IODevices/JYDevices/jydevicetype.h"

struct UTRItem {
    QString componentRef;
    QString planId;
    QMap<QString, QVariant> parameters;
};

struct TPSRequest {
    QString runId;
    QString boardId;
    QDateTime createdAt;
    QVector<UTRItem> items;
};

struct TPSResult {
    QString runId;
    bool success = false;
    QString summary;
    QMap<QString, QVariant> metrics;
};

enum class TPSParamType {
    String = 0,
    Integer,
    Double,
    Boolean,
    Enum
};

enum class TPSPortType {
    CurrentOutput = 0,
    VoltageOutput,
    CurrentInput,
    VoltageInput,
    DmmChannel
};

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

struct TPSPortRequest {
    TPSPortType type = TPSPortType::CurrentOutput;
    int count = 1;
    QStringList identifiers;
};

struct TPSPortBinding {
    QString identifier;
    TPSPortType type = TPSPortType::CurrentOutput;
    JYDeviceKind deviceKind = JYDeviceKind::PXIe5322;
    int channel = 0;
    int slot = 0;
    QString resourceId;
};

struct TPSSignalRequest {
    QString id;
    QString signalType;
    double value = 0.0;
    QString unit;
};

struct TPSDevicePlan {
    QVector<TPSPortBinding> bindings;
    QVector<TPSSignalRequest> requests;
    JYDeviceConfig cfg532x;
    JYDeviceConfig cfg5711;
    JYDeviceConfig cfg8902;
};

struct TPSPluginRequirement {
    QVector<TPSParamDefinition> parameters;
    QVector<TPSPortRequest> ports;
};

#endif // TPSMODELS_H
