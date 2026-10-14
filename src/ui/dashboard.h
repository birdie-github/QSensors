// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>
#include "sensors/sensortypes.h"
#include <QStringList>
#include <QTimer>

class QSpinBox;
class QAction;
class QLabel;
class QCloseEvent;
class QStackedWidget;
class QComboBox;
class QSizeGrip;
class QResizeEvent;

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
    void resizeEvent(QResizeEvent *event) override;

private:
    void chooseCustomTileSize();
    void updateVisibleIds();
    void saveWindowGeometry();
    void setControlsVisible(bool visible);
    void fitCompactView();
    void positionResizeGrips();
    void finishCompactResize();
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
    bool m_adjustingCompact = false;
    QSizeGrip *m_resizeGrips[4]{};
    QTimer m_resizeSaveTimer;
    bool m_resizeSavePending = false;
    QRect m_expandedGeometry;
    Qt::WindowStates m_expandedState;
    Qt::WindowFlags m_expandedFlags;
};

} // namespace qsensors
