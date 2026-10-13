// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "settings/sensorpreferences.h"
#include "sensors/sensortypes.h"

#include <QDialog>
#include <QHash>

class QTreeWidget;
class QLineEdit;
class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QGroupBox;

namespace qsensors {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(SensorPreferencesStore *store, const QList<SensorDescriptor> &catalog, QWidget *parent);
    void updateCatalog(const QList<SensorDescriptor> &catalog);
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
    void requestPreview();
    void filterSensors();
    void syncCurrentItem();
    void loadSensor(const QString &id);
    void chooseColor(bool background);
    void updateColorButtons();
    bool currentIsTemperature() const;
    double fromDisplay(double value, bool difference) const;
    double toDisplay(double value, bool difference) const;

    SensorPreferencesStore *m_store;
    QHash<QString, SensorPreferences> m_draft;
    QHash<QString, SensorDescriptor> m_catalog;
    QHash<QString, SensorSample> m_samples;
    QString m_readError;
    QString m_currentId;
    TemperatureUnit m_unit;
    bool m_loading = false;
    bool m_initialAutostart;
    QTreeWidget *m_list;
    QLineEdit *m_search;
    QGroupBox *m_editor;
    QLineEdit *m_label;
    QCheckBox *m_dashboard;
    QCheckBox *m_tray;
    QDoubleSpinBox *m_multiplier;
    QDoubleSpinBox *m_offset;
    QCheckBox *m_customRange;
    QDoubleSpinBox *m_lower;
    QDoubleSpinBox *m_upper;
    QPushButton *m_foreground;
    QPushButton *m_background;
    QComboBox *m_temperatureUnit;
    QCheckBox *m_autostart;
    QLabel *m_identity;
};

} // namespace qsensors
