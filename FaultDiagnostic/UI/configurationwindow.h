#ifndef CONFIGURATIONWINDOW_H
#define CONFIGURATIONWINDOW_H

#include <QWidget>
#include <QMap>
#include <QVector>

namespace Ui {
class ConfigurationWindow;
}

class QTableWidgetItem;
class QWidget;

class TPSPluginManager;
class TPSPluginInterface;
class TestSequenceManager;
struct TPSParamDefinition;

class ConfigurationWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigurationWindow(QWidget *parent = nullptr);
    ~ConfigurationWindow();

    void setSequenceManager(TestSequenceManager *manager);
    TestSequenceManager *sequenceManager() const;

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onAddItem();
    void onRemoveItem();
    void onImportSequence();
    void onExportSequence();
    void onSelectionChanged();

private:
    void loadPlugins();
    void refreshTable();
    void refreshDetail(int index);
    void clearDetail();
    QWidget *createParamEditor(const TPSParamDefinition &def, const QVariant &value, QWidget *parent = nullptr);
    QVariant readParamValue(const TPSParamDefinition &def, QWidget *editor) const;
    QMap<QString, QVariant> collectParams(const QVector<TPSParamDefinition> &defs, const QMap<QString, QWidget *> &editors) const;
    void applyThemeQss();

    Ui::ConfigurationWindow *ui;
    TestSequenceManager *m_manager = nullptr;
    TPSPluginManager *m_pluginManager = nullptr;
    QMap<QString, TPSPluginInterface *> m_plugins;
    QMap<QString, QWidget *> m_detailEditors;
    int m_currentIndex = -1;
    bool m_updatingDetail = false;
    bool m_applyingQss = false;
};

#endif // CONFIGURATIONWINDOW_H
