// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QWidget;

namespace qsensors {

class SensorPreferencesStore;

class TrayController final : public QObject {
    Q_OBJECT
public:
    explicit TrayController(QWidget *window, SensorPreferencesStore *preferences, QObject *parent = nullptr);
    ~TrayController() override;
    void start();
    void applySnapshot(const SensorSnapshot &snapshot);
    void openSettings();
    void setDashboardVisible(bool visible);
    QStringList selectedIds() const { return m_selected; }
    QAction *showAction() const { return m_showAction; }
    QAction *hideAction() const { return m_hideAction; }
    QAction *settingsAction() const { return m_settingsAction; }
    QAction *rescanAction() const { return m_rescanAction; }
    QAction *aboutAction() const { return m_aboutAction; }
    QAction *quitAction() const { return m_quitAction; }

signals:
    void showRequested();
    void hideRequested();
    void toggleRequested();
    void rescanRequested();
    void quitRequested();
    void availabilityChanged(bool available);
    void selectionChanged(const QStringList &ids);
    void settingsRequested();
    void sensorSettingsRequested(const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        QSystemTrayIcon *icon = nullptr;
        QMenu *menu = nullptr; // Owns this sensor's reading action.
        QAction *reading = nullptr;
        QString renderKey;
        QString tooltip;
    };
    void checkAvailability();
    void setSelectedIds(QStringList ids);
    void reconcileIcons();
    void updateEntry(const QString &id, Entry &entry);
    QSystemTrayIcon *createIcon();

    QWidget *m_window;
    SensorPreferencesStore *m_preferences;
    std::unique_ptr<QMenu> m_menu;
    QAction *m_showAction;
    QAction *m_hideAction;
    QAction *m_settingsAction;
    QAction *m_rescanAction;
    QAction *m_aboutAction;
    QAction *m_quitAction;
    QSystemTrayIcon *m_fallback = nullptr;
    QMap<QString, Entry> m_entries;
    QStringList m_selected;
    QHash<QString, SensorDescriptor> m_descriptors;
    QHash<QString, SensorSample> m_samples;
    QTimer m_hostWatch;
    bool m_available = false;
    bool m_dashboardVisible = true;
};

} // namespace qsensors
