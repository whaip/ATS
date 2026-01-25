#ifndef TPSMODELS_H
#define TPSMODELS_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

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

#endif // TPSMODELS_H
