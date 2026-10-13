// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>
#include "sensors/sensortypes.h"
#include <QStringList>

class QSpinBox;
class QAction;
class QLabel;
class QCloseEvent;
class QStackedWidget;
class QComboBox;

namespace qsensors {

class SensorTableModel;
class SensorGrid;
class SensorPreferencesStore;

class Dashboard final : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(SensorTableModel *model, SensorPreferencesStore *preferences, QWidget *parent = nullptr);
    ~Dashboard() override;
    void applySnapshot(const SensorSnapshot &snapshot);
    void clearReadings();
    void setIntervalSeconds(int seconds);
    void setTrayAvailable(bool available);
    void setActions(QAction *show, QAction *hide, QAction *settings,
                    QAction *rescan, QAction *about, QAction *quit);

signals:
    void quitRequested();
    void intervalChanged(int seconds);
    void dashboardActiveChanged(bool active);
    void visibleSensorIdsChanged(const QStringList &ids);
    void sensorConfigureRequested(const QString &id);
    void sensorDisabled(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateVisibleIds();
    void saveWindowGeometry();
    void setControlsVisible(bool visible);
    void fitCompactView();
    SensorPreferencesStore *m_preferences;
    QSpinBox *m_interval;
    SensorGrid *m_grid;
    QStackedWidget *m_stack;
    QComboBox *m_tileSize;
    QAction *m_tableView;
    QAction *m_controlsAction;
    QStringList m_catalogIds;
    QLabel *m_trayWarning;
    bool m_trayAvailable = false;
    bool m_compact = false;
    QRect m_expandedGeometry;
    Qt::WindowStates m_expandedState;
    Qt::WindowFlags m_expandedFlags;
};

} // namespace qsensors
