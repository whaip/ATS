#ifndef CAPACITORTPSPLUGIN_H
#define CAPACITORTPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class CapacitorTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit CapacitorTpsPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;

    QVector<TPSParamDefinition> parameterDefinitions() const override;
    TPSPluginRequirement requirements() const override;

    bool buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                         const QMap<QString, QVariant> &settings,
                         TPSDevicePlan *plan,
                         QString *error) override;

    bool configure(const QMap<QString, QVariant> &settings, QString *error) override;
    bool execute(const TPSRequest &request, TPSResult *result, QString *error) override;

private:
    struct SignalSeriesPair {
        QVector<double> vin;
        QVector<double> vcap;
        double sampleRateHz = 0.0;
    };

    struct FeatureSet {
        double vinAmplitude = 0.0;
        double vcapAmplitude = 0.0;
        double gain = 0.0;
        double highRetention = 0.0;
        double estimatedTauSec = 0.0;
        double nominalTauSec = 0.0;
        double edgeDrop = 0.0;
        int sampleCount = 0;
    };

    bool collectSignalSeries(const TPSRequest &request, SignalSeriesPair *series) const;
    FeatureSet estimateFeatures(const SignalSeriesPair &series) const;
    int classifyMode(const FeatureSet &features) const;
    int expectedMode() const;

    static void appendSamplesFromVariant(const QVariant &value, QVector<double> *samples);
    static QString modeName(int mode);
    static const TPSPortBinding *findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier);

    QMap<QString, QVariant> m_settings;
    QVector<TPSPortBinding> m_allocatedBindings;
    JYDeviceConfig m_config5711;
    bool m_configReady = false;
};

#endif // CAPACITORTPSPLUGIN_H
