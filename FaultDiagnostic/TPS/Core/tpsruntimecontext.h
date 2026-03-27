#ifndef TPSRUNTIMECONTEXT_H
#define TPSRUNTIMECONTEXT_H

#include <QMap>
#include <QVariant>
#include <QVector>

#include "tpsmodels.h"

namespace TPSRuntimeContext {

QString allocatedBindingsKey();

QVariantList encodeBindings(const QVector<TPSPortBinding> &bindings);
QVector<TPSPortBinding> decodeBindings(const QVariant &value);

}

#endif // TPSRUNTIMECONTEXT_H
