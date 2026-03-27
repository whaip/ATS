#ifndef CONFIGURATIONWINDOW_H
#define CONFIGURATIONWINDOW_H

#include <QWidget>
#include <QMap>
#include <QVector>
#include <QStringList>

namespace Ui {
class ConfigurationWindow;
}

class QTableWidgetItem;
class QWidget;
class QTableWidget;
class QToolButton;

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
    int currentIndex() const;

protected:
    void changeEvent(QEvent *event) override;

signals:
    void startTestRequested(int index);

private slots:
    void onAddItem();
    void onRemoveItem();
    void onImportSequence();
    void onExportSequence();
    void onSelectionChanged();
    void onStartTest();
    void onManageBindings();
    void onAddBindingType();
    void onRemoveBindingType();
    void onBindingCellChanged(int row, int column);
    void onBindingPluginChanged(int row);

private:
    void loadPlugins();
    void loadComponentBindings();
    bool saveComponentBindings(QString *errorMessage = nullptr) const;
    QString normalizedComponentType(const QString &value) const;
    QString suggestedPluginForType(const QString &componentType) const;
    QString pluginDisplayName(const QString &pluginId) const;
    void setupBindingEditorUi();
    void rebuildBindingEditorUi();
    bool persistBindingsFromEditor(QString *errorMessage = nullptr);
    bool isBuiltInTypeName(const QString &typeName) const;
    void refreshTable();
    QWidget *createParamEditor(const TPSParamDefinition &def, const QVariant &value, QWidget *parent = nullptr);
    QVariant readParamValue(const TPSParamDefinition &def, QWidget *editor) const;
    QMap<QString, QVariant> collectParams(const QVector<TPSParamDefinition> &defs, const QMap<QString, QWidget *> &editors) const;
    void applyThemeQss();

    Ui::ConfigurationWindow *ui;
    TestSequenceManager *m_manager = nullptr;
    TPSPluginManager *m_pluginManager = nullptr;
    QMap<QString, TPSPluginInterface *> m_plugins;
    QStringList m_componentTypes;
    QMap<QString, QString> m_componentPluginBindings;
    QTableWidget *m_bindingTable = nullptr;
    QToolButton *m_bindingAddButton = nullptr;
    QToolButton *m_bindingRemoveButton = nullptr;
    bool m_updatingBindingEditor = false;
    int m_currentIndex = -1;
    bool m_applyingQss = false;
};

#endif // CONFIGURATIONWINDOW_H
