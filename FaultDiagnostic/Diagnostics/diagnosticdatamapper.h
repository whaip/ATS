#ifndef DIAGNOSTICDATAMAPPER_H
#define DIAGNOSTICDATAMAPPER_H

#include <QMap>
#include <QString>

#include "diagnosticdatatypes.h"
#include "../TPS/Core/tpsmodels.h"
#include "../../IODevices/JYDevices/jydevicetype.h"

class DiagnosticDataMapper
{
public:
    static QMap<QString, DiagnosticSignalSeries> mapSignals(const JYAlignedBatch &batch,
                                                            const QVector<TPSPortBinding> &bindings);
};

#endif // DIAGNOSTICDATAMAPPER_H
