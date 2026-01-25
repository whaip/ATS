#ifndef DOMAIN_TESTPLAN_H
#define DOMAIN_TESTPLAN_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include "../../IODevices/portdefinitions.h"
 

struct ParameterSpec {
    QString key;
    QString unit;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    double tolerancePercent = 0.0;
    bool required = true;
};

struct TemperatureSpec {
    QPointF MonitorPoint;
    QRectF MonitorPosition;
    double alarmThresholdC = 80.0;
    bool needContinuousCapture = false;
    QString captureMode;
};

struct PortRequirementSpec {
    PortDefinitions::PortType portType;
    int count = 1;
};

struct TestPlanTemplate {
    QString planId;
    QString name;
    QVector<PortRequirementSpec> portRequirements;
    QVector<ParameterSpec> parameterSchema;
    TemperatureSpec temperatureSpec;
};

struct ComponentPlanBinding {
    QString componentRef;
    QString planId;
    QMap<QString, QVariant> parameterValues;
    bool hasTemperatureOverride = false;
    TemperatureSpec temperatureOverride;
};

#endif // DOMAIN_TESTPLAN_H
