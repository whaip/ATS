#ifndef DOMAIN_SEQUENCE_H
#define DOMAIN_SEQUENCE_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

struct TestTask {
    QString taskId;
    QVector<QString> componentRefs;
    QMap<QString, QVariant> planOverride;
    bool needContinuousTemp = false;
    int tempFrameCount = 0;
};

struct TestSequence {
    QString sequenceId;
    QString name;
    QDateTime createdAt;
    QVector<TestTask> tasks;
};

#endif // DOMAIN_SEQUENCE_H
