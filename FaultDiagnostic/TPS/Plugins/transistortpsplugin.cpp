#include "transistortpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

#include <QRegularExpression>

namespace {
QString deviceKindName(JYDeviceKind kind)
{
    switch (kind) {
    case JYDeviceKind::PXIe5322:
        return QStringLiteral("PXIe5322");
    case JYDeviceKind::PXIe5323:
        return QStringLiteral("PXIe5323");
    case JYDeviceKind::PXIe5711:
        return QStringLiteral("PXIe5711");
    case JYDeviceKind::PXIe8902:
        return QStringLiteral("PXIe8902");
    }
    return QStringLiteral("Unknown");
}

int resolveModeValue(const QVariant &value, int fallback)
{
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        return qBound(0, direct, 4);
    }

    const QString text = value.toString().trimmed().toUpper();
    const QRegularExpression re(QStringLiteral("MODE\\s*([0-4])"));
    const QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }

    return fallback;
}
}

TransistorTpsPlugin::TransistorTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString TransistorTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.transistor");
}

QString TransistorTpsPlugin::displayName() const
{
    return QStringLiteral("Transistor Pulse TPS");
}

QString TransistorTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> TransistorTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement TransistorTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition expectedMode;
    expectedMode.key = QStringLiteral("expectedMode");
    expectedMode.label = QStringLiteral("工况模式(MODE0~MODE4)");
    expectedMode.type = TPSParamType::Integer;
    expectedMode.defaultValue = 0;
    expectedMode.minValue = 0;
    expectedMode.maxValue = 4;
    expectedMode.stepValue = 1;

    TPSParamDefinition vdrv;
    vdrv.key = QStringLiteral("vdrvV");
    vdrv.label = QStringLiteral("驱动幅值Vdrv(V)");
    vdrv.type = TPSParamType::Double;
    vdrv.defaultValue = 3.3;
    vdrv.minValue = 0.1;
    vdrv.maxValue = 12.0;
    vdrv.stepValue = 0.1;

    TPSParamDefinition vcc;
    vcc.key = QStringLiteral("vccV");
    vcc.label = QStringLiteral("供电VCC(V)");
    vcc.type = TPSParamType::Double;
    vcc.defaultValue = 5.0;
    vcc.minValue = 0.1;
    vcc.maxValue = 60.0;
    vcc.stepValue = 0.1;

    TPSParamDefinition period;
    period.key = QStringLiteral("periodMs");
    period.label = QStringLiteral("周期Tper(ms)");
    period.type = TPSParamType::Double;
    period.defaultValue = 4.0;
    period.minValue = 0.01;
    period.maxValue = 1000.0;
    period.stepValue = 0.01;

    TPSParamDefinition ton;
    ton.key = QStringLiteral("tonMs");
    ton.label = QStringLiteral("高电平宽度Ton(ms)");
    ton.type = TPSParamType::Double;
    ton.defaultValue = 2.0;
    ton.minValue = 0.001;
    ton.maxValue = 1000.0;
    ton.stepValue = 0.001;

    TPSParamDefinition fs;
    fs.key = QStringLiteral("sampleRateHz");
    fs.label = QStringLiteral("采样率(Hz)");
    fs.type = TPSParamType::Double;
    fs.defaultValue = 1000000.0;
    fs.minValue = 1.0;
    fs.maxValue = 10e6;
    fs.stepValue = 1.0;

    TPSParamDefinition captureMs;
    captureMs.key = QStringLiteral("captureDurationMs");
    captureMs.label = QStringLiteral("采集时长(ms)");
    captureMs.type = TPSParamType::Integer;
    captureMs.defaultValue = 40;
    captureMs.minValue = 1;
    captureMs.maxValue = 60000;
    captureMs.stepValue = 1;

    TPSParamDefinition rb;
    rb.key = QStringLiteral("rbOhms");
    rb.label = QStringLiteral("基极电阻RB(Ω)");
    rb.type = TPSParamType::Double;
    rb.defaultValue = 20000.0;
    rb.minValue = 0.001;
    rb.maxValue = 1e9;
    rb.stepValue = 1.0;

    TPSParamDefinition rsIc;
    rsIc.key = QStringLiteral("icSenseOhms");
    rsIc.label = QStringLiteral("集电极测流电阻(Ω)");
    rsIc.type = TPSParamType::Double;
    rsIc.defaultValue = 100.0;
    rsIc.minValue = 0.0001;
    rsIc.maxValue = 1e9;
    rsIc.stepValue = 0.1;

    TPSParamDefinition iOpen;
    iOpen.key = QStringLiteral("iOpen_A");
    iOpen.label = QStringLiteral("开路阈值Iopen(A)");
    iOpen.type = TPSParamType::Double;
    iOpen.defaultValue = 0.0001;
    iOpen.minValue = 0.0;
    iOpen.maxValue = 10.0;
    iOpen.stepValue = 0.00001;

    TPSParamDefinition vShort;
    vShort.key = QStringLiteral("vShort_V");
    vShort.label = QStringLiteral("短路阈值Vshort(V)");
    vShort.type = TPSParamType::Double;
    vShort.defaultValue = 0.05;
    vShort.minValue = 0.0;
    vShort.maxValue = 10.0;
    vShort.stepValue = 0.001;

    TPSParamDefinition iLim;
    iLim.key = QStringLiteral("iLimit_A");
    iLim.label = QStringLiteral("限流阈值Ilim(A)");
    iLim.type = TPSParamType::Double;
    iLim.defaultValue = 0.04;
    iLim.minValue = 0.0;
    iLim.maxValue = 10.0;
    iLim.stepValue = 0.001;

    TPSParamDefinition iLeak;
    iLeak.key = QStringLiteral("iLeak_A");
    iLeak.label = QStringLiteral("漏电阈值Ileak(A)");
    iLeak.type = TPSParamType::Double;
    iLeak.defaultValue = 1e-7;
    iLeak.minValue = 0.0;
    iLeak.maxValue = 1.0;
    iLeak.stepValue = 1e-8;

    TPSParamDefinition betaNominal;
    betaNominal.key = QStringLiteral("betaNominal");
    betaNominal.label = QStringLiteral("标称增益β");
    betaNominal.type = TPSParamType::Double;
    betaNominal.defaultValue = 100.0;
    betaNominal.minValue = 0.001;
    betaNominal.maxValue = 1e6;
    betaNominal.stepValue = 1.0;

    TPSParamDefinition betaMargin;
    betaMargin.key = QStringLiteral("betaMargin");
    betaMargin.label = QStringLiteral("增益裕量δβ");
    betaMargin.type = TPSParamType::Double;
    betaMargin.defaultValue = 0.25;
    betaMargin.minValue = 0.0;
    betaMargin.maxValue = 5.0;
    betaMargin.stepValue = 0.01;

    TPSParamDefinition tTrip;
    tTrip.key = QStringLiteral("temperatureTripC");
    tTrip.label = QStringLiteral("温度停机阈值(℃)");
    tTrip.type = TPSParamType::Double;
    tTrip.defaultValue = 60.0;
    tTrip.minValue = -40.0;
    tTrip.maxValue = 250.0;
    tTrip.stepValue = 0.1;

    TPSParamDefinition onWindowStart;
    onWindowStart.key = QStringLiteral("onWindowStartRatio");
    onWindowStart.label = QStringLiteral("导通窗口起点比例");
    onWindowStart.type = TPSParamType::Double;
    onWindowStart.defaultValue = 0.6;
    onWindowStart.minValue = 0.0;
    onWindowStart.maxValue = 1.0;
    onWindowStart.stepValue = 0.01;

    TPSParamDefinition onWindowEnd;
    onWindowEnd.key = QStringLiteral("onWindowEndRatio");
    onWindowEnd.label = QStringLiteral("导通窗口终点比例");
    onWindowEnd.type = TPSParamType::Double;
    onWindowEnd.defaultValue = 0.9;
    onWindowEnd.minValue = 0.0;
    onWindowEnd.maxValue = 1.0;
    onWindowEnd.stepValue = 0.01;

    TPSParamDefinition offWindowStart;
    offWindowStart.key = QStringLiteral("offWindowStartRatio");
    offWindowStart.label = QStringLiteral("关断窗口起点比例");
    offWindowStart.type = TPSParamType::Double;
    offWindowStart.defaultValue = 0.2;
    offWindowStart.minValue = 0.0;
    offWindowStart.maxValue = 1.0;
    offWindowStart.stepValue = 0.01;

    TPSParamDefinition offWindowEnd;
    offWindowEnd.key = QStringLiteral("offWindowEndRatio");
    offWindowEnd.label = QStringLiteral("关断窗口终点比例");
    offWindowEnd.type = TPSParamType::Double;
    offWindowEnd.defaultValue = 0.8;
    offWindowEnd.minValue = 0.0;
    offWindowEnd.maxValue = 1.0;
    offWindowEnd.stepValue = 0.01;

    TPSParamDefinition baseDriveAnchor;
    baseDriveAnchor.key = QStringLiteral("baseDriveAnchor");
    baseDriveAnchor.label = QStringLiteral("Base驱动锚点");
    baseDriveAnchor.type = TPSParamType::Integer;
    baseDriveAnchor.defaultValue = -1;

    TPSParamDefinition vccAnchor;
    vccAnchor.key = QStringLiteral("vccAnchor");
    vccAnchor.label = QStringLiteral("VCC锚点");
    vccAnchor.type = TPSParamType::Integer;
    vccAnchor.defaultValue = -1;

    TPSParamDefinition collectorAnchor;
    collectorAnchor.key = QStringLiteral("collectorAnchor");
    collectorAnchor.label = QStringLiteral("Collector锚点");
    collectorAnchor.type = TPSParamType::Integer;
    collectorAnchor.defaultValue = -1;

    TPSParamDefinition emitterAnchor;
    emitterAnchor.key = QStringLiteral("emitterAnchor");
    emitterAnchor.label = QStringLiteral("Emitter锚点");
    emitterAnchor.type = TPSParamType::Integer;
    emitterAnchor.defaultValue = -1;

    TPSParamDefinition icSenseAnchor;
    icSenseAnchor.key = QStringLiteral("icSenseAnchor");
    icSenseAnchor.label = QStringLiteral("Ic分流锚点");
    icSenseAnchor.type = TPSParamType::Integer;
    icSenseAnchor.defaultValue = -1;

    TPSParamDefinition ibSenseAnchor;
    ibSenseAnchor.key = QStringLiteral("ibSenseAnchor");
    ibSenseAnchor.label = QStringLiteral("Ib分流锚点");
    ibSenseAnchor.type = TPSParamType::Integer;
    ibSenseAnchor.defaultValue = -1;

    TPSParamDefinition temperatureAnchor;
    temperatureAnchor.key = QStringLiteral("temperatureAnchor");
    temperatureAnchor.label = QStringLiteral("温度区域锚点");
    temperatureAnchor.type = TPSParamType::Integer;
    temperatureAnchor.defaultValue = -1;

    TPSPortRequest outputReq;
    outputReq.type = TPSPortType::VoltageOutput;
    outputReq.count = 1;
    outputReq.identifiers = {QStringLiteral("transistorDriveOutput")};

    TPSPortRequest vccOutputReq;
    vccOutputReq.type = TPSPortType::VoltageOutput;
    vccOutputReq.count = 1;
    vccOutputReq.identifiers = {QStringLiteral("transistorVccOutput")};

    TPSPortRequest vcReq;
    vcReq.type = TPSPortType::VoltageInput;
    vcReq.count = 1;
    vcReq.identifiers = {QStringLiteral("transistorVcInput")};

    TPSPortRequest veReq;
    veReq.type = TPSPortType::VoltageInput;
    veReq.count = 1;
    veReq.identifiers = {QStringLiteral("transistorVeInput")};

    TPSPortRequest icReq;
    icReq.type = TPSPortType::VoltageInput;
    icReq.count = 1;
    icReq.identifiers = {QStringLiteral("transistorIcSenseInput")};

    TPSPortRequest ibReq;
    ibReq.type = TPSPortType::VoltageInput;
    ibReq.count = 1;
    ibReq.identifiers = {QStringLiteral("transistorIbSenseInput")};

    requirement.parameters = {
        expectedMode,
        vdrv,
        vcc,
        period,
        ton,
        fs,
        captureMs,
        rb,
        rsIc,
        iOpen,
        vShort,
        iLim,
        iLeak,
        betaNominal,
        betaMargin,
        tTrip,
        onWindowStart,
        onWindowEnd,
        offWindowStart,
        offWindowEnd,
        baseDriveAnchor,
        vccAnchor,
        collectorAnchor,
        emitterAnchor,
        icSenseAnchor,
        ibSenseAnchor,
        temperatureAnchor
    };
    requirement.ports = {outputReq, vccOutputReq, vcReq, veReq, icReq, ibReq};

    return requirement;
}

bool TransistorTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                                          const QMap<QString, QVariant> &settings,
                                          TPSDevicePlan *plan,
                                          QString *error)
{
    if (!plan) {
        if (error) {
            *error = QStringLiteral("plan is null");
        }
        return false;
    }

    const QVector<TPSPortBinding> effectiveBindings = bindings.isEmpty() ? m_allocatedBindings : bindings;

    const TPSPortBinding *driveOut = findBinding(effectiveBindings, QStringLiteral("transistorDriveOutput"));
    const TPSPortBinding *vccOut = findBinding(effectiveBindings, QStringLiteral("transistorVccOutput"));
    const TPSPortBinding *vcIn = findBinding(effectiveBindings, QStringLiteral("transistorVcInput"));
    const TPSPortBinding *veIn = findBinding(effectiveBindings, QStringLiteral("transistorVeInput"));
    const TPSPortBinding *icIn = findBinding(effectiveBindings, QStringLiteral("transistorIcSenseInput"));
    const TPSPortBinding *ibIn = findBinding(effectiveBindings, QStringLiteral("transistorIbSenseInput"));

    if (!driveOut || !vccOut || !vcIn || !veIn || !icIn || !ibIn) {
        if (error) {
            *error = QStringLiteral("missing required bindings for transistor TPS");
        }
        return false;
    }

    if (driveOut->deviceKind != JYDeviceKind::PXIe5711 || vccOut->deviceKind != JYDeviceKind::PXIe5711) {
        if (error) {
            *error = QStringLiteral("transistorDriveOutput and transistorVccOutput must bind to PXIe5711");
        }
        return false;
    }

    const bool vcOn532x = (vcIn->deviceKind == JYDeviceKind::PXIe5322 || vcIn->deviceKind == JYDeviceKind::PXIe5323);
    const bool veOn532x = (veIn->deviceKind == JYDeviceKind::PXIe5322 || veIn->deviceKind == JYDeviceKind::PXIe5323);
    const bool icOn532x = (icIn->deviceKind == JYDeviceKind::PXIe5322 || icIn->deviceKind == JYDeviceKind::PXIe5323);
    const bool ibOn532x = (ibIn->deviceKind == JYDeviceKind::PXIe5322 || ibIn->deviceKind == JYDeviceKind::PXIe5323);
    if (!vcOn532x || !veOn532x || !icOn532x || !ibOn532x) {
        if (error) {
            *error = QStringLiteral("all transistor VI inputs must bind to PXIe5322 or PXIe5323");
        }
        return false;
    }

    if (vcIn->deviceKind != veIn->deviceKind || vcIn->deviceKind != icIn->deviceKind || vcIn->deviceKind != ibIn->deviceKind) {
        if (error) {
            *error = QStringLiteral("transistor inputs must be on the same capture card");
        }
        return false;
    }

    const double vdrv = settings.value(QStringLiteral("vdrvV"), 3.3).toDouble();
    const double vcc = settings.value(QStringLiteral("vccV"), 5.0).toDouble();
    const double periodMs = qMax(0.01, settings.value(QStringLiteral("periodMs"), 4.0).toDouble());
    const double tonMs = qBound(0.001, settings.value(QStringLiteral("tonMs"), 2.0).toDouble(), periodMs - 0.0001);
    const double frequency = 1000.0 / periodMs;
    const double duty = qBound(0.01, tonMs / periodMs, 0.99);

    TPSDevicePlan devicePlan;
    devicePlan.bindings = effectiveBindings;
    devicePlan.cfg532x = build532xInitConfig(vcIn->deviceKind);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();

    const int maxInputChannel = qMax(qMax(vcIn->channel, veIn->channel), qMax(icIn->channel, ibIn->channel));
    devicePlan.cfg532x.cfg532x.channelCount = maxInputChannel + 1;

    const int maxOutChannel = qMax(driveOut->channel, vccOut->channel);
    devicePlan.cfg5711.cfg5711.channelCount = maxOutChannel + 1;
    devicePlan.cfg5711.cfg5711.enabledChannels = {driveOut->channel, vccOut->channel};
    devicePlan.cfg5711.cfg5711.waveforms.clear();

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(driveOut->channel,
                                QStringLiteral("SquareWave"),
                                PXIe5711_make_params({
                                    {"amplitude", vdrv},
                                    {"frequency", frequency},
                                    {"dutyCycle", duty},
                                })));

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(vccOut->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({
                                    {"amplitude", vcc},
                                    {"frequency", frequency},
                                })));

    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    const QVector<const TPSPortBinding *> all = {driveOut, vccOut, vcIn, veIn, icIn, ibIn};
    for (const TPSPortBinding *binding : all) {
        TPSSignalRequest req;
        req.id = binding->identifier;
        req.signalType = binding->identifier;
        req.unit = QStringLiteral("V");
        if (binding == driveOut) {
            req.value = vdrv;
        } else if (binding == vccOut) {
            req.value = vcc;
        }
        devicePlan.requests.push_back(req);
    }

    devicePlan.wiringSteps = {
        QStringLiteral("将%1接到%2").arg(portText(*driveOut), anchorText(settings, QStringLiteral("baseDriveAnchor"))),
        QStringLiteral("将%1接到%2").arg(portText(*vccOut), anchorText(settings, QStringLiteral("vccAnchor"))),
        QStringLiteral("将%1接到%2").arg(portText(*vcIn), anchorText(settings, QStringLiteral("collectorAnchor"))),
        QStringLiteral("将%1接到%2").arg(portText(*veIn), anchorText(settings, QStringLiteral("emitterAnchor"))),
        QStringLiteral("将%1接到%2").arg(portText(*icIn), anchorText(settings, QStringLiteral("icSenseAnchor"))),
        QStringLiteral("将%1接到%2").arg(portText(*ibIn), anchorText(settings, QStringLiteral("ibSenseAnchor")))
    };

    devicePlan.temperatureGuide = QStringLiteral("请在%1完成器件本体与夹具接触点ROI框选")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));

    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("baseDriveAnchor")),
        anchorText(settings, QStringLiteral("vccAnchor")),
        anchorText(settings, QStringLiteral("collectorAnchor")),
        anchorText(settings, QStringLiteral("emitterAnchor")),
        anchorText(settings, QStringLiteral("icSenseAnchor")),
        anchorText(settings, QStringLiteral("ibSenseAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("mode.coverage"), QStringLiteral("MODE0~MODE4"));
    devicePlan.guide.extensions.insert(QStringLiteral("default.vdrvV"), vdrv);
    devicePlan.guide.extensions.insert(QStringLiteral("default.vccV"), vcc);
    devicePlan.guide.extensions.insert(QStringLiteral("default.periodMs"), periodMs);
    devicePlan.guide.extensions.insert(QStringLiteral("default.tonMs"), tonMs);
    devicePlan.guide.extensions.insert(QStringLiteral("default.sampleRateHz"), settings.value(QStringLiteral("sampleRateHz"), 1000000.0).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.rbOhms"), settings.value(QStringLiteral("rbOhms"), 20000.0).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.rlOhms"), settings.value(QStringLiteral("icSenseOhms"), 100.0).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.iOpen_A"), settings.value(QStringLiteral("iOpen_A"), 1e-4).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.vShort_V"), settings.value(QStringLiteral("vShort_V"), 0.05).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.iLimit_A"), settings.value(QStringLiteral("iLimit_A"), 0.04).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.iLeak_A"), settings.value(QStringLiteral("iLeak_A"), 1e-7).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.betaMargin"), settings.value(QStringLiteral("betaMargin"), 0.25).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.temperatureTripC"), settings.value(QStringLiteral("temperatureTripC"), 60.0).toDouble());

    *plan = devicePlan;
    return true;
}

bool TransistorTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    m_settings = settings;
    m_allocatedBindings = TPSRuntimeContext::decodeBindings(settings.value(TPSRuntimeContext::allocatedBindingsKey()));

    const TPSPortBinding *driveOut = findBinding(m_allocatedBindings, QStringLiteral("transistorDriveOutput"));
    const TPSPortBinding *vccOut = findBinding(m_allocatedBindings, QStringLiteral("transistorVccOutput"));
    if (!driveOut || !vccOut) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("missing allocated binding: transistorDriveOutput/transistorVccOutput");
        }
        return false;
    }

    if (driveOut->deviceKind != JYDeviceKind::PXIe5711 || vccOut->deviceKind != JYDeviceKind::PXIe5711) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("transistor outputs must bind to PXIe5711");
        }
        return false;
    }

    const double vdrv = settings.value(QStringLiteral("vdrvV"), 3.3).toDouble();
    const double vcc = settings.value(QStringLiteral("vccV"), 5.0).toDouble();
    const double periodMs = qMax(0.01, settings.value(QStringLiteral("periodMs"), 4.0).toDouble());
    const double tonMs = qBound(0.001, settings.value(QStringLiteral("tonMs"), 2.0).toDouble(), periodMs - 0.0001);
    const double frequency = 1000.0 / periodMs;
    const double duty = qBound(0.01, tonMs / periodMs, 0.99);

    m_config5711 = build5711InitConfig();
    const int maxOutChannel = qMax(driveOut->channel, vccOut->channel);
    m_config5711.cfg5711.channelCount = maxOutChannel + 1;
    m_config5711.cfg5711.enabledChannels = {driveOut->channel, vccOut->channel};
    m_config5711.cfg5711.waveforms.clear();

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(driveOut->channel,
                                QStringLiteral("SquareWave"),
                                PXIe5711_make_params({
                                    {"amplitude", vdrv},
                                    {"frequency", frequency},
                                    {"dutyCycle", duty},
                                })));

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(vccOut->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({
                                    {"amplitude", vcc},
                                    {"frequency", frequency},
                                })));

    m_configReady = true;
    return true;
}

bool TransistorTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("Transistor TPS strategy executed");
    result->metrics.insert(QStringLiteral("expectedMode"), expectedMode());
    result->metrics.insert(QStringLiteral("vdrvV"),
                           m_settings.value(QStringLiteral("vdrvV"), 3.3).toDouble());
    result->metrics.insert(QStringLiteral("vccV"),
                           m_settings.value(QStringLiteral("vccV"), 5.0).toDouble());
    result->metrics.insert(QStringLiteral("periodMs"),
                           m_settings.value(QStringLiteral("periodMs"), 4.0).toDouble());
    result->metrics.insert(QStringLiteral("tonMs"),
                           m_settings.value(QStringLiteral("tonMs"), 2.0).toDouble());
    result->metrics.insert(QStringLiteral("sampleRateHz"),
                           m_settings.value(QStringLiteral("sampleRateHz"), 1000000.0).toDouble());

    return true;
}

const TPSPortBinding *TransistorTpsPlugin::findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier)
{
    for (const TPSPortBinding &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}

QString TransistorTpsPlugin::portText(const TPSPortBinding &binding)
{
    const QString resource = binding.resourceId.trimmed().isEmpty()
        ? QStringLiteral("%1.CH%2").arg(deviceKindName(binding.deviceKind)).arg(binding.channel)
        : binding.resourceId;
    return QStringLiteral("%1(%2)").arg(binding.identifier, resource);
}

QString TransistorTpsPlugin::anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QVariant value = settings.value(key, -1);
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("-1") : text;
}

int TransistorTpsPlugin::expectedMode() const
{
    return resolveModeValue(m_settings.value(QStringLiteral("expectedMode")), 0);
}
