#ifndef DIAGNOSTICDATATYPES_H
#define DIAGNOSTICDATATYPES_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

// 一路采样信号的离散序列及采样率。
struct DiagnosticSignalSeries {
    QVector<double> samples;
    double sampleRateHz = 0.0;
};

// 诊断输入：元件标识、采样数据、策略参数和时间戳。
struct DiagnosticInput {
    QString componentRef;
    QString componentType;
    QMap<QString, DiagnosticSignalSeries> signalSeries;
    QMap<QString, QVariant> parameters;
    QDateTime timestamp;
};

// 诊断输出：是否通过、摘要、指标和报告 HTML。
struct DiagnosticReport {
    QString componentRef;
    QString componentType;
    bool success = false;
    QString summary;
    QMap<QString, QVariant> metrics;
    QString detailHtml;
};

#endif // DIAGNOSTICDATATYPES_H
