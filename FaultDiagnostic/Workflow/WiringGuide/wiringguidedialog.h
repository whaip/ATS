#ifndef WIRINGGUIDEDIALOG_H
#define WIRINGGUIDEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <QVector>

#include "../../../ComponentsDetect/componenttypes.h"

namespace Ui {
class WiringGuideDialog;
}

struct TaskWiringGuideItem {
    QString componentRef;
    QStringList wiringSteps;
    QStringList wiringFocusTargets;
    QStringList roiSteps;
    QStringList roiFocusTargets;
    QString temperatureGuide;
};

class WiringGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WiringGuideDialog(QWidget *parent = nullptr);
    ~WiringGuideDialog() override;
    void setItems(const QVector<TaskWiringGuideItem> &items);
    void setGuidanceImage(const QImage &image);
    void setGuidanceLabels(const QList<CompLabel> &labels, bool clearExisting = true);
    QRectF selectedRoi() const;
    QVector<QRectF> selectedRois() const;
    bool hasSelectedRoi() const;

protected:
    void accept() override;

private:
    enum class StepStatus {
        NotStarted,
        InProgress,
        Completed
    };

    struct GuidedStep {
        QString text;
        QString focusTarget;
        bool isRoiStep = false;
        StepStatus status = StepStatus::NotStarted;
    };

    void onNextStep();
    void onSelectRoi();
    void refreshStepList();
    void refreshProgress();
    void refreshRoiStatus();
    void focusOnCurrentStep();
    bool allCompleted() const;

    Ui::WiringGuideDialog *ui = nullptr;
    class LebalItemManager *m_labelManager = nullptr;
    QVector<GuidedStep> m_steps;
    int m_currentStep = -1;
    QRectF m_selectedRoi;
    QVector<QRectF> m_selectedRois;
    bool m_hasSelectedRoi = false;
};

#endif // WIRINGGUIDEDIALOG_H
