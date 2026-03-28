#include "faultdiagnostic.h"
#include "ui_faultdiagnostic.h"
#include "../Workflow/BatchParameterReview/batchparamreviewdialog.h"
#include "../Workflow/PortAllocation/portallocationreviewdialog.h"
#include "../Workflow/WiringGuide/wiringguidedialog.h"
#include "../TPS/Core/tpsruntimecontext.h"
#include "../TPS/Manager/tpsbuiltinregistry.h"

#include "../../ComponentsDetect/componenttypes.h"
#include "../../IRCamera/ircamera.h"
#include "../../IODevices/uestcqcustomplot.h"
#include "../../HDCamera/camerastation.h"
#include "../../IODevices/JYDevices/jythreadmanager.h"
#include "../../IODevices/JYDevices/jydatapipeline.h"
#include "../../IRCamera/ircamerastation.h"
#include "../../logger.h"
#include "../Core/deviceportmanager.h"
#include "../Diagnostics/diagnosticdatamapper.h"
#include "../Core/captureddatamanager.h"
#include "../../IODevices/JYDevices/jydeviceconfigutils.h"
#include <QMessageBox>

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QListWidget>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSplitter>
#include <QSet>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
QString deviceKindNameForLog(JYDeviceKind kind)
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

QString summarizeBindingsForLog(const QVector<TPSPortBinding> &bindings)
{
    QStringList parts;
    parts.reserve(bindings.size());
    for (const auto &binding : bindings) {
        parts.push_back(QStringLiteral("%1:%2->%3")
                            .arg(binding.identifier,
                                 binding.resourceId,
                                 deviceKindNameForLog(binding.deviceKind)));
    }
    return parts.join(QStringLiteral(", "));
}

QString resolveExistingFileByCandidates(const QStringList &relativeCandidates)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList allCandidates;

    for (const QString &relative : relativeCandidates) {
        allCandidates << QDir(appDir).filePath(relative);
    }

    QDir probe(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        for (const QString &relative : relativeCandidates) {
            allCandidates << probe.filePath(relative);
        }
        if (!probe.cdUp()) {
            break;
        }
    }

    for (const QString &path : allCandidates) {
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }

    return allCandidates.isEmpty() ? QString() : QDir::cleanPath(allCandidates.first());
}

QString resolveExistingDirByCandidates(const QStringList &relativeCandidates)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList allCandidates;

    for (const QString &relative : relativeCandidates) {
        allCandidates << QDir(appDir).filePath(relative);
    }

    QDir probe(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        for (const QString &relative : relativeCandidates) {
            allCandidates << probe.filePath(relative);
        }
        if (!probe.cdUp()) {
            break;
        }
    }

    for (const QString &path : allCandidates) {
        QFileInfo info(path);
        if (info.exists() && info.isDir()) {
            return QDir::cleanPath(path);
        }
    }

    return allCandidates.isEmpty() ? QString() : QDir::cleanPath(allCandidates.first());
}

QString resolveParamSnapshotDbPath()
{
    return resolveExistingFileByCandidates({
        QStringLiteral("board_db/i_params_db.json"),
        QStringLiteral("build/release/board_db/i_params_db.json"),
        QStringLiteral("build/debug/board_db/i_params_db.json")
    });
}

double signalSeriesDurationSec(const QMap<QString, DiagnosticSignalSeries> &signalSeries)
{
    double durationSec = 0.0;
    for (auto it = signalSeries.begin(); it != signalSeries.end(); ++it) {
        const DiagnosticSignalSeries &series = it.value();
        if (series.samples.isEmpty() || series.sampleRateHz <= 0.0) {
            continue;
        }
        durationSec = qMax(durationSec,
                           static_cast<double>(series.samples.size()) / series.sampleRateHz);
    }
    return durationSec;
}

QVector<double> buildUniformTimelineForDuration(int sampleCount, double durationSec)
{
    QVector<double> timeline;
    if (sampleCount <= 0) {
        return timeline;
    }

    timeline.reserve(sampleCount);
    if (sampleCount == 1 || durationSec <= 0.0) {
        timeline.push_back(0.0);
        return timeline;
    }

    const double step = durationSec / static_cast<double>(sampleCount - 1);
    for (int index = 0; index < sampleCount; ++index) {
        timeline.push_back(step * index);
    }
    return timeline;
}

int componentIdFromRef(const QString &componentRef)
{
    const QString trimmed = componentRef.trimmed();
    if (trimmed.isEmpty()) {
        return -1;
    }

    bool ok = false;
    const int direct = trimmed.toInt(&ok);
    if (ok) {
        return direct;
    }

    const QRegularExpression re(QStringLiteral("(\\d+)$"));
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        return -1;
    }
    return match.captured(1).toInt(&ok);
}

QString focusTargetFromVariant(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    bool ok = false;
    const int anchorId = value.toInt(&ok);
    if (ok && anchorId >= 0) {
        return QString::number(anchorId);
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text == QStringLiteral("-1")) {
        return {};
    }
    return text;
}

QStringList focusTargetListFromVariant(const QVariant &value)
{
    QStringList targets;
    if (!value.isValid() || value.isNull()) {
        return targets;
    }

    if (value.canConvert<QStringList>()) {
        const QStringList list = value.toStringList();
        for (const QString &entry : list) {
            const QString target = focusTargetFromVariant(entry);
            if (!target.isEmpty()) {
                targets.push_back(target);
            }
        }
        return targets;
    }

    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        for (const QVariant &entry : list) {
            const QString target = focusTargetFromVariant(entry);
            if (!target.isEmpty()) {
                targets.push_back(target);
            }
        }
        return targets;
    }

    const QString single = focusTargetFromVariant(value);
    if (!single.isEmpty()) {
        targets.push_back(single);
    }
    return targets;
}

QStringList inferWiringFocusTargets(const QMap<QString, QVariant> &settings)
{
    QStringList targets;
    const QStringList keys = {
        QStringLiteral("outputPositiveAnchor"),
        QStringLiteral("outputNegativeAnchor"),
        QStringLiteral("inputPositiveAnchor"),
        QStringLiteral("inputNegativeAnchor")
    };

    for (const QString &key : keys) {
        targets.push_back(focusTargetFromVariant(settings.value(key)));
    }
    return targets;
}

QStringList inferRoiFocusTargets(const QMap<QString, QVariant> &settings)
{
    return {focusTargetFromVariant(settings.value(QStringLiteral("temperatureAnchor")))};
}

QPointF maxTemperaturePointInRect(const IRCameraStation::Frame &frame, const QRectF &rect, bool *ok = nullptr)
{
    if (ok) {
        *ok = false;
    }
    if (frame.irImage.isNull() || frame.temperatureMatrix.isEmpty() || !frame.matrixSize.isValid()) {
        return {};
    }

    QRectF bounded = rect.normalized();
    bounded.setLeft(std::clamp(bounded.left(), 0.0, static_cast<double>(frame.irImage.width())));
    bounded.setTop(std::clamp(bounded.top(), 0.0, static_cast<double>(frame.irImage.height())));
    bounded.setRight(std::clamp(bounded.right(), 0.0, static_cast<double>(frame.irImage.width())));
    bounded.setBottom(std::clamp(bounded.bottom(), 0.0, static_cast<double>(frame.irImage.height())));
    if (bounded.width() <= 0.0 || bounded.height() <= 0.0) {
        return {};
    }

    const int x0 = std::clamp(static_cast<int>(bounded.left() / frame.irImage.width() * frame.matrixSize.width()), 0, frame.matrixSize.width() - 1);
    const int x1 = std::clamp(static_cast<int>(bounded.right() / frame.irImage.width() * frame.matrixSize.width()), 0, frame.matrixSize.width() - 1);
    const int y0 = std::clamp(static_cast<int>(bounded.top() / frame.irImage.height() * frame.matrixSize.height()), 0, frame.matrixSize.height() - 1);
    const int y1 = std::clamp(static_cast<int>(bounded.bottom() / frame.irImage.height() * frame.matrixSize.height()), 0, frame.matrixSize.height() - 1);

    double bestTemp = -std::numeric_limits<double>::infinity();
    int bestX = -1;
    int bestY = -1;
    for (int y = qMin(y0, y1); y <= qMax(y0, y1); ++y) {
        const int row = y * frame.matrixSize.width();
        for (int x = qMin(x0, x1); x <= qMax(x0, x1); ++x) {
            const int index = row + x;
            if (index < 0 || index >= frame.temperatureMatrix.size()) {
                continue;
            }
            const double temperature = frame.temperatureMatrix.at(index);
            if (temperature > bestTemp) {
                bestTemp = temperature;
                bestX = x;
                bestY = y;
            }
        }
    }

    if (bestX < 0 || bestY < 0) {
        return {};
    }

    if (ok) {
        *ok = true;
    }
    return QPointF((bestX + 0.5) * frame.irImage.width() / frame.matrixSize.width(),
                   (bestY + 0.5) * frame.irImage.height() / frame.matrixSize.height());
}

QStringList resolveFocusTargets(const TPSDevicePlan &plan,
                                const QMap<QString, QVariant> &settings,
                                bool wiring)
{
    const QString extensionKey = wiring
        ? QStringLiteral("wiringFocusTargets")
        : QStringLiteral("roiFocusTargets");
    const QStringList extensionTargets = focusTargetListFromVariant(plan.guide.extensions.value(extensionKey));
    if (!extensionTargets.isEmpty()) {
        return extensionTargets;
    }
    return wiring ? inferWiringFocusTargets(settings) : inferRoiFocusTargets(settings);
}

QMap<QString, QVariant> loadTaskParamsFromDb(const QString &dbPath,
                                             const QString &boardId,
                                             int componentId,
                                             const QString &pluginId)
{
    QMap<QString, QVariant> params;
    if (componentId < 0) {
        return params;
    }

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return params;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return params;
    }

    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    QJsonObject matched;

    auto scoreOf = [&](const QJsonObject &item) {
        int score = 0;
        if (item.value(QStringLiteral("componentId")).toInt(-1) == componentId) {
            score += 10;
        } else {
            return -1;
        }
        if (item.value(QStringLiteral("pluginId")).toString() == pluginId) {
            score += 5;
        }
        if (!boardId.trimmed().isEmpty() && item.value(QStringLiteral("boardId")).toString() == boardId) {
            score += 3;
        }
        return score;
    };

    int bestScore = -1;
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject item = value.toObject();
        const int score = scoreOf(item);
        if (score > bestScore) {
            bestScore = score;
            matched = item;
        }
    }

    const QJsonObject paramObject = matched.value(QStringLiteral("parameters")).toObject();
    for (auto it = paramObject.begin(); it != paramObject.end(); ++it) {
        params.insert(it.key(), it.value().toVariant());
    }

    return params;
}
}

FaultDiagnostic::FaultDiagnostic(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FaultDiagnostic)
{
    ui->setupUi(this);
    buildWidgets();
    applyThemeQss();
    m_runtime = new SystemRuntimeOrchestration(this);
    m_runtime->setCameraStation(CameraStation::instance());
    m_threadManager = new JYThreadManager(this);
    m_ownsThreadManager = true;
    m_runtime->setThreadManager(m_threadManager);
    bindThreadManagerSignals();
    if (m_runtime->frm()) {
        FrameworkSecurityService::UserContext user;
        user.userId = QStringLiteral("local");
        user.role = QStringLiteral("admin");
        m_runtime->frm()->setUser(user);
    }

    m_tpsManager = new TPSPluginManager(this);
    registerDefaultTpsBuiltins(m_tpsManager, m_tpsManager);
    m_tpsManager->loadAll(nullptr);

    m_diagPluginManager = new DiagnosticPluginManager(this);
    m_diagPluginManager->setPluginDir(resolveExistingDirByCandidates({
        QStringLiteral("FaultDiagnostic/Diagnostics/Plugins")
    }));
    m_multiSignalDiagnosticPlugin = new MultiTpsDiagnosticPlugin(this);
    m_capacitorDiagnosticPlugin = new CapacitorDiagnosticPlugin(this);
    m_inductorDiagnosticPlugin = new InductorDiagnosticPlugin(this);
    m_resistorDiagnosticPlugin = new ResistorDiagnosticPlugin(this);
    m_typicalDiagnosticPlugin = new TypicalDiagnosticPlugin(this);
    m_transistorDiagnosticPlugin = new TransistorDiagnosticPlugin(this);
    m_diagPluginManager->addBuiltin(m_multiSignalDiagnosticPlugin);
    m_diagPluginManager->addBuiltin(m_capacitorDiagnosticPlugin);
    m_diagPluginManager->addBuiltin(m_inductorDiagnosticPlugin);
    m_diagPluginManager->addBuiltin(m_resistorDiagnosticPlugin);
    m_diagPluginManager->addBuiltin(m_typicalDiagnosticPlugin);
    m_diagPluginManager->addBuiltin(m_transistorDiagnosticPlugin);
    m_diagPluginManager->loadAll(nullptr);
    m_diagnosticDispatcher.setPluginManager(m_diagPluginManager);
    m_taskContextManager = new TestTaskContextManager(this);

    if (m_list) {
        connect(m_list, &QListWidget::currentRowChanged, this, &FaultDiagnostic::onComponentSelectionChanged);
    }
}

FaultDiagnostic::~FaultDiagnostic()
{
    if (m_runtime && m_runtime->rts()) {
        QString err;
        m_runtime->rts()->abortRun(&err);
    }
    IRCameraStation::instance()->stop();
    delete ui;
}

SystemRuntimeOrchestration *FaultDiagnostic::runtime() const
{
    return m_runtime;
}

void FaultDiagnostic::setRuntimeThreadManager(JYThreadManager *manager)
{
    if (!manager || manager == m_threadManager) {
        return;
    }

    if (m_ownsThreadManager && m_threadManager) {
        m_threadManager->deleteLater();
    }

    m_threadManager = manager;
    m_ownsThreadManager = false;
    m_devicesCreated = false;
    if (m_runtime) {
        m_runtime->setThreadManager(m_threadManager);
    }
    bindThreadManagerSignals();
}

void FaultDiagnostic::setRuntimeCameraStation(CameraStation *station)
{
    if (m_runtime) {
        m_runtime->setCameraStation(station);
    }
}

void FaultDiagnostic::bindThreadManagerSignals()
{
    if (!m_threadManager || !m_threadManager->pipeline()) {
        return;
    }

    connect(m_threadManager->pipeline(), &JYDataPipeline::packetIngested, this,
            [](JYDeviceKind kind, int channelCount, int dataSize, qint64 ts) {
                static QElapsedTimer timer;
                static qint64 lastMs = -1;
                if (!timer.isValid()) {
                    timer.start();
                    lastMs = 0;
                }
                const qint64 nowMs = timer.elapsed();
                if (nowMs - lastMs < 1000) {
                    return;
                }
                lastMs = nowMs;
                Logger::log(QStringLiteral("FaultDiag packet: kind=%1 ch=%2 size=%3 ts=%4")
                                .arg(static_cast<int>(kind))
                                .arg(channelCount)
                                .arg(dataSize)
                                .arg(ts),
                            Logger::Level::Info);
            },
            Qt::UniqueConnection);
    connect(m_threadManager->pipeline(), &JYDataPipeline::packetRejected, this,
            [](JYDeviceKind kind, const QString &reason) {
                Logger::log(QStringLiteral("FaultDiag packet rejected: kind=%1 reason=%2")
                                .arg(static_cast<int>(kind))
                                .arg(reason),
                            Logger::Level::Warn);
            },
            Qt::UniqueConnection);
}

void FaultDiagnostic::setComponents(const QVector<ComponentViewData> &items)
{
    m_components = items;
    m_indexById.clear();

    if (!m_list) {
        return;
    }

    m_list->clear();
    for (int i = 0; i < m_components.size(); ++i) {
        const auto &item = m_components[i];
        const QString taskTag = item.taskId.isEmpty() ? QString() : QStringLiteral(" [Task:%1]").arg(item.taskId);
        const QString label = (item.name.isEmpty() ? item.id : item.name) + taskTag;
        m_list->addItem(label.isEmpty() ? QStringLiteral("未命名组件") : label);
        if (!item.id.isEmpty()) {
            m_indexById.insert(item.id, i);
        }
    }

    if (!m_components.isEmpty()) {
        m_list->setCurrentRow(0);
        setCurrentIndex(0);
    } else {
        refreshThermal(ComponentViewData{});
        refreshPlot(ComponentViewData{});
        refreshReport(ComponentViewData{});
    }
}

void FaultDiagnostic::setGuidanceLabels(const QList<CompLabel> &labels)
{
    m_guidanceAllLabels = labels;
    m_guidanceLabelsByRef.clear();
    for (const CompLabel &label : labels) {
        const QString ref = label.position_number.trimmed().isEmpty()
            ? QString::number(label.id)
            : label.position_number.trimmed();
        if (ref.isEmpty()) {
            continue;
        }
        m_guidanceLabelsByRef.insert(ref, label);
    }
}

void FaultDiagnostic::setGuidanceImage(const QImage &image)
{
    m_guidanceImage = image;
}

void FaultDiagnostic::updateComponent(const ComponentViewData &item)
{
    if (!m_list) {
        return;
    }

    const int existing = item.id.isEmpty() ? -1 : m_indexById.value(item.id, -1);
    if (existing >= 0 && existing < m_components.size()) {
        m_components[existing] = item;
        const QString taskTag = item.taskId.isEmpty() ? QString() : QStringLiteral(" [Task:%1]").arg(item.taskId);
        const QString label = (item.name.isEmpty() ? item.id : item.name) + taskTag;
        if (auto *w = m_list->item(existing)) {
            w->setText(label.isEmpty() ? QStringLiteral("未命名组件") : label);
        }
        if (m_list->currentRow() == existing) {
            setCurrentIndex(existing);
        }
        return;
    }

    m_components.push_back(item);
    const int index = m_components.size() - 1;
    const QString taskTag = item.taskId.isEmpty() ? QString() : QStringLiteral(" [Task:%1]").arg(item.taskId);
    const QString label = (item.name.isEmpty() ? item.id : item.name) + taskTag;
    m_list->addItem(label.isEmpty() ? QStringLiteral("未命名组件") : label);
    if (!item.id.isEmpty()) {
        m_indexById.insert(item.id, index);
    }
    if (m_list->currentRow() < 0) {
        m_list->setCurrentRow(index);
        setCurrentIndex(index);
    }
}

void FaultDiagnostic::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        applyThemeQss();
    }
}

void FaultDiagnostic::onComponentSelectionChanged()
{
    if (!m_list) {
        return;
    }
    setCurrentIndex(m_list->currentRow());
}

void FaultDiagnostic::onTestClicked()
{
    startTest();
}

void FaultDiagnostic::selectComponentById(const QString &id)
{
    if (!m_list || id.isEmpty()) {
        return;
    }
    const int index = m_indexById.value(id, -1);
    if (index < 0 || index >= m_components.size()) {
        return;
    }
    m_list->setCurrentRow(index);
    setCurrentIndex(index);
}

void FaultDiagnostic::startTest()
{
    const int currentIndex = m_list ? m_list->currentRow() : -1;
    QString componentRef;
    if (currentIndex >= 0 && currentIndex < m_components.size()) {
        componentRef = m_components[currentIndex].id;
    }
    if (componentRef.isEmpty()) {
        componentRef = QStringLiteral("R1");
    }
    runTest(componentRef, QStringLiteral("tps.multi.signal"), {});
}

void FaultDiagnostic::startTestWith(const QString &componentRef,
                                    const QString &pluginId,
                                    const QMap<QString, QVariant> &parameters)
{
    TestTask task;
    task.componentRef = componentRef;
    task.pluginId = pluginId;
    task.parameters = parameters;
    startBatchTest({task});
}

void FaultDiagnostic::startBatchTest(const QVector<TestTask> &tasks)
{
    if (tasks.isEmpty()) {
        startTest();
        return;
    }

    struct TaskRuntime {
        TestTask task;
        QString taskId;
        TPSPluginInterface *plugin = nullptr;
        TPSPluginRequirement requirement;
        QVector<TPSPortBinding> bindings;
        QMap<QString, QVariant> settings;
        TPSDevicePlan plan;
        QString signalPrefix;
    };

    QVector<TaskRuntime> runtimeTasks;
    runtimeTasks.reserve(tasks.size());
    const QString runId = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString boardId = QStringLiteral("board");
    const QString paramSnapshotDbPath = resolveParamSnapshotDbPath();
    Logger::log(QStringLiteral("FaultDiag batch start: runId=%1 taskCount=%2")
                    .arg(runId)
                    .arg(tasks.size()),
                Logger::Level::Info);
    const auto closePreparedTasks = [this, &runtimeTasks](const QString &status, const QString &reason = QString()) {
        Logger::log(QStringLiteral("FaultDiag close prepared tasks: status=%1 reason=%2 count=%3")
                        .arg(status)
                        .arg(reason)
                        .arg(runtimeTasks.size()),
                    Logger::Level::Info);
        if (!m_taskContextManager) {
            return;
        }
        for (const auto &prepared : runtimeTasks) {
            if (!prepared.taskId.isEmpty()) {
                m_taskContextManager->closeTask(prepared.taskId, status, reason);
            }
        }
    };

    for (int i = 0; i < tasks.size(); ++i) {
        const auto &task = tasks[i];
        const QString pluginId = task.pluginId.isEmpty() ? QStringLiteral("tps.multi.signal") : task.pluginId;
        TPSPluginInterface *plugin = m_tpsManager ? m_tpsManager->plugin(pluginId) : nullptr;
        if (!plugin) {
            Logger::log(QStringLiteral("FaultDiag plugin missing: component=%1 plugin=%2")
                            .arg(task.componentRef)
                            .arg(pluginId),
                        Logger::Level::Error);
            QMessageBox::warning(this, tr("测试"), tr("未找到测试插件。"));
            closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("PluginNotFound"));
            return;
        }

        TaskRuntime runtime;
        runtime.task = task;
        runtime.plugin = plugin;
        runtime.requirement = plugin->requirements();
        if (m_taskContextManager) {
            const QString componentRef = task.componentRef.trimmed().isEmpty()
                ? QStringLiteral("item_%1").arg(i + 1)
                : task.componentRef.trimmed();
            runtime.taskId = m_taskContextManager->createTask(runId, boardId, componentRef, pluginId);
        }
        if (runtime.taskId.isEmpty()) {
            runtime.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        Logger::log(QStringLiteral("FaultDiag task prepared: taskId=%1 component=%2 plugin=%3")
                        .arg(runtime.taskId)
                        .arg(task.componentRef)
                        .arg(pluginId),
                    Logger::Level::Info);

        runtime.settings = task.parameters;
        const int componentId = componentIdFromRef(task.componentRef);
        const QMap<QString, QVariant> dbParams = loadTaskParamsFromDb(paramSnapshotDbPath,
                                                                       boardId,
                                                                       componentId,
                                                                       pluginId);
        if (!dbParams.isEmpty()) {
            runtime.settings = dbParams;
            for (auto it = task.parameters.begin(); it != task.parameters.end(); ++it) {
                runtime.settings.insert(it.key(), it.value());
            }
        }
        for (const auto &def : runtime.requirement.parameters) {
            if (!runtime.settings.contains(def.key) || !runtime.settings.value(def.key).isValid()) {
                runtime.settings.insert(def.key, def.defaultValue);
            }
        }

        runtimeTasks.push_back(runtime);
    }

    QVector<BatchParamReviewItem> reviewItems;
    reviewItems.reserve(runtimeTasks.size());
    for (const auto &runtime : runtimeTasks) {
        BatchParamReviewItem item;
        item.componentRef = runtime.task.componentRef;
        item.pluginId = runtime.task.pluginId.isEmpty() ? QStringLiteral("tps.multi.signal") : runtime.task.pluginId;
        item.definitions = runtime.requirement.parameters;
        item.values = runtime.settings;
        reviewItems.push_back(item);
    }

    BatchParamReviewDialog reviewDialog(this);
    reviewDialog.setItems(reviewItems);
    if (reviewDialog.exec() != QDialog::Accepted) {
        Logger::log(QStringLiteral("FaultDiag batch canceled at parameter review: runId=%1")
                        .arg(runId),
                    Logger::Level::Info);
        closePreparedTasks(QStringLiteral("Canceled"), QStringLiteral("ParamReviewRejected"));
        return;
    }
    Logger::log(QStringLiteral("FaultDiag parameter review accepted: runId=%1")
                    .arg(runId),
                Logger::Level::Info);

    const QVector<BatchParamReviewItem> reviewed = reviewDialog.reviewedItems();
    for (int i = 0; i < runtimeTasks.size() && i < reviewed.size(); ++i) {
        runtimeTasks[i].settings = reviewed[i].values;
        if (m_taskContextManager) {
            m_taskContextManager->setParamSnapshot(runtimeTasks[i].taskId,
                                                   runtimeTasks[i].requirement.parameters,
                                                   runtimeTasks[i].settings);
        }
    }

    DevicePortManager::AllocationState allocationState = DevicePortManager::defaultState();
    for (auto &runtime : runtimeTasks) {
        QString portError;
        if (!DevicePortManager::allocate(runtime.requirement.ports, &allocationState, &runtime.bindings, &portError)) {
            Logger::log(QStringLiteral("FaultDiag auto port allocation failed: taskId=%1 component=%2 error=%3")
                            .arg(runtime.taskId)
                            .arg(runtime.task.componentRef)
                            .arg(portError),
                        Logger::Level::Error);
            QMessageBox::warning(this, tr("测试"), tr("端口分配失败：%1").arg(portError));
            if (m_taskContextManager && !runtime.taskId.isEmpty()) {
                m_taskContextManager->closeTask(runtime.taskId, QStringLiteral("Failed"), QStringLiteral("PortAllocationFailed"));
            }
            closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("PortAllocationFailed"));
            return;
        }

        QString error;
        QMap<QString, QVariant> configureSettings = runtime.settings;
        configureSettings.insert(TPSRuntimeContext::allocatedBindingsKey(),
                     TPSRuntimeContext::encodeBindings(runtime.bindings));
        runtime.plugin->configure(configureSettings, &error);
        Logger::log(QStringLiteral("FaultDiag auto port allocation: taskId=%1 bindings=[%2]")
                        .arg(runtime.taskId)
                        .arg(summarizeBindingsForLog(runtime.bindings)),
                    Logger::Level::Info);

        if (!runtime.plugin->buildDevicePlan(runtime.bindings, runtime.settings, &runtime.plan, &error)) {
            Logger::log(QStringLiteral("FaultDiag build device plan failed: taskId=%1 error=%2")
                            .arg(runtime.taskId)
                            .arg(error),
                        Logger::Level::Error);
            QMessageBox::warning(this, tr("测试"), tr("设备配置生成失败：%1").arg(error));
            if (m_taskContextManager && !runtime.taskId.isEmpty()) {
                m_taskContextManager->closeTask(runtime.taskId, QStringLiteral("Failed"), QStringLiteral("BuildDevicePlanFailed"));
            }
            closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("BuildDevicePlanFailed"));
            return;
        }
        Logger::log(QStringLiteral("FaultDiag build device plan success: taskId=%1 bindCount=%2 requestCount=%3")
                        .arg(runtime.taskId)
                        .arg(runtime.plan.bindings.size())
                        .arg(runtime.plan.requests.size()),
                    Logger::Level::Info);
        if (m_taskContextManager) {
            m_taskContextManager->setPortAllocation(runtime.taskId, runtime.bindings);

            QJsonArray wiringGuide;
            const QStringList effectiveWiringSteps = !runtime.plan.guide.wiringSteps.isEmpty()
                ? runtime.plan.guide.wiringSteps
                : runtime.plan.wiringSteps;
            if (!effectiveWiringSteps.isEmpty()) {
                int step = 1;
                for (const QString &instruction : effectiveWiringSteps) {
                    QJsonObject item;
                    item.insert(QStringLiteral("step"), step++);
                    item.insert(QStringLiteral("instruction"), instruction);
                    wiringGuide.append(item);
                }
            } else {
                for (const auto &binding : runtime.bindings) {
                    QJsonObject item;
                    item.insert(QStringLiteral("identifier"), binding.identifier);
                    item.insert(QStringLiteral("type"), static_cast<int>(binding.type));
                    item.insert(QStringLiteral("deviceKind"), static_cast<int>(binding.deviceKind));
                    item.insert(QStringLiteral("channel"), binding.channel);
                    item.insert(QStringLiteral("resourceId"), binding.resourceId);
                    wiringGuide.append(item);
                }
            }
            m_taskContextManager->setWiringGuide(runtime.taskId, wiringGuide);

            QStringList roiBindings;
            const QStringList effectiveRoiSteps = !runtime.plan.guide.roiSteps.isEmpty()
                ? runtime.plan.guide.roiSteps
                : QStringList{runtime.plan.temperatureGuide};
            for (const QString &roi : effectiveRoiSteps) {
                const QString text = roi.trimmed();
                if (!text.isEmpty()) {
                    roiBindings.push_back(text);
                }
            }
            const QString anchor = runtime.settings.value(QStringLiteral("temperatureAnchor")).toString().trimmed();
            if (!anchor.isEmpty()) {
                roiBindings.push_back(anchor);
            }
            m_taskContextManager->setRoiBindings(runtime.taskId, roiBindings);
            m_taskContextManager->setDeviceConfig(runtime.taskId,
                                                  runtime.plan.cfg532x,
                                                  runtime.plan.cfg5711,
                                                  runtime.plan.cfg8902);
        }

        const QString prefix = runtime.taskId;
        runtime.signalPrefix = prefix;
    }

    QVector<TaskPortAllocationReviewItem> allocationItems;
    allocationItems.reserve(runtimeTasks.size());
    for (const auto &runtime : runtimeTasks) {
        TaskPortAllocationReviewItem item;
        item.componentRef = runtime.task.componentRef;
        item.bindings = runtime.bindings;
        allocationItems.push_back(item);
    }
    PortAllocationReviewDialog allocationDialog(this);
    allocationDialog.setItems(allocationItems);
    if (allocationDialog.exec() != QDialog::Accepted) {
        Logger::log(QStringLiteral("FaultDiag batch canceled at port review: runId=%1")
                        .arg(runId),
                    Logger::Level::Info);
        closePreparedTasks(QStringLiteral("Canceled"), QStringLiteral("PortAllocationRejected"));
        return;
    }
    Logger::log(QStringLiteral("FaultDiag port review accepted: runId=%1")
                    .arg(runId),
                Logger::Level::Info);

    const QVector<TaskPortAllocationReviewItem> reviewedAllocations = allocationDialog.reviewedItems();
    if (reviewedAllocations.size() != runtimeTasks.size()) {
        Logger::log(QStringLiteral("FaultDiag reviewed allocation size mismatch: runId=%1 expected=%2 actual=%3")
                        .arg(runId)
                        .arg(runtimeTasks.size())
                        .arg(reviewedAllocations.size()),
                    Logger::Level::Error);
        QMessageBox::warning(this, tr("测试"), tr("端口分配结果数量异常，请重新分配。"));
        closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("PortAllocationResultInvalid"));
        return;
    }

    for (int i = 0; i < runtimeTasks.size(); ++i) {
        auto &runtime = runtimeTasks[i];
        runtime.bindings = reviewedAllocations[i].bindings;

        QString pluginError;
        QMap<QString, QVariant> configureSettings = runtime.settings;
        configureSettings.insert(TPSRuntimeContext::allocatedBindingsKey(),
                                 TPSRuntimeContext::encodeBindings(runtime.bindings));
        if (!runtime.plugin->configure(configureSettings, &pluginError)) {
            Logger::log(QStringLiteral("FaultDiag plugin configure failed after review: taskId=%1 error=%2")
                            .arg(runtime.taskId)
                            .arg(pluginError),
                        Logger::Level::Error);
            QMessageBox::warning(this, tr("测试"), tr("端口配置失败：%1").arg(pluginError));
            closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("PluginConfigureFailed"));
            return;
        }

        if (!runtime.plugin->buildDevicePlan(runtime.bindings, runtime.settings, &runtime.plan, &pluginError)) {
            Logger::log(QStringLiteral("FaultDiag build plan failed after review: taskId=%1 error=%2")
                            .arg(runtime.taskId)
                            .arg(pluginError),
                        Logger::Level::Error);
            QMessageBox::warning(this, tr("测试"), tr("端口分配后的设备计划生成失败：%1").arg(pluginError));
            closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("BuildDevicePlanAfterAllocationFailed"));
            return;
        }
        Logger::log(QStringLiteral("FaultDiag reviewed allocation applied: taskId=%1 bindings=[%2]")
                        .arg(runtime.taskId)
                        .arg(summarizeBindingsForLog(runtime.bindings)),
                    Logger::Level::Info);

        if (m_taskContextManager) {
            m_taskContextManager->setPortAllocation(runtime.taskId, runtime.bindings);

            QJsonArray wiringGuide;
            const QStringList effectiveWiringSteps = !runtime.plan.guide.wiringSteps.isEmpty()
                ? runtime.plan.guide.wiringSteps
                : runtime.plan.wiringSteps;
            if (!effectiveWiringSteps.isEmpty()) {
                int step = 1;
                for (const QString &instruction : effectiveWiringSteps) {
                    QJsonObject item;
                    item.insert(QStringLiteral("step"), step++);
                    item.insert(QStringLiteral("instruction"), instruction);
                    wiringGuide.append(item);
                }
            }
            m_taskContextManager->setWiringGuide(runtime.taskId, wiringGuide);

            QStringList roiBindings;
            const QStringList effectiveRoiSteps = !runtime.plan.guide.roiSteps.isEmpty()
                ? runtime.plan.guide.roiSteps
                : QStringList{runtime.plan.temperatureGuide};
            for (const QString &roi : effectiveRoiSteps) {
                const QString text = roi.trimmed();
                if (!text.isEmpty()) {
                    roiBindings.push_back(text);
                }
            }
            const QString anchor = runtime.settings.value(QStringLiteral("temperatureAnchor")).toString().trimmed();
            if (!anchor.isEmpty()) {
                roiBindings.push_back(anchor);
            }
            m_taskContextManager->setRoiBindings(runtime.taskId, roiBindings);
            m_taskContextManager->setDeviceConfig(runtime.taskId,
                                                  runtime.plan.cfg532x,
                                                  runtime.plan.cfg5711,
                                                  runtime.plan.cfg8902);
        }
    }

    QVector<TaskWiringGuideItem> guideItems;
    guideItems.reserve(runtimeTasks.size());
    QList<CompLabel> guideLabels = m_guidanceAllLabels;
    QSet<QString> existingRefs;
    existingRefs.reserve(guideLabels.size());
    for (const CompLabel &label : guideLabels) {
        const QString ref = label.position_number.trimmed().isEmpty()
            ? QString::number(label.id)
            : label.position_number.trimmed();
        if (!ref.isEmpty()) {
            existingRefs.insert(ref);
        }
    }

    for (const auto &runtime : runtimeTasks) {
        TaskWiringGuideItem item;
        item.componentRef = runtime.task.componentRef;
        item.wiringSteps = !runtime.plan.guide.wiringSteps.isEmpty()
            ? runtime.plan.guide.wiringSteps
            : runtime.plan.wiringSteps;
        item.roiSteps = !runtime.plan.guide.roiSteps.isEmpty()
            ? runtime.plan.guide.roiSteps
            : QStringList{runtime.plan.temperatureGuide};
        if (item.roiSteps.size() == 1 && item.roiSteps.first().trimmed().isEmpty()) {
            item.roiSteps.clear();
        }
        item.temperatureGuide = item.roiSteps.join(QStringLiteral("；"));
        item.wiringFocusTargets = resolveFocusTargets(runtime.plan, runtime.settings, true);
        item.roiFocusTargets = resolveFocusTargets(runtime.plan, runtime.settings, false);
        guideItems.push_back(item);

        const QString ref = runtime.task.componentRef.trimmed().isEmpty()
            ? QStringLiteral("item")
            : runtime.task.componentRef.trimmed();

        if (existingRefs.contains(ref)) {
            continue;
        }

        const auto foundLabel = m_guidanceLabelsByRef.constFind(ref);
        if (foundLabel != m_guidanceLabelsByRef.constEnd()) {
            guideLabels.push_back(foundLabel.value());
            existingRefs.insert(ref);
        } else {
            int labelId = componentIdFromRef(ref);
            if (labelId <= 0) {
                labelId = guideLabels.size() + 1;
            }
            const int index = guideLabels.size();
            const int col = index % 4;
            const int row = index / 4;
            const double x = 40.0 + col * 180.0;
            const double y = 40.0 + row * 140.0;
            guideLabels.push_back(CompLabel(labelId,
                                            x,
                                            y,
                                            140.0,
                                            90.0,
                                            0,
                                            1.0,
                                            ref,
                                            ref,
                                            QByteArray()));
            existingRefs.insert(ref);
        }
    }
    WiringGuideDialog guideDialog(this);
    guideDialog.setItems(guideItems);
    if (!m_guidanceImage.isNull()) {
        guideDialog.setGuidanceImage(m_guidanceImage);
    }
    guideDialog.setGuidanceLabels(guideLabels, true);
    if (guideDialog.exec() != QDialog::Accepted) {
        Logger::log(QStringLiteral("FaultDiag batch canceled at wiring guide: runId=%1")
                        .arg(runId),
                    Logger::Level::Info);
        closePreparedTasks(QStringLiteral("Canceled"), QStringLiteral("WiringGuideRejected"));
        return;
    }
    Logger::log(QStringLiteral("FaultDiag wiring guide accepted: runId=%1")
                    .arg(runId),
                Logger::Level::Info);

    const QVector<QRectF> selectedRoiRects = guideDialog.selectedRois();
    const bool hasSelectedRoi = guideDialog.hasSelectedRoi() && !selectedRoiRects.isEmpty();
    const QRectF selectedRoiRect = hasSelectedRoi ? selectedRoiRects.first() : QRectF();
    Logger::log(QStringLiteral("FaultDiag ROI selection: runId=%1 hasRoi=%2 roiCount=%3")
                    .arg(runId)
                    .arg(hasSelectedRoi ? 1 : 0)
                    .arg(selectedRoiRects.size()),
                Logger::Level::Info);

    if (m_taskContextManager && hasSelectedRoi) {
        QStringList roiSelectionSummary;
        roiSelectionSummary.push_back(QStringLiteral("roi.count=%1").arg(selectedRoiRects.size()));
        for (int i = 0; i < selectedRoiRects.size(); ++i) {
            const QRectF &rect = selectedRoiRects.at(i);
            roiSelectionSummary.push_back(
                QStringLiteral("roi[%1]=x:%2,y:%3,w:%4,h:%5")
                    .arg(i)
                    .arg(rect.x(), 0, 'f', 1)
                    .arg(rect.y(), 0, 'f', 1)
                    .arg(rect.width(), 0, 'f', 1)
                    .arg(rect.height(), 0, 'f', 1));
        }

        for (const auto &task : runtimeTasks) {
            QStringList roiBindings = task.settings.value(QStringLiteral("temperatureAnchor")).toString().trimmed().isEmpty()
                ? QStringList{}
                : QStringList{task.settings.value(QStringLiteral("temperatureAnchor")).toString().trimmed()};
            roiBindings.append(roiSelectionSummary);
            m_taskContextManager->setRoiBindings(task.taskId, roiBindings);
        }
    }

    auto hasDevice = [&](JYDeviceKind kind) {
        for (const auto &task : runtimeTasks) {
            for (const auto &binding : task.plan.bindings) {
                if (binding.deviceKind == kind) {
                    return true;
                }
            }
        }
        return false;
    };

    if (m_threadManager && !m_devicesCreated) {
        if (hasDevice(JYDeviceKind::PXIe5711)) {
            Logger::log(QStringLiteral("FaultDiag create worker: kind=%1")
                            .arg(deviceKindNameForLog(JYDeviceKind::PXIe5711)),
                        Logger::Level::Info);
            m_threadManager->create5711Worker();
        }
        if (hasDevice(JYDeviceKind::PXIe5322)) {
            Logger::log(QStringLiteral("FaultDiag create worker: kind=%1")
                            .arg(deviceKindNameForLog(JYDeviceKind::PXIe5322)),
                        Logger::Level::Info);
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5322);
        }
        if (hasDevice(JYDeviceKind::PXIe5323)) {
            Logger::log(QStringLiteral("FaultDiag create worker: kind=%1")
                            .arg(deviceKindNameForLog(JYDeviceKind::PXIe5323)),
                        Logger::Level::Info);
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5323);
        }
        if (hasDevice(JYDeviceKind::PXIe8902)) {
            Logger::log(QStringLiteral("FaultDiag create worker: kind=%1")
                            .arg(deviceKindNameForLog(JYDeviceKind::PXIe8902)),
                        Logger::Level::Info);
            m_threadManager->create8902Worker();
        }
        m_devicesCreated = true;
    }

    JYDeviceConfig merged532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    JYDeviceConfig merged5711 = build5711InitConfig();
    JYDeviceConfig merged8902 = build8902InitConfig();
    merged532x.cfg532x.channelCount = 0;
    merged532x.cfg532x.slotNumber = -1;
    merged5711.cfg5711.channelCount = 0;
    merged5711.cfg5711.slotNumber = -1;
    merged5711.cfg5711.enabledChannels.clear();
    merged5711.cfg5711.waveforms.clear();
    merged8902.cfg8902.sampleCount = 0;
    merged8902.cfg8902.slotNumber = -1;

    QMap<int, JY5711WaveformConfig> waveformByChannel;
    QSet<int> enabledChannels;
    int maxInputChannel = -1;
    int maxOutputChannel = -1;
    int maxInputChannel5322 = -1;
    int maxInputChannel5323 = -1;

    for (const auto &task : runtimeTasks) {
        for (const auto &binding : task.plan.bindings) {
            if (binding.deviceKind == JYDeviceKind::PXIe5322 || binding.deviceKind == JYDeviceKind::PXIe5323) {
                maxInputChannel = qMax(maxInputChannel, binding.channel);
                if (binding.deviceKind == JYDeviceKind::PXIe5322) {
                    maxInputChannel5322 = qMax(maxInputChannel5322, binding.channel);
                } else {
                    maxInputChannel5323 = qMax(maxInputChannel5323, binding.channel);
                }
            } else if (binding.deviceKind == JYDeviceKind::PXIe5711) {
                maxOutputChannel = qMax(maxOutputChannel, binding.channel);
            }
        }

        const auto &src532x = task.plan.cfg532x.cfg532x;
        if (src532x.channelCount > 0 && src532x.slotNumber >= 0) {
            merged532x.cfg532x.slotNumber = src532x.slotNumber;
            merged532x.cfg532x.channelCount = qMax(merged532x.cfg532x.channelCount, src532x.channelCount);
            merged532x.cfg532x.sampleRate = qMax(merged532x.cfg532x.sampleRate, src532x.sampleRate);
            merged532x.cfg532x.samplesPerRead = qMax(merged532x.cfg532x.samplesPerRead, src532x.samplesPerRead);
            merged532x.cfg532x.timeoutMs = qMax(merged532x.cfg532x.timeoutMs, src532x.timeoutMs);
            merged532x.cfg532x.lowRange = qMin(merged532x.cfg532x.lowRange, src532x.lowRange);
            merged532x.cfg532x.highRange = qMax(merged532x.cfg532x.highRange, src532x.highRange);
            merged532x.cfg532x.bandwidth = qMax(merged532x.cfg532x.bandwidth, src532x.bandwidth);
        }

        const auto &src5711 = task.plan.cfg5711.cfg5711;
        if (!src5711.waveforms.isEmpty() || !src5711.enabledChannels.isEmpty()) {
            merged5711.cfg5711.slotNumber = src5711.slotNumber;
            for (const auto &ch : src5711.enabledChannels) {
                enabledChannels.insert(ch);
            }
            for (const auto &wf : src5711.waveforms) {
                waveformByChannel.insert(wf.channel, wf);
                enabledChannels.insert(wf.channel);
            }
            merged5711.cfg5711.sampleRate = qMax(merged5711.cfg5711.sampleRate, src5711.sampleRate);
            merged5711.cfg5711.lowRange = qMin(merged5711.cfg5711.lowRange, src5711.lowRange);
            merged5711.cfg5711.highRange = qMax(merged5711.cfg5711.highRange, src5711.highRange);
        }

        const auto &src8902 = task.plan.cfg8902.cfg8902;
        if (src8902.sampleCount > 0 && src8902.slotNumber >= 0) {
            merged8902.cfg8902.slotNumber = src8902.slotNumber;
            merged8902.cfg8902.sampleCount = qMax(merged8902.cfg8902.sampleCount, src8902.sampleCount);
            merged8902.cfg8902.timeoutMs = qMax(merged8902.cfg8902.timeoutMs, src8902.timeoutMs);
            merged8902.cfg8902.apertureTime = qMax(merged8902.cfg8902.apertureTime, src8902.apertureTime);
            merged8902.cfg8902.triggerDelay = qMax(merged8902.cfg8902.triggerDelay, src8902.triggerDelay);
            merged8902.cfg8902.measurementFunction = src8902.measurementFunction;
            merged8902.cfg8902.range = src8902.range;
        }
    }

    QList<int> enabledList = enabledChannels.values();
    std::sort(enabledList.begin(), enabledList.end());
    merged5711.cfg5711.enabledChannels = enabledList;
    merged5711.cfg5711.waveforms = waveformByChannel.values().toVector();
    if (!enabledList.isEmpty() || maxOutputChannel >= 0) {
        merged5711.cfg5711.channelCount = qMax(merged5711.cfg5711.channelCount, maxOutputChannel + 1);
    } else {
        merged5711.cfg5711.channelCount = 0;
        merged5711.cfg5711.slotNumber = -1;
        merged5711.cfg5711.waveforms.clear();
    }

    if (maxInputChannel >= 0) {
        const int required5322 = (maxInputChannel5322 >= 0) ? (maxInputChannel5322 + 1) : 0;
        const int required5323 = (maxInputChannel5323 >= 0) ? (maxInputChannel5323 + 1) : 0;
        merged532x.cfg532x.channelCount = qMax(merged532x.cfg532x.channelCount, qMax(required5322, required5323));
        if (merged532x.cfg532x.slotNumber < 0) {
            merged532x.cfg532x.slotNumber = build532xInitConfig(JYDeviceKind::PXIe5322).cfg532x.slotNumber;
        }
        merged532x.cfg532x.samplesPerRead = 1000;
        merged532x.cfg532x.timeoutMs = -1;
    } else {
        merged532x.cfg532x.channelCount = 0;
        merged532x.cfg532x.slotNumber = -1;
    }
    if (merged8902.cfg8902.sampleCount <= 0) {
        merged8902.cfg8902.slotNumber = -1;
    }

    QVector<ResourceMapping> mappings;
    QVector<SignalRequest> signalRequests;
    QMap<JYDeviceKind, int> targetSamplesByKind;
    int fallbackDurationMs = 0;
    for (const auto &task : runtimeTasks) {
        const int durationMs = qMax(1, task.settings.value(QStringLiteral("captureDurationMs"), 1000).toInt());
        fallbackDurationMs = qMax(fallbackDurationMs, durationMs);
        const double durationSec = durationMs / 1000.0;
        const auto updateTarget = [&](JYDeviceKind kind, double sampleRateHz) {
            const int target = qMax(1, static_cast<int>(qCeil(durationSec * sampleRateHz)));
            const int current = targetSamplesByKind.value(kind, 0);
            if (target > current) {
                targetSamplesByKind.insert(kind, target);
            }
        };

        for (const auto &binding : task.plan.bindings) {
            ResourceMapping mapping;
            mapping.signalType = QStringLiteral("%1:%2").arg(task.signalPrefix, binding.identifier);
            mapping.binding.kind = binding.deviceKind;
            mapping.binding.channel = binding.channel;
            mapping.binding.resourceId = binding.resourceId;
            mappings.push_back(mapping);

            if (binding.type == TPSPortType::VoltageInput) {
                updateTarget(JYDeviceKind::PXIe5322, 1000000.0);
            } else if (binding.type == TPSPortType::CurrentInput) {
                updateTarget(JYDeviceKind::PXIe5323, 200000.0);
            } else if (binding.type == TPSPortType::DmmChannel) {
                updateTarget(JYDeviceKind::PXIe8902, 60.0);
            }
        }

        for (const auto &req : task.plan.requests) {
            SignalRequest request;
            request.id = QStringLiteral("%1:%2").arg(task.signalPrefix, req.id);
            request.type = QStringLiteral("%1:%2").arg(task.signalPrefix, req.signalType);
            request.value = req.value;
            request.unit = req.unit;
            signalRequests.push_back(request);
        }
    }

    if (m_runtime && m_runtime->rms()) {
        m_runtime->rms()->setMappings(mappings);
    }
    if (m_threadManager && m_threadManager->pipeline()) {
        m_threadManager->pipeline()->setExpectedKinds(QSet<JYDeviceKind>{});
        JYDataAligner::Settings alignSettings;
        alignSettings.windowMs = 50;
        alignSettings.maxAgeMs = 1000;
        m_threadManager->pipeline()->setAlignSettings(alignSettings);
    }

    bool started = true;
    if (m_runtime && m_runtime->rts()) {
        QString runError;
        started = m_runtime->rts()->startRun(runId,
                   boardId,
                   signalRequests,
                   merged532x,
                   merged5711,
                   merged8902,
                   2000,
                   &runError);
        if (!runError.isEmpty()) {
            Logger::log(QStringLiteral("FaultDiag runtime start warning: runId=%1 error=%2")
                            .arg(runId)
                            .arg(runError),
                        Logger::Level::Warn);
            QMessageBox::information(this, tr("测试"), tr("运行时启动失败：%1").arg(runError));
        }
    }

    if (!started) {
        Logger::log(QStringLiteral("FaultDiag runtime start failed: runId=%1")
                        .arg(runId),
                    Logger::Level::Error);
        closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("RuntimeStartFailed"));
        return;
    }
    Logger::log(QStringLiteral("FaultDiag runtime started: runId=%1 taskCount=%2")
                    .arg(runId)
                    .arg(runtimeTasks.size()),
                Logger::Level::Info);

    struct TempCapture {
        IRCameraStation::Frame frame;
        IRCameraStation::Frame alarmFrame;
        IRCameraStation::PointResult point;
        QMap<QString, IRCameraStation::BoxResult> boxes;
        bool hasFrame = false;
        bool hasAlarmFrame = false;
        bool hasPoint = false;
        bool hasBox = false;
        bool hasBatch = false;
        bool alarmTriggered = false;
        JYAlignedBatch lastBatch;
        qint64 t0ms = -1;
        QVector<double> tempTimes;
        QVector<double> tempValues;
        QVector<double> x5322;
        QVector<double> y5322;
        QVector<double> x5323;
        QVector<double> y5323;
        QVector<double> x8902;
        QVector<double> y8902;
        QMap<JYDeviceKind, int> sampleCounts;
        QMap<JYDeviceKind, int> targetSamples;
        double alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
        double alarmMaxTempC = std::numeric_limits<double>::quiet_NaN();
        QString alarmSourceId;
        QRectF alarmRect;
        QPointF alarmPoint;
        bool hasAlarmRect = false;
        bool hasAlarmPoint = false;
        bool finished = false;
        CapturedDataManager dataManager;
    };
    auto capture = QSharedPointer<TempCapture>::create();
    capture->targetSamples = targetSamplesByKind;
    capture->dataManager.reset();
    for (const auto &task : runtimeTasks) {
        bool ok = false;
        double threshold = task.settings.value(QStringLiteral("temperatureWarnC")).toDouble(&ok);
        if (!ok) {
            threshold = task.settings.value(QStringLiteral("temperatureTripC")).toDouble(&ok);
        }
        if (!ok) {
            continue;
        }
        if (std::isnan(capture->alarmThresholdC) || threshold < capture->alarmThresholdC) {
            capture->alarmThresholdC = threshold;
        }
    }
    const bool useCaptureManager = (m_threadManager && m_threadManager->pipeline()
                                    && !targetSamplesByKind.isEmpty());
    IRCameraStation *station = IRCameraStation::instance();
    if (station) {
        station->start();
        const QString tag = QStringLiteral("resistance_test");
        station->clearSubscriptions(tag);

        IRCameraStation::Frame latest;
        QPointF pointPos(10.0, 10.0);
        QRectF boxRect(5.0, 5.0, 20.0, 20.0);
        if (station->tryGetLatestFrame(&latest) && latest.irImage.isNull() == false) {
            const QSize size = latest.irImage.size();
            pointPos = QPointF(size.width() / 2.0, size.height() / 2.0);
            boxRect = QRectF(pointPos.x() - 15.0, pointPos.y() - 15.0, 30.0, 30.0);
        }
        if (hasSelectedRoi) {
            boxRect = selectedRoiRect;
            pointPos = boxRect.center();
        }

        station->setPointSubscription(tag, QStringLiteral("P1"), pointPos);
        if (hasSelectedRoi) {
            for (int i = 0; i < selectedRoiRects.size(); ++i) {
                station->setBoxSubscription(tag, QStringLiteral("B%1").arg(i + 1), selectedRoiRects.at(i));
            }
        } else {
            station->setBoxSubscription(tag, QStringLiteral("B1"), boxRect);
        }

        auto finishRun = QSharedPointer<std::function<void()>>::create();
        auto frameConn = connect(station, &IRCameraStation::frameReady, this,
                                 [capture](const IRCameraStation::Frame &frame) {
                                     capture->frame = frame;
                                     capture->hasFrame = true;
                                 });
        auto pointConn = connect(station, &IRCameraStation::pointTemperatureReady, this,
                                 [capture](const IRCameraStation::PointResult &result) {
                                     capture->point = result;
                                     capture->hasPoint = true;
                                     if (capture->t0ms < 0) {
                                         capture->t0ms = result.timestampMs;
                                     }
                                     const double tSec = (result.timestampMs - capture->t0ms) / 1000.0;
                                     capture->tempTimes.push_back(tSec);
                                     capture->tempValues.push_back(result.temp);
                                 });
        auto boxConn = connect(station, &IRCameraStation::boxTemperatureReady, this,
                               [capture, finishRun](const IRCameraStation::BoxResult &result) {
                                   capture->boxes.insert(result.id, result);
                                   capture->hasBox = true;
                                   if (std::isnan(capture->alarmMaxTempC) || result.maxTemp > capture->alarmMaxTempC) {
                                       capture->alarmMaxTempC = result.maxTemp;
                                   }
                                   if (capture->alarmTriggered
                                       || std::isnan(capture->alarmThresholdC)
                                       || result.maxTemp < capture->alarmThresholdC) {
                                       return;
                                   }
                                   capture->alarmTriggered = true;
                                   capture->alarmSourceId = result.id;
                                   capture->alarmRect = result.rect;
                                   capture->hasAlarmRect = true;
                                   if (capture->hasFrame) {
                                       capture->alarmFrame = capture->frame;
                                       capture->hasAlarmFrame = true;
                                       bool pointOk = false;
                                       capture->alarmPoint = maxTemperaturePointInRect(capture->frame, result.rect, &pointOk);
                                       capture->hasAlarmPoint = pointOk;
                                   }
                                   Logger::log(QStringLiteral("FaultDiag temperature alarm: threshold=%1 current=%2 roi=%3")
                                                   .arg(capture->alarmThresholdC, 0, 'f', 2)
                                                   .arg(result.maxTemp, 0, 'f', 2)
                                                   .arg(result.id),
                                               Logger::Level::Warn);
                                   if (*finishRun) {
                                       (*finishRun)();
                                   }
                               });

        QMetaObject::Connection batchConn;
        QMetaObject::Connection packetConn;
        *finishRun = [=]() {
            if (capture->finished) {
                return;
            }
            capture->finished = true;
            Logger::log(QStringLiteral("FaultDiag finish run begin: runId=%1")
                            .arg(runId),
                        Logger::Level::Info);
            station->clearSubscriptions(tag);
            disconnect(frameConn);
            disconnect(pointConn);
            disconnect(boxConn);
            if (batchConn) {
                disconnect(batchConn);
            }
            if (packetConn) {
                disconnect(packetConn);
            }

            if (m_runtime && m_runtime->rts()) {
                QString stopError;
                m_runtime->rts()->completeRun(2000, &stopError);
                if (!stopError.isEmpty()) {
                    Logger::log(QStringLiteral("FaultDiag pause run warning: runId=%1 error=%2")
                                    .arg(runId)
                                    .arg(stopError),
                                Logger::Level::Warn);
                }
            }

            auto buildSeriesFromPacket = [capture](const JYDataPacket &packet, QVector<double> &times, QVector<double> &values) {
                if (packet.channelCount <= 0) {
                    return;
                }
                const int available = packet.data.size() / packet.channelCount;
                int samples = packet.samplesPerChannel > 0 ? packet.samplesPerChannel : available;
                samples = qMin(samples, available);
                if (samples <= 0) {
                    return;
                }
                const double dtMs = packet.sampleRateHz > 0.0 ? (1000.0 / packet.sampleRateHz) : 0.0;
                for (int i = 0; i < samples; ++i) {
                    const int idx = i * packet.channelCount;
                    if (idx < 0 || idx >= packet.data.size()) {
                        continue;
                    }
                    const double tMs = i * dtMs;
                    times.push_back(tMs / 1000.0);
                    values.push_back(packet.data[idx]);
                }
            };

            const bool has5322 = capture->dataManager.buildSeries(JYDeviceKind::PXIe5322, 0, &capture->x5322, &capture->y5322);
            if (!has5322 && capture->lastBatch.packets.contains(JYDeviceKind::PXIe5322)) {
                capture->x5322.clear();
                capture->y5322.clear();
                buildSeriesFromPacket(capture->lastBatch.packets.value(JYDeviceKind::PXIe5322), capture->x5322, capture->y5322);
            }

            const bool has5323 = capture->dataManager.buildSeries(JYDeviceKind::PXIe5323, 0, &capture->x5323, &capture->y5323);
            if (!has5323 && capture->lastBatch.packets.contains(JYDeviceKind::PXIe5323)) {
                capture->x5323.clear();
                capture->y5323.clear();
                buildSeriesFromPacket(capture->lastBatch.packets.value(JYDeviceKind::PXIe5323), capture->x5323, capture->y5323);
            }

            const bool has8902 = capture->dataManager.buildSeries(JYDeviceKind::PXIe8902, 0, &capture->x8902, &capture->y8902);
            if (!has8902 && capture->lastBatch.packets.contains(JYDeviceKind::PXIe8902)) {
                capture->x8902.clear();
                capture->y8902.clear();
                buildSeriesFromPacket(capture->lastBatch.packets.value(JYDeviceKind::PXIe8902), capture->x8902, capture->y8902);
            }

            QString tempHtml;
            if (capture->hasPoint) {
                tempHtml += QStringLiteral("<p>点温度：%1 ℃</p>").arg(capture->point.temp, 0, 'f', 2);
            }
            if (capture->hasBox) {
                QStringList boxLines;
                boxLines.reserve(capture->boxes.size());
                for (auto it = capture->boxes.begin(); it != capture->boxes.end(); ++it) {
                    boxLines.push_back(
                        QStringLiteral("%1(min/avg/max)：%2 / %3 / %4 ℃")
                            .arg(it.key())
                            .arg(it.value().minTemp, 0, 'f', 2)
                            .arg(it.value().avgTemp, 0, 'f', 2)
                            .arg(it.value().maxTemp, 0, 'f', 2));
                }
                tempHtml += QStringLiteral("<p>框温度：%1</p>").arg(boxLines.join(QStringLiteral("；")));
            }

            if (capture->alarmTriggered) {
                tempHtml += QStringLiteral("<p>测温区域温度达到或超过预警阈值，测试已停止。</p>");
            }

            for (const auto &task : runtimeTasks) {
                const QString componentId = task.task.componentRef.trimmed().isEmpty()
                    ? task.signalPrefix
                    : task.task.componentRef.trimmed();
                Logger::log(QStringLiteral("FaultDiag diagnose begin: runId=%1 taskId=%2 component=%3")
                                .arg(runId)
                                .arg(task.taskId)
                                .arg(componentId),
                            Logger::Level::Info);

                ComponentViewData view;
                view.id = componentId;
                view.name = componentId;
                if (capture->hasAlarmFrame) {
                    view.thermalImage = capture->alarmFrame.irImage;
                    view.thermalMatrix = capture->alarmFrame.temperatureMatrix;
                    view.thermalMatrixSize = capture->alarmFrame.matrixSize;
                    if (!capture->hasAlarmPoint && capture->hasAlarmRect) {
                        bool pointOk = false;
                        capture->alarmPoint = maxTemperaturePointInRect(capture->alarmFrame, capture->alarmRect, &pointOk);
                        capture->hasAlarmPoint = pointOk;
                    }
                } else if (capture->hasFrame) {
                    view.thermalImage = capture->frame.irImage;
                    view.thermalMatrix = capture->frame.temperatureMatrix;
                    view.thermalMatrixSize = capture->frame.matrixSize;
                }
                view.hasThermalAlarmPoint = capture->alarmTriggered && capture->hasAlarmPoint;
                if (view.hasThermalAlarmPoint) {
                    view.thermalAlarmPoint = capture->alarmPoint;
                }

                DiagnosticInput diagInput;
                diagInput.componentRef = componentId;
                diagInput.componentType = task.task.pluginId.isEmpty() ? QStringLiteral("tps.multi.signal") : task.task.pluginId;
                diagInput.parameters = task.settings;
                if (!std::isnan(capture->alarmMaxTempC)) {
                    diagInput.parameters.insert(QStringLiteral("temperatureMaxC"), capture->alarmMaxTempC);
                    diagInput.parameters.insert(QStringLiteral("roiMaxTempC"), capture->alarmMaxTempC);
                    diagInput.parameters.insert(QStringLiteral("tMaxC"), capture->alarmMaxTempC);
                }
                diagInput.parameters.insert(QStringLiteral("temperatureAlarmTriggered"), capture->alarmTriggered);
                if (!std::isnan(capture->alarmThresholdC)) {
                    diagInput.parameters.insert(QStringLiteral("temperatureAlarmThresholdC"), capture->alarmThresholdC);
                }
                if (!capture->alarmSourceId.isEmpty()) {
                    diagInput.parameters.insert(QStringLiteral("temperatureAlarmSourceId"), capture->alarmSourceId);
                }
                diagInput.timestamp = QDateTime::currentDateTime();
                const auto stitchedSeries = capture->dataManager.buildSignalSeries(task.plan.bindings);
                if (!stitchedSeries.isEmpty()) {
                    diagInput.signalSeries = stitchedSeries;
                } else if (capture->hasBatch) {
                    diagInput.signalSeries = DiagnosticDataMapper::mapSignals(capture->lastBatch, task.plan.bindings);
                }
                if (m_taskContextManager) {
                    m_taskContextManager->setRawDataSummary(task.taskId, diagInput.signalSeries);
                }

                QString diagError;
                const DiagnosticReport report = m_diagnosticDispatcher.diagnose(diagInput, &diagError);
                const double daqDurationSec = signalSeriesDurationSec(diagInput.signalSeries);
                if (!diagError.isEmpty()) {
                    Logger::log(QStringLiteral("FaultDiag diagnose warning: taskId=%1 error=%2")
                                    .arg(task.taskId)
                                    .arg(diagError),
                                Logger::Level::Warn);
                }
                const double measured = report.metrics.value(QStringLiteral("voltageIn1.avg"), 0.0).toDouble();
                const QString detail = report.detailHtml.isEmpty() ? QString() : report.detailHtml;
                view.reportHtml = QStringLiteral(
                    "<h3>诊断结果</h3>"
                    "<p>Task ID：%4</p>"
                    "<p>Run ID：%5</p>"
                    "<p>状态：%1</p>%2%3")
                    .arg(report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                    .arg(detail)
                    .arg(tempHtml)
                    .arg(task.taskId)
                    .arg(runId);

                if (m_taskContextManager) {
                    m_taskContextManager->setDiagnosticResult(task.taskId, report, view.reportHtml);
                    m_taskContextManager->closeTask(task.taskId,
                                                    report.success ? QStringLiteral("Completed")
                                                                   : QStringLiteral("CompletedWithFailure"));
                }
                Logger::log(QStringLiteral("FaultDiag diagnose complete: runId=%1 taskId=%2 status=%3 signalSeries=%4")
                                .arg(runId)
                                .arg(task.taskId)
                                .arg(report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                                .arg(diagInput.signalSeries.size()),
                            Logger::Level::Info);

                view.taskId = task.taskId;
                view.name = componentId;
                view.x = capture->tempValues.isEmpty()
                    ? QVector<double>{0.0}
                    : buildUniformTimelineForDuration(capture->tempValues.size(), daqDurationSec);
                view.y = capture->tempValues.isEmpty() ? QVector<double>{measured} : capture->tempValues;
                view.x5322 = capture->x5322;
                view.y5322 = capture->y5322;
                view.x5323 = capture->x5323;
                view.y5323 = capture->y5323;
                view.x8902 = capture->x8902;
                view.y8902 = capture->y8902;
                for (auto it = diagInput.signalSeries.begin(); it != diagInput.signalSeries.end(); ++it) {
                    const QString &signalId = it.key();
                    const DiagnosticSignalSeries &series = it.value();
                    if (series.samples.isEmpty()) {
                        continue;
                    }
                    QVector<double> xs;
                    xs.reserve(series.samples.size());
                    const double sampleRate = series.sampleRateHz > 0.0 ? series.sampleRateHz : 1.0;
                    const double dt = 1.0 / sampleRate;
                    for (int sampleIndex = 0; sampleIndex < series.samples.size(); ++sampleIndex) {
                        xs.push_back(sampleIndex * dt);
                    }
                    view.signalXById.insert(signalId, xs);
                    view.signalYById.insert(signalId, series.samples);
                }

                updateComponent(view);
            }
            Logger::log(QStringLiteral("FaultDiag batch completed: runId=%1")
                            .arg(runId),
                        Logger::Level::Info);
        };
        if (m_runtime && m_runtime->rts()) {
            batchConn = connect(m_runtime->rts(), &RuntimeServices::batchReady, this,
                                [capture, finishRun, useCaptureManager](const JYAlignedBatch &batch) {
                                    capture->hasBatch = true;
                                    if (capture->lastBatch.packets.isEmpty()) {
                                        capture->lastBatch.timestampMs = batch.timestampMs;
                                    }
                                    for (auto it = batch.packets.begin(); it != batch.packets.end(); ++it) {
                                        capture->lastBatch.packets.insert(it.key(), it.value());
                                        if (!useCaptureManager) {
                                            const int delta = qMax(0, it.value().samplesPerChannel);
                                            capture->sampleCounts[it.key()] = capture->sampleCounts.value(it.key(), 0) + delta;
                                        }
                                    }
                                    capture->lastBatch.timestampMs = qMax(capture->lastBatch.timestampMs, batch.timestampMs);
                                    if (!useCaptureManager && !capture->targetSamples.isEmpty()) {
                                        bool done = true;
                                        for (auto it = capture->targetSamples.begin(); it != capture->targetSamples.end(); ++it) {
                                            const int have = capture->sampleCounts.value(it.key(), 0);
                                            if (have < it.value()) {
                                                done = false;
                                                break;
                                            }
                                        }
                                        if (done) {
                                            if (*finishRun) {
                                                (*finishRun)();
                                            }
                                            return;
                                        }
                                    }
                                    if (!useCaptureManager) {
                                        auto appendSeries = [capture](const JYDataPacket &packet, double sampleRate,
                                                                      QVector<double> &times, QVector<double> &values) {
                                            if (packet.channelCount <= 0) {
                                                return;
                                            }
                                            const int samples = packet.data.size() / packet.channelCount;
                                            if (samples <= 0) {
                                                return;
                                            }
                                            if (capture->t0ms < 0) {
                                                capture->t0ms = packet.timestampMs;
                                            }
                                            const double dtMs = 1000.0 / sampleRate;
                                            const double baseMs = static_cast<double>(packet.timestampMs - capture->t0ms);
                                            const double lastOffset = (samples - 1) * dtMs;
                                            for (int i = 0; i < samples; ++i) {
                                                const int idx = i * packet.channelCount;
                                                if (idx < 0 || idx >= packet.data.size()) {
                                                    continue;
                                                }
                                                const double tMs = baseMs - (lastOffset - i * dtMs);
                                                if (tMs < 0.0) {
                                                    continue;
                                                }
                                                times.push_back(tMs / 1000.0);
                                                values.push_back(packet.data[idx]);
                                            }
                                        };

                                        if (batch.packets.contains(JYDeviceKind::PXIe5322)) {
                                            appendSeries(batch.packets.value(JYDeviceKind::PXIe5322), 1000000.0,
                                                         capture->x5322, capture->y5322);
                                        }
                                        if (batch.packets.contains(JYDeviceKind::PXIe5323)) {
                                            appendSeries(batch.packets.value(JYDeviceKind::PXIe5323), 200000.0,
                                                         capture->x5323, capture->y5323);
                                        }
                                    }
                                }, Qt::QueuedConnection);
        }
        if (m_threadManager && m_threadManager->pipeline() && !targetSamplesByKind.isEmpty()) {
            packetConn = connect(m_threadManager->pipeline(), &JYDataPipeline::packetReady, this,
                                 [capture, finishRun](const JYDataPacket &packet) {
                                     if (capture->finished) {
                                         return;
                                     }
                                     if (!capture->targetSamples.contains(packet.kind)) {
                                         return;
                                     }
                                     if (packet.channelCount <= 0 || packet.data.isEmpty()) {
                                         return;
                                     }
                                     capture->dataManager.appendPacket(packet);
                                     capture->sampleCounts[packet.kind] = capture->dataManager.totalSamples(packet.kind);
                                     bool done = true;
                                     for (auto it = capture->targetSamples.begin(); it != capture->targetSamples.end(); ++it) {
                                         if (capture->sampleCounts.value(it.key(), 0) < it.value()) {
                                             done = false;
                                             break;
                                         }
                                     }
                                     if (!done) {
                                         return;
                                     }

                                     capture->hasBatch = true;
                                     if (*finishRun) {
                                         (*finishRun)();
                                     }
                                 }, Qt::QueuedConnection);
        }

        const int timeoutMs = targetSamplesByKind.isEmpty()
            ? qMax(1500, fallbackDurationMs)
            : qMax(1500, *std::max_element(targetSamplesByKind.begin(), targetSamplesByKind.end()) / 100);
        Logger::log(QStringLiteral("FaultDiag capture timeout armed: runId=%1 timeoutMs=%2")
                        .arg(runId)
                        .arg(timeoutMs),
                    Logger::Level::Info);
        QTimer::singleShot(timeoutMs, this, [finishRun]() {
            if (*finishRun) {
                (*finishRun)();
            }
        });
    } else {
        if (m_runtime && m_runtime->rts()) {
            QString stopError;
            m_runtime->rts()->completeRun(2000, &stopError);
        }
        Logger::log(QStringLiteral("FaultDiag test failed: runId=%1 reason=IRCameraUnavailable")
                        .arg(runId),
                    Logger::Level::Error);
        closePreparedTasks(QStringLiteral("Failed"), QStringLiteral("IRCameraUnavailable"));
    }
}

void FaultDiagnostic::runTest(const QString &componentRef,
                              const QString &pluginId,
                              const QMap<QString, QVariant> &parameters)
{
    TPSPluginInterface *plugin = m_tpsManager ? m_tpsManager->plugin(pluginId) : nullptr;
    if (!plugin) {
        QMessageBox::warning(this, tr("测试"), tr("未找到测试插件。"));
        return;
    }

    const TPSPluginRequirement requirement = plugin->requirements();
    QVector<TPSPortBinding> bindings;
    QString portError;
    if (!DevicePortManager::allocate(requirement.ports, &bindings, &portError)) {
        QMessageBox::warning(this, tr("测试"), tr("端口分配失败：%1").arg(portError));
        return;
    }

    auto hasDevice = [&](JYDeviceKind kind) {
        for (const auto &binding : bindings) {
            if (binding.deviceKind == kind) {
                return true;
            }
        }
        return false;
    };

    if (m_threadManager && !m_devicesCreated) {
        if (hasDevice(JYDeviceKind::PXIe5711)) {
            m_threadManager->create5711Worker();
        }
        if (hasDevice(JYDeviceKind::PXIe5322)) {
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5322);
        }
        if (hasDevice(JYDeviceKind::PXIe5323)) {
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5323);
        }
        if (hasDevice(JYDeviceKind::PXIe8902)) {
            m_threadManager->create8902Worker();
        }
        m_devicesCreated = true;
    }

    TPSRequest request;
    request.runId = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    request.boardId = QStringLiteral("board");
    request.createdAt = QDateTime::currentDateTime();

    UTRItem item;
    item.componentRef = componentRef;
    item.planId = pluginId.isEmpty() ? QStringLiteral("multi.signal.basic") : pluginId;
    request.items.push_back(item);

    QMap<QString, QVariant> settings = parameters;
    const QString paramSnapshotDbPath = resolveParamSnapshotDbPath();
    const int componentId = componentIdFromRef(componentRef);
    const QMap<QString, QVariant> dbParams = loadTaskParamsFromDb(paramSnapshotDbPath,
                                                                   request.boardId,
                                                                   componentId,
                                                                   pluginId);
    if (!dbParams.isEmpty()) {
        settings = dbParams;
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            settings.insert(it.key(), it.value());
        }
    }
    for (const auto &def : requirement.parameters) {
        if (!settings.contains(def.key) || !settings.value(def.key).isValid()) {
            settings.insert(def.key, def.defaultValue);
        }
    }

    QString error;
    plugin->configure(settings, &error);

    TPSDevicePlan plan;
    if (!plugin->buildDevicePlan(bindings, settings, &plan, &error)) {
        QMessageBox::warning(this, tr("测试"), tr("设备配置生成失败：%1").arg(error));
        return;
    }

    QVector<ResourceMapping> mappings;
    for (const auto &binding : plan.bindings) {
        ResourceMapping mapping;
        mapping.signalType = binding.identifier;
        mapping.binding.kind = binding.deviceKind;
        mapping.binding.channel = binding.channel;
        mapping.binding.resourceId = binding.resourceId;
        mappings.push_back(mapping);
    }

    QVector<SignalRequest> signalRequests;
    for (const auto &req : plan.requests) {
        signalRequests.push_back(SignalRequest{req.id, req.signalType, req.value, req.unit});
    }

    if (m_runtime && m_runtime->rms()) {
        m_runtime->rms()->setMappings(mappings);
    }

    bool started = true;
    if (m_runtime && m_runtime->rts()) {
        QString runError;
        started = m_runtime->rts()->startRun(request.runId,
                   request.boardId,
                   signalRequests,
                   plan.cfg532x,
                   plan.cfg5711,
                   plan.cfg8902,
                   2000,
                   &runError);
        if (!runError.isEmpty()) {
            QMessageBox::information(this, tr("测试"), tr("运行时启动失败：%1").arg(runError));
        }
    }

    if (!started) {
        return;
    }

    struct TempCapture {
        IRCameraStation::Frame frame;
        IRCameraStation::Frame alarmFrame;
        IRCameraStation::PointResult point;
        IRCameraStation::BoxResult box;
        bool hasFrame = false;
        bool hasAlarmFrame = false;
        bool hasPoint = false;
        bool hasBox = false;
        bool hasBatch = false;
        bool alarmTriggered = false;
        JYAlignedBatch lastBatch;
        qint64 t0ms = -1;
        QVector<double> tempTimes;
        QVector<double> tempValues;
        QVector<double> x5322;
        QVector<double> y5322;
        QVector<double> x5323;
        QVector<double> y5323;
        QVector<double> x8902;
        QVector<double> y8902;
        double alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
        double alarmMaxTempC = std::numeric_limits<double>::quiet_NaN();
        QRectF alarmRect;
        QPointF alarmPoint;
        bool hasAlarmRect = false;
        bool hasAlarmPoint = false;
        bool finished = false;
    };
    auto capture = QSharedPointer<TempCapture>::create();
    bool alarmThresholdOk = false;
    capture->alarmThresholdC = settings.value(QStringLiteral("temperatureWarnC")).toDouble(&alarmThresholdOk);
    if (!alarmThresholdOk) {
        capture->alarmThresholdC = settings.value(QStringLiteral("temperatureTripC")).toDouble(&alarmThresholdOk);
    }
    if (!alarmThresholdOk) {
        capture->alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
    }
    IRCameraStation *station = IRCameraStation::instance();
    if (station) {
        station->start();
        const QString tag = QStringLiteral("resistance_test");
        station->clearSubscriptions(tag);

        IRCameraStation::Frame latest;
        QPointF pointPos(10.0, 10.0);
        QRectF boxRect(5.0, 5.0, 20.0, 20.0);
        if (station->tryGetLatestFrame(&latest) && latest.irImage.isNull() == false) {
            const QSize size = latest.irImage.size();
            pointPos = QPointF(size.width() / 2.0, size.height() / 2.0);
            boxRect = QRectF(pointPos.x() - 15.0, pointPos.y() - 15.0, 30.0, 30.0);
        }

        station->setPointSubscription(tag, QStringLiteral("P1"), pointPos);
        station->setBoxSubscription(tag, QStringLiteral("B1"), boxRect);

        auto finishRun = QSharedPointer<std::function<void()>>::create();
        auto frameConn = connect(station, &IRCameraStation::frameReady, this,
                                 [capture](const IRCameraStation::Frame &frame) {
                                     capture->frame = frame;
                                     capture->hasFrame = true;
                                 });
        auto pointConn = connect(station, &IRCameraStation::pointTemperatureReady, this,
                                 [capture](const IRCameraStation::PointResult &result) {
                                     capture->point = result;
                                     capture->hasPoint = true;
                                     if (capture->t0ms < 0) {
                                         capture->t0ms = result.timestampMs;
                                     }
                                     const double tSec = (result.timestampMs - capture->t0ms) / 1000.0;
                                     capture->tempTimes.push_back(tSec);
                                     capture->tempValues.push_back(result.temp);
                                 });
        auto boxConn = connect(station, &IRCameraStation::boxTemperatureReady, this,
                               [capture, finishRun](const IRCameraStation::BoxResult &result) {
                                    capture->box = result;
                                    capture->hasBox = true;
                                    if (std::isnan(capture->alarmMaxTempC) || result.maxTemp > capture->alarmMaxTempC) {
                                        capture->alarmMaxTempC = result.maxTemp;
                                    }
                                    if (capture->alarmTriggered
                                        || std::isnan(capture->alarmThresholdC)
                                        || result.maxTemp < capture->alarmThresholdC) {
                                        return;
                                    }
                                    capture->alarmTriggered = true;
                                    capture->alarmRect = result.rect;
                                    capture->hasAlarmRect = true;
                                    if (capture->hasFrame) {
                                        capture->alarmFrame = capture->frame;
                                        capture->hasAlarmFrame = true;
                                        bool pointOk = false;
                                        capture->alarmPoint = maxTemperaturePointInRect(capture->frame, result.rect, &pointOk);
                                        capture->hasAlarmPoint = pointOk;
                                    }
                                    Logger::log(QStringLiteral("FaultDiag temperature alarm: threshold=%1 current=%2")
                                                    .arg(capture->alarmThresholdC, 0, 'f', 2)
                                                    .arg(result.maxTemp, 0, 'f', 2),
                                                Logger::Level::Warn);
                                    if (*finishRun) {
                                        (*finishRun)();
                                    }
                                });

        QMetaObject::Connection batchConn;
        if (m_runtime && m_runtime->rts()) {
            batchConn = connect(m_runtime->rts(), &RuntimeServices::batchReady, this,
                                [capture](const JYAlignedBatch &batch) {
                                    capture->lastBatch = batch;
                                    capture->hasBatch = true;
                                    auto appendSeries = [capture](const JYDataPacket &packet, double sampleRate,
                                                                  QVector<double> &times, QVector<double> &values) {
                                        if (packet.channelCount <= 0) {
                                            return;
                                        }
                                        const int samples = packet.data.size() / packet.channelCount;
                                        if (samples <= 0) {
                                            return;
                                        }
                                        const double dtMs = 1000.0 / sampleRate;
                                        for (int i = 0; i < samples; ++i) {
                                            const int idx = i * packet.channelCount;
                                            if (idx < 0 || idx >= packet.data.size()) {
                                                continue;
                                            }
                                            const double tMs = i * dtMs;
                                            times.push_back(tMs / 1000.0);
                                            values.push_back(packet.data[idx]);
                                        }
                                    };

                                    if (batch.packets.contains(JYDeviceKind::PXIe5322)) {
                                        appendSeries(batch.packets.value(JYDeviceKind::PXIe5322), 1000000.0,
                                                     capture->x5322, capture->y5322);
                                    }
                                    if (batch.packets.contains(JYDeviceKind::PXIe5323)) {
                                        appendSeries(batch.packets.value(JYDeviceKind::PXIe5323), 200000.0,
                                                     capture->x5323, capture->y5323);
                                    }
                                    if (batch.packets.contains(JYDeviceKind::PXIe8902)) {
                                        appendSeries(batch.packets.value(JYDeviceKind::PXIe8902), 1000.0,
                                                     capture->x8902, capture->y8902);
                                    }
                                }, Qt::QueuedConnection);
        }

        *finishRun = [=]() {
            if (capture->finished) {
                return;
            }
            capture->finished = true;
            station->clearSubscriptions(tag);
            disconnect(frameConn);
            disconnect(pointConn);
            disconnect(boxConn);
            if (batchConn) {
                disconnect(batchConn);
            }

            if (m_runtime && m_runtime->rts()) {
                QString stopError;
                m_runtime->rts()->completeRun(2000, &stopError);
            }

            ComponentViewData view;
            view.id = item.componentRef;
            view.name = item.componentRef;
            if (capture->hasAlarmFrame) {
                view.thermalImage = capture->alarmFrame.irImage;
                view.thermalMatrix = capture->alarmFrame.temperatureMatrix;
                view.thermalMatrixSize = capture->alarmFrame.matrixSize;
                if (!capture->hasAlarmPoint && capture->hasAlarmRect) {
                    bool pointOk = false;
                    capture->alarmPoint = maxTemperaturePointInRect(capture->alarmFrame, capture->alarmRect, &pointOk);
                    capture->hasAlarmPoint = pointOk;
                }
            } else if (capture->hasFrame) {
                view.thermalImage = capture->frame.irImage;
                view.thermalMatrix = capture->frame.temperatureMatrix;
                view.thermalMatrixSize = capture->frame.matrixSize;
            }
            view.hasThermalAlarmPoint = capture->alarmTriggered && capture->hasAlarmPoint;
            if (view.hasThermalAlarmPoint) {
                view.thermalAlarmPoint = capture->alarmPoint;
            }

            QString tempHtml;
            if (capture->hasPoint) {
                tempHtml += QStringLiteral("<p>点温度：%1 ℃</p>").arg(capture->point.temp, 0, 'f', 2);
            }
            if (capture->hasBox) {
                tempHtml += QStringLiteral("<p>框温度(min/avg/max)：%1 / %2 / %3 ℃</p>")
                    .arg(capture->box.minTemp, 0, 'f', 2)
                    .arg(capture->box.avgTemp, 0, 'f', 2)
                    .arg(capture->box.maxTemp, 0, 'f', 2);
            }

            if (capture->alarmTriggered) {
                tempHtml += QStringLiteral("<p>测温区域温度达到或超过预警阈值，测试已停止。</p>");
            }

            DiagnosticInput diagInput;
            diagInput.componentRef = item.componentRef;
            diagInput.componentType = pluginId;
            diagInput.parameters = settings;
            if (!std::isnan(capture->alarmMaxTempC)) {
                diagInput.parameters.insert(QStringLiteral("temperatureMaxC"), capture->alarmMaxTempC);
                diagInput.parameters.insert(QStringLiteral("roiMaxTempC"), capture->alarmMaxTempC);
                diagInput.parameters.insert(QStringLiteral("tMaxC"), capture->alarmMaxTempC);
            }
            diagInput.parameters.insert(QStringLiteral("temperatureAlarmTriggered"), capture->alarmTriggered);
            if (!std::isnan(capture->alarmThresholdC)) {
                diagInput.parameters.insert(QStringLiteral("temperatureAlarmThresholdC"), capture->alarmThresholdC);
            }
            diagInput.timestamp = QDateTime::currentDateTime();
            if (capture->hasBatch) {
                diagInput.signalSeries = DiagnosticDataMapper::mapSignals(capture->lastBatch, bindings);
            }

            QString diagError;
            const DiagnosticReport report = m_diagnosticDispatcher.diagnose(diagInput, &diagError);
            const double daqDurationSec = signalSeriesDurationSec(diagInput.signalSeries);
            const double measured = report.metrics.value(QStringLiteral("voltageIn1.avg"), 0.0).toDouble();
            const QString detail = report.detailHtml.isEmpty() ? QString() : report.detailHtml;
            view.reportHtml = QStringLiteral(
                "<h3>诊断结果</h3>"
                "<p>状态：%1</p>%2%3")
                .arg(report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                .arg(detail)
                .arg(tempHtml);

            view.x = capture->tempValues.isEmpty()
                ? QVector<double>{0.0}
                : buildUniformTimelineForDuration(capture->tempValues.size(), daqDurationSec);
            view.y = capture->tempValues.isEmpty() ? QVector<double>{measured} : capture->tempValues;
            view.x5322 = capture->x5322;
            view.y5322 = capture->y5322;
            view.x5323 = capture->x5323;
            view.y5323 = capture->y5323;
            view.x8902 = capture->x8902;
            view.y8902 = capture->y8902;
            for (auto it = diagInput.signalSeries.begin(); it != diagInput.signalSeries.end(); ++it) {
                const QString &signalId = it.key();
                const DiagnosticSignalSeries &series = it.value();
                if (series.samples.isEmpty()) {
                    continue;
                }
                QVector<double> xs;
                xs.reserve(series.samples.size());
                const double sampleRate = series.sampleRateHz > 0.0 ? series.sampleRateHz : 1.0;
                const double dt = 1.0 / sampleRate;
                for (int sampleIndex = 0; sampleIndex < series.samples.size(); ++sampleIndex) {
                    xs.push_back(sampleIndex * dt);
                }
                view.signalXById.insert(signalId, xs);
                view.signalYById.insert(signalId, series.samples);
            }

            updateComponent(view);
        };
        const int timeoutMs = qMax(1500, qMax(1, settings.value(QStringLiteral("captureDurationMs"), 1000).toInt()));
        QTimer::singleShot(timeoutMs, this, [finishRun]() {
            if (*finishRun) {
                (*finishRun)();
            }
        });
    }
}
void FaultDiagnostic::buildWidgets()
{
    if (!ui) {
        return;
    }

    m_list = ui->listComponents;
    if (m_list) {
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    if (ui->thermalContainer) {
        if (!ui->thermalContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->thermalContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_tempView = new IRCamera(ui->thermalContainer);
        m_tempView->setStationEnabled(false);

        if (auto *splitter = m_tempView->findChild<QSplitter *>(QStringLiteral("splitterMain"))) {
            splitter->setSizes({0, 1});
        }
        if (auto *leftPanel = m_tempView->findChild<QWidget *>(QStringLiteral("leftPanel"))) {
            leftPanel->setVisible(false);
        }

        ui->thermalContainer->layout()->addWidget(m_tempView);
    }

    if (ui->plotContainer) {
        if (!ui->plotContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->plotContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_plot = new UESTCQCustomPlot(ui->plotContainer);
        m_plot->setTimeAxisEnabled(false);
        m_plot->setAutoRangeEnabled(true);
        ui->plotContainer->layout()->addWidget(m_plot);
    }

    if (ui->reportContainer) {
        if (!ui->reportContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->reportContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_report = new QTextBrowser(ui->reportContainer);
        m_report->setOpenExternalLinks(false);
        ui->reportContainer->layout()->addWidget(m_report);
    }

    if (ui->splitterMain) {
        ui->splitterMain->setStretchFactor(0, 7);
        ui->splitterMain->setStretchFactor(1, 5);
        ui->splitterMain->setSizes({520, 360});
    }
    if (ui->splitterBottom) {
        ui->splitterBottom->setStretchFactor(0, 1);
        ui->splitterBottom->setStretchFactor(1, 1);
        ui->splitterBottom->setSizes({520, 520});
    }
}

void FaultDiagnostic::applyThemeQss()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (m_applyingQss) {
        return;
    }
    if (!theme.isEmpty() && theme == m_loadedTheme) {
        return;
    }
    const QString qssPath = (theme == QStringLiteral("light"))
        ? QStringLiteral(":/styles/faultdiagnostic_light.qss")
        : QStringLiteral(":/styles/faultdiagnostic_dark.qss");
    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_applyingQss = true;
        setStyleSheet(QString::fromUtf8(file.readAll()));
        m_applyingQss = false;
        if (!theme.isEmpty()) {
            m_loadedTheme = theme;
        }
    }
}

void FaultDiagnostic::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_components.size()) {
        return;
    }
    const auto &item = m_components[index];
    if (m_plot) {
        m_plot->removeStaticLine(QStringLiteral("娓╁害"));
        m_plot->removeStaticLine(QStringLiteral("5322"));
        m_plot->removeStaticLine(QStringLiteral("5323"));
        m_plot->removeStaticLine(QStringLiteral("8902"));
        const QStringList keys = m_signalGraphs.keys();
        for (const QString &key : keys) {
            m_plot->removeStaticLine(key);
        }
        m_tempGraph = nullptr;
        m_signalGraphs.clear();
    }
    refreshThermal(item);
    refreshPlot(item);
    refreshReport(item);
}

void FaultDiagnostic::refreshThermal(const ComponentViewData &item)
{
    if (!m_tempView) {
        return;
    }
    if (!item.thermalImage.isNull() && !item.thermalMatrix.isEmpty() && item.thermalMatrixSize.isValid()) {
        m_tempView->setInputData(item.thermalImage, item.thermalMatrix, item.thermalMatrixSize);
        if (item.hasThermalAlarmPoint) {
            m_tempView->setResultMarker(item.thermalAlarmPoint, QStringLiteral("最高温"));
        } else {
            m_tempView->clearResultMarker();
        }
    } else {
        m_tempView->setInputData(QImage(), {}, QSize());
        m_tempView->clearResultMarker();
    }
}

void FaultDiagnostic::refreshPlot(const ComponentViewData &item)
{
    if (!m_plot) {
        return;
    }
    auto downsample = [](const QVector<double> &xs, const QVector<double> &ys, int maxPoints) {
        if (xs.size() <= maxPoints || ys.size() <= maxPoints || xs.size() != ys.size()) {
            return qMakePair(xs, ys);
        }
        const int step = qMax(1, qCeil(static_cast<double>(xs.size()) / maxPoints));
        QVector<double> dx;
        QVector<double> dy;
        dx.reserve(xs.size() / step + 1);
        dy.reserve(ys.size() / step + 1);
        for (int i = 0; i < xs.size(); i += step) {
            dx.push_back(xs[i]);
            dy.push_back(ys[i]);
        }
        return qMakePair(dx, dy);
    };

    const auto temp = downsample(item.x, item.y, 4000);
    if (!m_tempGraph) {
        m_tempGraph = m_plot->addStaticLine(QStringLiteral("娓╁害"), temp.first, temp.second, QColor(52, 152, 219));
    } else {
        m_plot->updateStaticLine(m_tempGraph, temp.first, temp.second);
    }

    QStringList desiredSignalKeys;
    for (auto it = item.signalXById.begin(); it != item.signalXById.end(); ++it) {
        const QString &key = it.key();
        if (!item.signalYById.contains(key)) {
            continue;
        }
        const QVector<double> &xs = it.value();
        const QVector<double> &ys = item.signalYById.value(key);
        if (xs.isEmpty() || ys.isEmpty() || xs.size() != ys.size()) {
            continue;
        }
        desiredSignalKeys.push_back(key);
    }

    if (!desiredSignalKeys.isEmpty()) {
        m_plot->removeStaticLine(QStringLiteral("5322"));
        m_plot->removeStaticLine(QStringLiteral("5323"));
        m_plot->removeStaticLine(QStringLiteral("8902"));

        const QStringList existingKeys = m_signalGraphs.keys();
        for (const QString &key : existingKeys) {
            if (!desiredSignalKeys.contains(key)) {
                m_plot->removeStaticLine(key);
                m_signalGraphs.remove(key);
            }
        }

        const QVector<QColor> palette = {
            QColor(46, 204, 113),
            QColor(241, 196, 15),
            QColor(155, 89, 182),
            QColor(231, 76, 60),
            QColor(52, 152, 219),
            QColor(26, 188, 156)
        };

        for (int index = 0; index < desiredSignalKeys.size(); ++index) {
            const QString &key = desiredSignalKeys.at(index);
            const auto curve = downsample(item.signalXById.value(key), item.signalYById.value(key), 4000);
            if (!m_signalGraphs.contains(key) || !m_signalGraphs.value(key)) {
                const QColor color = palette.at(index % palette.size());
                m_signalGraphs.insert(key, m_plot->addStaticLine(key, curve.first, curve.second, color));
            } else {
                m_plot->updateStaticLine(m_signalGraphs.value(key), curve.first, curve.second);
            }
        }
        return;
    }

    const auto ch5322 = downsample(item.x5322, item.y5322, 4000);
    const auto ch5323 = downsample(item.x5323, item.y5323, 4000);
    const auto ch8902 = downsample(item.x8902, item.y8902, 4000);

    const QStringList existingKeys = m_signalGraphs.keys();
    for (const QString &key : existingKeys) {
        m_plot->removeStaticLine(key);
        m_signalGraphs.remove(key);
    }

    if (!m_signalGraphs.contains(QStringLiteral("5322")) || !m_signalGraphs.value(QStringLiteral("5322"))) {
        m_signalGraphs.insert(QStringLiteral("5322"),
                              m_plot->addStaticLine(QStringLiteral("5322"), ch5322.first, ch5322.second, QColor(46, 204, 113)));
    } else {
        m_plot->updateStaticLine(m_signalGraphs.value(QStringLiteral("5322")), ch5322.first, ch5322.second);
    }

    if (!m_signalGraphs.contains(QStringLiteral("5323")) || !m_signalGraphs.value(QStringLiteral("5323"))) {
        m_signalGraphs.insert(QStringLiteral("5323"),
                              m_plot->addStaticLine(QStringLiteral("5323"), ch5323.first, ch5323.second, QColor(241, 196, 15)));
    } else {
        m_plot->updateStaticLine(m_signalGraphs.value(QStringLiteral("5323")), ch5323.first, ch5323.second);
    }

    if (!m_signalGraphs.contains(QStringLiteral("8902")) || !m_signalGraphs.value(QStringLiteral("8902"))) {
        m_signalGraphs.insert(QStringLiteral("8902"),
                              m_plot->addStaticLine(QStringLiteral("8902"), ch8902.first, ch8902.second, QColor(155, 89, 182)));
    } else {
        m_plot->updateStaticLine(m_signalGraphs.value(QStringLiteral("8902")), ch8902.first, ch8902.second);
    }
}

void FaultDiagnostic::refreshReport(const ComponentViewData &item)
{
    if (!m_report) {
        return;
    }
    if (item.reportHtml.isEmpty()) {
        m_report->clear();
        return;
    }
    m_report->setHtml(item.reportHtml);
}
