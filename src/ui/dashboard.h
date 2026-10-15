// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>
#include "sensors/sensortypes.h"
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QTimer>

class QAction;
class QLabel;
class QCloseEvent;
class QStackedWidget;
class QSizeGrip;
class QResizeEvent;
class QMenu;

namespace qsensors {

class SensorTableModel;
class SensorGrid;
class AlarmController;
class SensorPreferencesStore;

class Dashboard final : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(SensorTableModel *model, SensorPreferencesStore *preferences, QWidget *parent = nullptr);
    ~Dashboard() override;
    void applySnapshot(const SensorSnapshot &snapshot);
    void setAlarmController(AlarmController *alarms);
    void clearReadings();
    void setIntervalSeconds(double seconds);
    void setTrayAvailable(bool available);
    QMenu *applicationSettingsMenu() const { return m_settingsMenu; }
    void setActions(QAction *hide, QAction *settings,
                    QAction *rescan, QAction *about, QAction *quit);

signals:
    void quitRequested();
    void intervalChanged(double seconds);
    void dashboardActiveChanged(bool active);
    void visibleSensorIdsChanged(const QStringList &ids);
    void sensorConfigureRequested(const QString &id);
    void sensorDisabled(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void createSettingsMenu();
    void updateStatusSummary();
    void chooseCustomTileSize();
    void updateVisibleIds();
    void updatePresentation(const QList<SensorSample> &samples, bool incremental);
    void refreshPresentation();
    void saveWindowGeometry();
    void setControlsVisible(bool visible);
    void fitCompactView();
    void positionResizeGrip();
    void finishCompactResize();
    SensorPreferencesStore *m_preferences;
    SensorTableModel *m_model;
    int m_activeAlarms = 0;
    double m_intervalSeconds = 5;
    QAction *m_intervalAction;
    QAction *m_snoozeAction;
    SensorGrid *m_grid;
    QStackedWidget *m_stack;
    QAction *m_tileSizeAction;
    QAction *m_tableView;
    QAction *m_dashboardView;
    QAction *m_controlsAction;
    QMenu *m_settingsMenu;
    QStringList m_catalogIds;
    QSet<QString> m_catalogSet;
    QHash<QString, SensorSample> m_dashboardSamples;
    bool m_gridDirty = true;
    bool m_tableDirty = true;
    QLabel *m_statusSummary;
    QLabel *m_trayWarning;
    bool m_trayAvailable = false;
    bool m_compact = false;
    bool m_adjustingCompact = false;
    QSizeGrip *m_resizeGrip = nullptr;
    QTimer m_resizeSaveTimer;
    bool m_resizeSavePending = false;
    int m_resizeInitialTileSize = 0;
    QSize m_compactChrome;
    QRect m_expandedGeometry;
    Qt::WindowStates m_expandedState;
    Qt::WindowFlags m_expandedFlags;
};

} // namespace qsensors
