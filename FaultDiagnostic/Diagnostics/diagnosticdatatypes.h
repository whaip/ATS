#ifndef DIAGNOSTICDATATYPES_H
#define DIAGNOSTICDATATYPES_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

struct DiagnosticSignalSeries {
    QVector<double> samples;
    double sampleRateHz = 0.0;
};

struct DiagnosticInput {
    QString componentRef;
    QString componentType;
    QMap<QString, DiagnosticSignalSeries> signalSeries;
    QMap<QString, QVariant> parameters;
    QDateTime timestamp;
};

struct DiagnosticReport {
    QString componentRef;
    QString componentType;
    bool success = false;
    QString summary;
    QMap<QString, QVariant> metrics;
    QString detailHtml;
};

#endif // DIAGNOSTICDATATYPES_H
