#ifndef DIAGNOSTICALGORITHM_H
#define DIAGNOSTICALGORITHM_H

#include "diagnosticdatatypes.h"

class DiagnosticAlgorithm
{
public:
    virtual ~DiagnosticAlgorithm() = default;

    virtual QString componentType() const = 0;
    virtual DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const = 0;
};

#endif // DIAGNOSTICALGORITHM_H
