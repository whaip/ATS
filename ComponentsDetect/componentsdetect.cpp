#include "componentsdetect.h"
#include "ui_componentsdetect.h"
#include "yolostationclient.h"
#include "yolomodel.h"
#include "../logger.h"
#include "../HDCamera/camerastationclient.h"
#include "../HDCamera/cameratypes.h"
#include "../HDCamera/dshowcamerautil.h"
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QVariant>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListView>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QTimer>
#include <QPushButton>
#include <QCheckBox>
#include <QShowEvent>
#include <QHideEvent>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QFontMetrics>
#include <QSet>
#include <algorithm>
#include <opencv2/opencv.hpp>

namespace {
constexpr int kShowLabelText = 1 << 0;
constexpr int kShowClassId = 1 << 1;
constexpr int kShowConfidence = 1 << 2;

cv::Scalar classColorForBgr(int cls)
{
    const auto &model = YOLOModel::getInstance();
    const auto &colors = model->get_colors();
    if (cls >= 0 && cls < static_cast<int>(colors.size())) {
        return colors[cls];
    }
    return cv::Scalar(0, 255, 0);
}

QString buildLabelText(const CompLabel &l, int opts)
{
    QStringList parts;
    if (opts & kShowLabelText) {
        parts << l.label;
    }
    if (opts & kShowClassId) {
        parts << QStringLiteral("ID:%1").arg(l.cls);
    }
    if (opts & kShowConfidence) {
        parts << QString::number(l.confidence, 'f', 3);
    }
    return parts.join(QStringLiteral(" "));
}

cv::Mat qimageToBgrMat(const QImage &img)
{
    if (img.isNull()) {
        return {};
    }

    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.bits()), static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

QImage bgrMatToQImage(const cv::Mat &bgr)
{
    if (bgr.empty()) {
        return {};
    }

    if (bgr.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        QImage out(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return out.copy();
    }

    if (bgr.type() == CV_8UC1) {
        QImage out(bgr.data, bgr.cols, bgr.rows, static_cast<int>(bgr.step), QImage::Format_Grayscale8);
        return out.copy();
    }

    if (bgr.type() == CV_8UC4) {
        cv::Mat rgba;
        cv::cvtColor(bgr, rgba, cv::COLOR_BGRA2RGBA);
        QImage out(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step), QImage::Format_RGBA8888);
        return out.copy();
    }

    cv::Mat bgr8;
    bgr.convertTo(bgr8, CV_8U);
    return bgrMatToQImage(bgr8);
}
}

ComponentsDetect::ComponentsDetect(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ComponentsDetect)
{
    ui->setupUi(this);

    applyLocalStyleForTheme();

    initClassSelectUi();

    if (ui->tableResults) {
        ui->tableResults->horizontalHeader()->setStretchLastSection(true);
        connect(ui->tableResults->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &ComponentsDetect::onTableSelectionChanged);
    }

    initCameraSettingsUi();

    // Recognition controls
    if (ui->onoroff) {
        ui->onoroff->setCheckable(true);
        // Default: recognition OFF. Model can still be preloaded at app startup.
        ui->onoroff->setChecked(false);
        ui->onoroff->setText(QStringLiteral("开始识别"));

        if (ui->labelMonitorStatus) {
            ui->labelMonitorStatus->setText(QStringLiteral("已停止"));
        }
        if (ui->frameMonitorIndicator) {
            ui->frameMonitorIndicator->setVisible(false);
        }
        if (ui->labelPromptValue) {
            ui->labelPromptValue->setText(QStringLiteral("识别结束"));
        }

        connect(ui->onoroff, &QPushButton::toggled, this, [this](bool on) {
            if (ui && ui->onoroff) {
                ui->onoroff->setText(on ? QStringLiteral("停止识别") : QStringLiteral("开始识别"));
            }
            if (m_worker) {
                m_worker->setEnabled(on);
            }
            if (ui && ui->labelMonitorStatus) {
                ui->labelMonitorStatus->setText(on ? QStringLiteral("监测中") : QStringLiteral("已停止"));
            }
            if (ui && ui->frameMonitorIndicator) {
                ui->frameMonitorIndicator->setVisible(on);
            }
            if (ui && ui->labelPromptValue) {
                ui->labelPromptValue->setText(on ? QStringLiteral("识别中") : QStringLiteral("识别结束"));
            }
        });
    }

    if (ui->usepcbextract) {
        connect(ui->usepcbextract, &QPushButton::toggled, this, [this](bool on) {
            if (m_worker) {
                m_worker->setUsePcbExtract(on);
            }
            refreshOverlayPreview();
        });
    }

    // Label overlay content options (on-image text)
    {
        const auto applyLabelOptions = [this]() {
            if (!ui) {
                return;
            }
            refreshOverlayPreview();
        };

        if (ui->checkShowLabelText) {
            connect(ui->checkShowLabelText, &QCheckBox::toggled, this, [applyLabelOptions](bool) { applyLabelOptions(); });
        }
        if (ui->checkShowClassId) {
            connect(ui->checkShowClassId, &QCheckBox::toggled, this, [applyLabelOptions](bool) { applyLabelOptions(); });
        }
        if (ui->checkShowConfidence) {
            connect(ui->checkShowConfidence, &QCheckBox::toggled, this, [applyLabelOptions](bool) { applyLabelOptions(); });
        }

        // Apply once after UI is constructed; worker may not exist yet.
        QTimer::singleShot(0, this, [this, applyLabelOptions]() { applyLabelOptions(); });
    }

    if (ui->sliderProgress) {
        connect(ui->sliderProgress, &QSlider::valueChanged, this, [this](int) {
            refreshOverlayPreview();
        });
    }
}

void ComponentsDetect::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Start camera + worker only when page is visible.
    startPipeline();
    applyCameraSettingsNow();
}

void ComponentsDetect::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // Stop background capture when page is hidden to avoid warnings/spam.
    stopPipeline();
}

bool ComponentsDetect::eventFilter(QObject *watched, QEvent *event)
{
    if (!ui || !ui->comboSelect || !m_classSelectModel) {
        return QWidget::eventFilter(watched, event);
    }

    // When combo is editable+readOnly, clicking the line edit won't open the popup by default.
    // Make click anywhere open it.
    if (event && event->type() == QEvent::MouseButtonPress) {
        if (watched == ui->comboSelect || watched == ui->comboSelect->lineEdit()) {
            ui->comboSelect->showPopup();
            return true;
        }
    }

    QObject *viewport = ui->comboSelect->view() ? ui->comboSelect->view()->viewport() : nullptr;
    if (watched == viewport && event && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QModelIndex idx = ui->comboSelect->view()->indexAt(me->pos());
        if (!idx.isValid()) {
            return true;
        }

        if (m_updatingClassSelect) {
            return true;
        }
        m_updatingClassSelect = true;

        QStandardItem *item = m_classSelectModel->itemFromIndex(idx);
        if (item) {
            const int row = idx.row();
            if (row == 0) {
                const bool wantChecked = (item->checkState() != Qt::Checked);
                setAllClassesChecked(wantChecked);
            } else {
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                syncAllItemState();
            }
        }

        m_updatingClassSelect = false;
        updateComboSelectText();
        applyClassDisplayToWorker();

        // Keep popup open for multi-select.
        QTimer::singleShot(0, this, [this]() {
            if (ui && ui->comboSelect) {
                ui->comboSelect->showPopup();
            }
        });
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

ComponentsDetect::~ComponentsDetect()
{
    stopPipeline();
    delete ui;
}

void ComponentsDetect::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::StyleChange) {
        applyLocalStyleForTheme();
    }
}

void ComponentsDetect::applyLocalStyleForTheme()
{
    const bool isDark = qApp && qApp->property("atsTheme").toString() == QStringLiteral("dark");
    const QString resPath = isDark
                                ? QStringLiteral(":/styles/componentsdetect_dark.qss")
                                : QStringLiteral(":/styles/componentsdetect_light.qss");

    QFile f(resPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const QString qss = QString::fromUtf8(f.readAll());
    if (!qss.isEmpty()) {
        if (styleSheet() != qss) {
            setStyleSheet(qss);
        }
    }
}

void ComponentsDetect::startPipeline()
{
    if (!m_worker) {
        m_worker = new YoloStationClient(QStringLiteral("ComponentsDetect"), this);
        applyClassDisplayToWorker();

        connect(m_worker, &YoloStationClient::resultReady, this, &ComponentsDetect::onYoloResultReady, Qt::QueuedConnection);
    }

    // Always restore worker state when page becomes visible again.
    if (m_worker) {
        const bool recognizeOn = (ui && ui->onoroff && ui->onoroff->isChecked());
        m_worker->setEnabled(recognizeOn);
        if (ui && ui->usepcbextract) {
            m_worker->setUsePcbExtract(ui->usepcbextract->isChecked());
        }
        applyClassDisplayToWorker();
    }

    if (!m_camera) {
        m_camera = new CameraStationClient(this);
        connect(m_camera, &CameraStationClient::imageCaptured, this, [this](const ImageData &img) {
            if (!ui || img.image.isNull()) {
                return;
            }

            // When recognition is enabled, feed frames to worker.
            // Otherwise show raw preview (does not touch last labels/table).
            const bool recognizeOn = (ui->onoroff && ui->onoroff->isChecked());
            if (recognizeOn && m_worker) {
                // Drop-frame strategy handled inside worker (keeps latest only).
                m_worker->submitFrame(img.image, img.timestampMs);
            } else if (ui->graphicsViewPreview) {
                ui->graphicsViewPreview->setImage(img.image);
            }
        });

        connect(CameraStation::instance(), &CameraStation::configureFinished, this,
                [this](bool ok, int /*deviceIndex*/, int /*w*/, int /*h*/, int /*fps*/, int /*fourcc*/, const QString &/*detail*/) {
                    if (ui && ui->labelCameraStatus) {
                        ui->labelCameraStatus->setText(ok ? QStringLiteral("摄像头已开启") : QStringLiteral("摄像头开启失败"));
                    }
                });

        // Configure camera using current UI selection.
        QTimer::singleShot(0, this, [this]() { applyCameraSettingsNow(); });
    }

    // Always restore camera subscription when page becomes visible again.
    if (m_camera) {
        m_camera->setEnabled(true);
    }

    if (ui && ui->labelMonitorStatus) {
        ui->labelMonitorStatus->setText(QStringLiteral("监测中"));
    }
    if (ui && ui->frameMonitorIndicator) {
        ui->frameMonitorIndicator->setVisible(true);
    }
}

void ComponentsDetect::initClassSelectUi()
{
    if (!ui || !ui->comboSelect) {
        return;
    }

    // Class list should match YOLOModel::class_name order.
    m_classNames = {
        QStringLiteral("Capacitor"),
        QStringLiteral("IC"),
        QStringLiteral("LED"),
        QStringLiteral("Resistor"),
        QStringLiteral("battery"),
        QStringLiteral("buzzer"),
        QStringLiteral("clock"),
        QStringLiteral("connector"),
        QStringLiteral("diode"),
        QStringLiteral("display"),
        QStringLiteral("fuse"),
        QStringLiteral("inductor"),
        QStringLiteral("potentiometer"),
        QStringLiteral("relay"),
        QStringLiteral("switch"),
        QStringLiteral("transistor")
    };

    ui->comboSelect->setEditable(true);
    if (ui->comboSelect->lineEdit()) {
        ui->comboSelect->lineEdit()->setReadOnly(true);
    }

    // Ensure popup view exists now (so viewport filter can be installed).
    auto *view = new QListView(ui->comboSelect);
    ui->comboSelect->setView(view);

    // Replace any designer items with a checkable model.
    ui->comboSelect->clear();
    m_classSelectModel = new QStandardItemModel(this);

    auto *allItem = new QStandardItem(QStringLiteral("所有对象"));
    allItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    allItem->setData(Qt::Checked, Qt::CheckStateRole);
    m_classSelectModel->appendRow(allItem);

    for (const QString &name : m_classNames) {
        auto *it = new QStandardItem(name);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setData(Qt::Checked, Qt::CheckStateRole);
        m_classSelectModel->appendRow(it);
    }

    ui->comboSelect->setModel(m_classSelectModel);
    ui->comboSelect->setCurrentIndex(0);

    // Open popup on click, and keep it open while checking items.
    ui->comboSelect->installEventFilter(this);
    if (ui->comboSelect->lineEdit()) {
        ui->comboSelect->lineEdit()->installEventFilter(this);
    }
    if (ui->comboSelect->view() && ui->comboSelect->view()->viewport()) {
        ui->comboSelect->view()->viewport()->installEventFilter(this);
    }

    updateComboSelectText();
}

void ComponentsDetect::updateComboSelectText()
{
    if (!ui || !ui->comboSelect || !m_classSelectModel) {
        return;
    }

    const int total = m_classNames.size();
    const int checked = checkedClassIds().size();

    QString text;
    if (checked <= 0) {
        text = QStringLiteral("未选择");
    } else if (checked >= total) {
        text = QStringLiteral("所有对象");
    } else {
        text = QStringLiteral("已选 %1 类").arg(checked);
    }

    ui->comboSelect->setEditText(text);
}

void ComponentsDetect::setAllClassesChecked(bool checked)
{
    if (!m_classSelectModel) {
        return;
    }

    const Qt::CheckState st = checked ? Qt::Checked : Qt::Unchecked;
    for (int r = 0; r < m_classSelectModel->rowCount(); ++r) {
        QStandardItem *it = m_classSelectModel->item(r);
        if (it) {
            it->setCheckState(st);
        }
    }
}

void ComponentsDetect::syncAllItemState()
{
    if (!m_classSelectModel || m_classSelectModel->rowCount() <= 1) {
        return;
    }

    int checked = 0;
    int unchecked = 0;
    for (int r = 1; r < m_classSelectModel->rowCount(); ++r) {
        QStandardItem *it = m_classSelectModel->item(r);
        if (!it) continue;
        if (it->checkState() == Qt::Checked) checked++;
        else unchecked++;
    }

    QStandardItem *allItem = m_classSelectModel->item(0);
    if (!allItem) return;
    if (unchecked == 0) {
        allItem->setCheckState(Qt::Checked);
    } else if (checked == 0) {
        allItem->setCheckState(Qt::Unchecked);
    } else {
        allItem->setCheckState(Qt::PartiallyChecked);
    }
}

QList<int> ComponentsDetect::checkedClassIds() const
{
    QList<int> out;
    if (!m_classSelectModel) {
        return out;
    }
    for (int r = 1; r < m_classSelectModel->rowCount(); ++r) {
        const QStandardItem *it = m_classSelectModel->item(r);
        if (it && it->checkState() == Qt::Checked) {
            out.push_back(r - 1);
        }
    }
    return out;
}

void ComponentsDetect::applyClassDisplayToWorker()
{
    if (!m_worker || !m_classSelectModel) {
        return;
    }
    m_worker->setClassDisplay(checkedClassIds());
}

void ComponentsDetect::initCameraSettingsUi()
{
    if (!ui) {
        return;
    }

    m_applySettingsTimer = new QTimer(this);
    m_applySettingsTimer->setSingleShot(true);
    m_applySettingsTimer->setInterval(250);
    connect(m_applySettingsTimer, &QTimer::timeout, this, [this]() { applyCameraSettingsNow(); });

    if (!ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat || !ui->comboFps) {
        return;
    }

    refreshCameraList();
    refreshResolutionListForCurrentCamera();
    refreshPixelFormatListForCurrentSelection();
    refreshFpsListForCurrentSelection();

    connect(ui->comboCamera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshResolutionListForCurrentCamera();
        refreshPixelFormatListForCurrentSelection();
        refreshFpsListForCurrentSelection();
        if (m_applySettingsTimer) m_applySettingsTimer->start();
    });
    connect(ui->comboResolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshPixelFormatListForCurrentSelection();
        refreshFpsListForCurrentSelection();
        if (m_applySettingsTimer) m_applySettingsTimer->start();
    });
    connect(ui->comboPixelFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshFpsListForCurrentSelection();
        if (m_applySettingsTimer) m_applySettingsTimer->start();
    });
    connect(ui->comboFps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_applySettingsTimer) m_applySettingsTimer->start();
    });

    if (ui->btnApplyCamera) {
        connect(ui->btnApplyCamera, &QPushButton::clicked, this, [this]() { applyCameraSettingsNow(); });
    }
}

void ComponentsDetect::refreshCameraList()
{
    if (!ui || !ui->comboCamera) {
        return;
    }

    m_capsByDevice.clear();
    QSignalBlocker b(ui->comboCamera);
    ui->comboCamera->clear();

#ifdef _WIN32
    const auto devices = DShowCameraUtil::enumerateDevices();
    for (int i = 0; i < devices.size(); ++i) {
        const auto &dev = devices.at(i);
        const QString name = dev.friendlyName.isEmpty() ? QStringLiteral("摄像头 %1").arg(i) : dev.friendlyName;
        ui->comboCamera->addItem(name, i);
        m_capsByDevice.push_back(dev.capabilities);
    }
#endif

    if (ui->comboCamera->count() == 0) {
        ui->comboCamera->addItem(QStringLiteral("摄像头 0"), 0);
        m_capsByDevice.push_back({});
    }

    ui->comboCamera->setCurrentIndex(0);
}

void ComponentsDetect::refreshResolutionListForCurrentCamera()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution) {
        return;
    }

    QSignalBlocker b(ui->comboResolution);
    ui->comboResolution->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    QList<QSize> resolutions;
    if (devIdx >= 0 && devIdx < m_capsByDevice.size()) {
        QSet<quint64> seen;
        const auto &caps = m_capsByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (!c.size.isValid()) continue;
            const quint64 key = (static_cast<quint64>(static_cast<quint32>(c.size.width())) << 32)
                                | static_cast<quint32>(c.size.height());
            if (!seen.contains(key)) {
                seen.insert(key);
                resolutions.push_back(c.size);
            }
        }
        std::sort(resolutions.begin(), resolutions.end(), [](const QSize &a, const QSize &b) {
            const qint64 areaA = static_cast<qint64>(a.width()) * a.height();
            const qint64 areaB = static_cast<qint64>(b.width()) * b.height();
            if (areaA != areaB) return areaA < areaB;
            if (a.width() != b.width()) return a.width() < b.width();
            return a.height() < b.height();
        });
    }

    if (resolutions.isEmpty()) {
        resolutions = {QSize(640, 480), QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)};
    }

    for (const QSize &s : resolutions) {
        ui->comboResolution->addItem(QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()), s);
    }

    int idx = -1;
    for (int i = 0; i < ui->comboResolution->count(); ++i) {
        if (ui->comboResolution->itemData(i).toSize() == QSize(1920, 1080)) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && ui->comboResolution->count() > 0) idx = ui->comboResolution->count() - 1;
    if (idx >= 0) ui->comboResolution->setCurrentIndex(idx);
}

void ComponentsDetect::refreshPixelFormatListForCurrentSelection()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat) {
        return;
    }

    QSignalBlocker b(ui->comboPixelFormat);
    ui->comboPixelFormat->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    const QSize size = ui->comboResolution->currentData().toSize();

    struct FormatEntry {
        int fourcc = 0;
        QString name;
        double maxFps = 0.0;
    };

    QSet<int> seen;
    QList<FormatEntry> formats;

    if (devIdx >= 0 && devIdx < m_capsByDevice.size() && size.isValid()) {
        const auto &caps = m_capsByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (c.size != size) continue;
            if (!seen.contains(c.fourcc)) {
                seen.insert(c.fourcc);
                formats.push_back({c.fourcc, c.formatName.isEmpty() ? QStringLiteral("UNKNOWN") : c.formatName, c.maxFps});
            } else {
                for (auto &e : formats) {
                    if (e.fourcc == c.fourcc) {
                        e.maxFps = std::max(e.maxFps, c.maxFps);
                        break;
                    }
                }
            }
        }
    }

    if (formats.isEmpty()) {
        ui->comboPixelFormat->addItem(QStringLiteral("默认"), 0);
        ui->comboPixelFormat->addItem(QStringLiteral("MJPG"), cv::VideoWriter::fourcc('M','J','P','G'));
        ui->comboPixelFormat->addItem(QStringLiteral("YUY2"), cv::VideoWriter::fourcc('Y','U','Y','2'));
        ui->comboPixelFormat->setCurrentIndex(0);
        return;
    }

    const int mjpg = cv::VideoWriter::fourcc('M','J','P','G');
    const bool isHighRes = size.width() >= 4000 || size.height() >= 3000;
    std::sort(formats.begin(), formats.end(), [&](const FormatEntry &a, const FormatEntry &b) {
        if (isHighRes) {
            const bool am = a.fourcc == mjpg;
            const bool bm = b.fourcc == mjpg;
            if (am != bm) return am;
        }
        if (a.maxFps != b.maxFps) return a.maxFps > b.maxFps;
        return a.name < b.name;
    });

    for (const auto &e : formats) {
        ui->comboPixelFormat->addItem(e.name, e.fourcc);
    }
    ui->comboPixelFormat->setCurrentIndex(0);
}

void ComponentsDetect::refreshFpsListForCurrentSelection()
{
    if (!ui || !ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat || !ui->comboFps) {
        return;
    }

    QSignalBlocker b(ui->comboFps);
    const int prev = ui->comboFps->currentData().toInt();
    ui->comboFps->clear();

    const int devIdx = ui->comboCamera->currentData().toInt();
    const QSize size = ui->comboResolution->currentData().toSize();
    const int fourcc = ui->comboPixelFormat->currentData().toInt();

    double minFps = 0.0;
    double maxFps = 0.0;
    if (devIdx >= 0 && devIdx < m_capsByDevice.size() && size.isValid()) {
        const auto &caps = m_capsByDevice.at(devIdx);
        for (const auto &c : caps) {
            if (c.size != size || c.fourcc != fourcc) continue;
            if (c.maxFps > 0.0) maxFps = (maxFps <= 0.0) ? c.maxFps : std::max(maxFps, c.maxFps);
            if (c.minFps > 0.0) minFps = (minFps <= 0.0) ? c.minFps : std::min(minFps, c.minFps);
        }
    }

    const QList<int> common = {5, 10, 15, 20, 30, 60};
    QList<int> fpsList;
    auto within = [&](int f) {
        if (maxFps > 0.0 && static_cast<double>(f) > maxFps + 0.001) return false;
        if (minFps > 0.0 && static_cast<double>(f) < minFps - 0.001) return false;
        return true;
    };
    for (int f : common) if (within(f)) fpsList.push_back(f);
    if (fpsList.isEmpty() && maxFps > 0.0) fpsList.push_back(std::max(1, static_cast<int>(std::floor(maxFps + 0.001))));
    if (fpsList.isEmpty()) fpsList = common;

    for (int f : fpsList) ui->comboFps->addItem(QStringLiteral("%1 fps").arg(f), f);

    int idx = -1;
    for (int i = 0; i < ui->comboFps->count(); ++i) {
        if (ui->comboFps->itemData(i).toInt() == prev) { idx = i; break; }
    }
    if (idx < 0 && ui->comboFps->count() > 0) idx = ui->comboFps->count() - 1;
    if (idx >= 0) ui->comboFps->setCurrentIndex(idx);
}

void ComponentsDetect::applyCameraSettingsNow()
{
    if (!ui || !m_camera || !ui->comboCamera || !ui->comboResolution || !ui->comboPixelFormat || !ui->comboFps) {
        return;
    }

    CameraStation::Config cfg;
    cfg.deviceIndex = ui->comboCamera->currentData().toInt();
    const QSize res = ui->comboResolution->currentData().toSize();
    if (res.isValid()) {
        cfg.width = res.width();
        cfg.height = res.height();
    }
    cfg.fourcc = ui->comboPixelFormat->currentData().toInt();
    cfg.fps = ui->comboFps->currentData().toInt();

    if (ui->labelCameraStatus) {
        ui->labelCameraStatus->setText(QStringLiteral("配置中..."));
    }
    m_camera->requestConfigure(cfg);
}

void ComponentsDetect::stopPipeline()
{
    if (m_camera) {
        m_camera->setEnabled(false);
    }
    if (m_worker) {
        // Station is persistent; just disable this page's submissions.
        m_worker->setEnabled(false);
    }
    if (ui && ui->labelMonitorStatus) {
        ui->labelMonitorStatus->setText(QStringLiteral("已停止"));
    }
}

void ComponentsDetect::refreshOverlayPreview()
{
    if (!ui || !ui->graphicsViewPreview) {
        return;
    }
    if (m_lastFrame.isNull()) {
        return;
    }
    ui->graphicsViewPreview->setImage(renderOverlay(m_lastFrame, m_lastLabels, m_lastPcbQuad));
}

QImage ComponentsDetect::renderOverlay(const QImage &frame, const QList<CompLabel> &labels, const QPolygonF &pcbQuad) const
{
    if (!ui || frame.isNull()) {
        return frame;
    }

    int thickness = 2;
    if (ui->sliderProgress) {
        thickness = std::max(1, ui->sliderProgress->value() / 10);
    }

    int opts = (kShowLabelText | kShowConfidence);
    if (ui->checkShowLabelText || ui->checkShowClassId || ui->checkShowConfidence) {
        opts = 0;
        if (ui->checkShowLabelText && ui->checkShowLabelText->isChecked()) opts |= kShowLabelText;
        if (ui->checkShowClassId && ui->checkShowClassId->isChecked()) opts |= kShowClassId;
        if (ui->checkShowConfidence && ui->checkShowConfidence->isChecked()) opts |= kShowConfidence;
    }

    cv::Mat bgr = qimageToBgrMat(frame);
    if (bgr.empty()) {
        return frame;
    }

    const int w = bgr.cols;
    const int h = bgr.rows;
    const double baseScale = std::clamp(std::min(w, h) / 1000.0, 0.5, 2.0);
    const double fontScale = baseScale * (0.85 + 0.12 * thickness);

    // Draw extraction quad if requested
    const bool drawQuad = (ui->usepcbextract && ui->usepcbextract->isChecked());
    if (drawQuad && pcbQuad.size() >= 4) {
        std::vector<cv::Point> poly;
        poly.reserve(static_cast<size_t>(pcbQuad.size()));
        for (const auto &p : pcbQuad) {
            poly.emplace_back(cv::Point(static_cast<int>(std::round(p.x())), static_cast<int>(std::round(p.y()))));
        }
        cv::polylines(bgr, poly, true, cv::Scalar(0, 0, 255), thickness, cv::LINE_AA);
    }

    for (const auto &l : labels) {
        const int x1 = std::clamp(static_cast<int>(std::round(l.x)), 0, w - 1);
        const int y1 = std::clamp(static_cast<int>(std::round(l.y)), 0, h - 1);
        const int x2 = std::clamp(static_cast<int>(std::round(l.x + l.w)), 0, w - 1);
        const int y2 = std::clamp(static_cast<int>(std::round(l.y + l.h)), 0, h - 1);
        if (x2 <= x1 + 1 || y2 <= y1 + 1) {
            continue;
        }

        const cv::Scalar color = classColorForBgr(l.cls);
        cv::rectangle(bgr, cv::Point(x1, y1), cv::Point(x2, y2), color, thickness, cv::LINE_AA);

        const QString text = buildLabelText(l, opts);
        if (!text.isEmpty()) {
            const std::string s = text.toStdString();
            const int textY = std::max(0, y1 - 8);
            cv::putText(bgr, s, cv::Point(x1, textY), cv::FONT_HERSHEY_SIMPLEX, fontScale, color, thickness, cv::LINE_AA);
        }
    }

    return bgrMatToQImage(bgr);
}

void ComponentsDetect::onYoloResultReady(const QImage &frame,
                                        const QList<CompLabel> &labels,
                                        const QPolygonF &pcbQuad,
                                        qint64 /*timestampMs*/,
                                        double inferMs)
{
    if (!ui) {
        return;
    }

    // Preview
    m_lastFrame = frame;
    m_lastPcbQuad = pcbQuad;
    m_lastLabels = labels;
    if (ui->graphicsViewPreview && !frame.isNull()) {
        ui->graphicsViewPreview->setImage(renderOverlay(frame, labels, pcbQuad));
    }

    // Left info
    if (ui->labelTimeValue) {
        ui->labelTimeValue->setText(QStringLiteral("%1 ms").arg(inferMs, 0, 'f', 1));
    }
    if (ui->labelCountValue) {
        ui->labelCountValue->setText(QString::number(labels.size()));
    }
    if (ui->labelPromptValue) {
        ui->labelPromptValue->setText(QStringLiteral("识别完成"));
    }

    // Table
    if (ui->tableResults) {
        ui->tableResults->setRowCount(0);
        ui->tableResults->setRowCount(labels.size());

        for (int i = 0; i < labels.size(); ++i) {
            const auto &l = labels.at(i);
            const int xmin = static_cast<int>(std::round(l.x));
            const int ymin = static_cast<int>(std::round(l.y));
            const int xmax = static_cast<int>(std::round(l.x + l.w));
            const int ymax = static_cast<int>(std::round(l.y + l.h));

            ui->tableResults->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
            ui->tableResults->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1").arg(l.id)));
            ui->tableResults->setItem(i, 2, new QTableWidgetItem(l.label));
            ui->tableResults->setItem(i, 3, new QTableWidgetItem(QString::number(l.confidence, 'f', 3)));
            ui->tableResults->setItem(i, 4, new QTableWidgetItem(QString::number(xmin)));
            ui->tableResults->setItem(i, 5, new QTableWidgetItem(QString::number(ymin)));
            ui->tableResults->setItem(i, 6, new QTableWidgetItem(QString::number(xmax)));
            ui->tableResults->setItem(i, 7, new QTableWidgetItem(QString::number(ymax)));
        }

        if (labels.size() > 0) {
            ui->tableResults->selectRow(0);
            updateSelectionDetails(&labels.first());
        } else {
            updateSelectionDetails(nullptr);
        }
    }
}

void ComponentsDetect::onTableSelectionChanged()
{
    if (!ui || !ui->tableResults) {
        return;
    }

    const int row = ui->tableResults->currentRow();
    if (row < 0 || row >= m_lastLabels.size()) {
        updateSelectionDetails(nullptr);
        return;
    }
    updateSelectionDetails(&m_lastLabels[row]);
}

void ComponentsDetect::updateSelectionDetails(const CompLabel *label)
{
    if (!ui) {
        return;
    }

    if (!label) {
        if (ui->labelClassValue) ui->labelClassValue->setText(QStringLiteral("-"));
        if (ui->labelConfidenceValue) ui->labelConfidenceValue->setText(QStringLiteral("-"));
        if (ui->labelXminValue) ui->labelXminValue->setText(QStringLiteral("-"));
        if (ui->labelYminValue) ui->labelYminValue->setText(QStringLiteral("-"));
        if (ui->labelXmaxValue) ui->labelXmaxValue->setText(QStringLiteral("-"));
        if (ui->labelYmaxValue) ui->labelYmaxValue->setText(QStringLiteral("-"));
        return;
    }

    const int xmin = static_cast<int>(std::round(label->x));
    const int ymin = static_cast<int>(std::round(label->y));
    const int xmax = static_cast<int>(std::round(label->x + label->w));
    const int ymax = static_cast<int>(std::round(label->y + label->h));

    if (ui->labelClassValue) ui->labelClassValue->setText(label->label);
    if (ui->labelConfidenceValue) ui->labelConfidenceValue->setText(QString::number(label->confidence, 'f', 3));
    if (ui->labelXminValue) ui->labelXminValue->setText(QString::number(xmin));
    if (ui->labelYminValue) ui->labelYminValue->setText(QString::number(ymin));
    if (ui->labelXmaxValue) ui->labelXmaxValue->setText(QString::number(xmax));
    if (ui->labelYmaxValue) ui->labelYmaxValue->setText(QString::number(ymax));
}
