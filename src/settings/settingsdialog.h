// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "settings/sensorpreferences.h"
#include "sensors/sensortypes.h"

#include <QDialog>
#include <QHash>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QGroupBox;
class QTabWidget;
class QRadioButton;
class QFontComboBox;

namespace qsensors {
class DecimalSpinBox;

class AlarmEditor;
class AlarmController;

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(SensorPreferencesStore *store, const QList<SensorDescriptor> &catalog, QWidget *parent);
    void setAlarmController(AlarmController *alarms);
    void updateCatalog(const QList<SensorDescriptor> &catalog);
    void selectSensor(const QString &id);
    void selectTraySensor(const QString &id);
    void disableDashboardSensor(const QString &id);
    void applySnapshot(const SensorSnapshot &snapshot);

signals:
    void previewIdsChanged(const QStringList &ids);

protected:
    void accept() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void populate();
    void updateValues();
    void updateValue(QTreeWidgetItem *item);
    void requestPreview();
    void filterSensors();
    void syncCurrentItem();
    void loadSensor(const QString &id);
    struct PresentationControls {
        QWidget *page;
        QWidget *options;
        QCheckBox *enabled;
        QRadioButton *global;
        QRadioButton *custom;
        DecimalSpinBox *interval;
        QPushButton *foreground;
        QPushButton *background;
        QCheckBox *shadow;
        QRadioButton *solid;
        QRadioButton *transparent;
    };
    PresentationControls createPresentationPage(bool tray);
    void loadPresentation(const PresentationControls &controls, bool tray);
    void updatePresentationControls();
    bool applyChanges();
    void chooseColor(bool background, bool tray = false);
    void updateColorButtons();
    bool currentIsTemperature() const;
    double fromDisplay(double value, bool difference) const;
    double toDisplay(double value, bool difference) const;

    AlarmEditor *m_alarmEditor;
    AlarmController *m_alarms = nullptr;
    SensorPreferencesStore *m_store;
    QHash<QString, SensorPreferences> m_draft;
    QHash<QString, SensorDescriptor> m_catalog;
    QHash<QString, SensorSample> m_samples;
    QString m_readError;
    QString m_currentId;
    TemperatureUnit m_unit;
    bool m_loading = false;
    QTreeWidget *m_list;
    QHash<QString, QTreeWidgetItem *> m_valueItems;
    QLineEdit *m_search;
    QGroupBox *m_editor;
    QLineEdit *m_label;
    QTabWidget *m_sensorTabs;
    PresentationControls m_dashboardControls;
    PresentationControls m_trayControls;
    QCheckBox *m_gauge;
    QRadioButton *m_normalText;
    QRadioButton *m_digitalText;
    QGroupBox *m_gaugeRange;
    QGroupBox *m_trayTextStyle;
    QRadioButton *m_trayNormal;
    QRadioButton *m_trayDigital;
    QWidget *m_trayFontControls;
    QFontComboBox *m_trayFont;
    QCheckBox *m_trayShowUnit;
    DecimalSpinBox *m_multiplier;
    DecimalSpinBox *m_offset;
    QCheckBox *m_customRange;
    DecimalSpinBox *m_lower;
    DecimalSpinBox *m_upper;
    QComboBox *m_memoryDisplay;
    QLabel *m_identity;
};

} // namespace qsensors
