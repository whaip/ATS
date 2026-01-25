#ifndef DOMAIN_BOARDPROFILE_H
#define DOMAIN_BOARDPROFILE_H

#include <QDateTime>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include "../FaultDiagnostic/Core/testplan.h"

struct AnchorPoint {
    QString id;
    QString label;
    QRectF position;
};

struct ComponentInstance {
    QString reference;
    QString type;
    QString model;
    QRectF bbox;
    QVector<AnchorPoint> anchors;
};

struct BoardProfile {
    QString boardId;
    QString name;
    QString imagePath;
    QString version;
    QDateTime createdAt;
    QVector<ComponentInstance> components;
    QVector<ComponentPlanBinding> planBindings;
};

#endif // DOMAIN_BOARDPROFILE_H
