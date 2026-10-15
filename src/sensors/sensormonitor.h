// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>

namespace qsensors {

class SensorWorker;
class SensorPreferencesStore;

// GUI-side demand/scheduling. At most one request and one completion are in flight.
class SensorMonitor final : public QObject {
    Q_OBJECT
public:
    explicit SensorMonitor(SensorPreferencesStore *preferences, QObject *parent = nullptr);
    ~SensorMonitor() override;
    double intervalSeconds() const { return m_intervalSeconds; }
    void setDashboardActive(bool active);
    void setDashboardIds(QStringList ids);
    void setBackgroundIds(QStringList ids);
    void setPreviewIds(QStringList ids);
    void setIntervalSeconds(double seconds);
    void rescan();

signals:
    void snapshotReady(const qsensors::SensorSnapshot &snapshot);
    void statusChanged(const QString &text);
    void collectRequested(bool discover, bool readAll, const QStringList &ids);

private:
    void request();
    void completed(const SensorSnapshot &snapshot);
    void schedule();
    bool hasDemand() const;
    const QStringList &demandedIds() const;
    void rebuildDemand();

    const QStringList &dashboardDemand() const;
    qint64 intervalMilliseconds(const QString &id, bool tray) const;
    qint64 globalIntervalMilliseconds() const;
    qint64 acquisitionMilliseconds(const QString &id) const;
    void resetSchedule();
    void pruneDeadlines();

    SensorPreferencesStore *m_preferences;
    QElapsedTimer m_clock;
    QHash<QString, qint64> m_acquisitionDue;
    QHash<QString, qint64> m_acquisitionPeriods;
    QHash<QString, qint64> m_dashboardDue;
    QHash<QString, qint64> m_trayDue;
    QHash<QString, qint64> m_previewDue;
    QSet<QString> m_failedIds;
    QString m_discoveryError;
    QStringList m_requestedIds;
    QStringList m_dashboardRefreshIds;
    QStringList m_trayRefreshIds;
    QThread m_thread;
    SensorWorker *m_worker;
    QTimer m_timer;
    QStringList m_ids;
    QStringList m_backgroundIds;
    QStringList m_previewIds;
    QStringList m_dashboardIds;
    QStringList m_dashboardDemand;
    QStringList m_demandIds;
    QSet<QString> m_dashboardSet;
    QSet<QString> m_backgroundSet;
    QSet<QString> m_previewSet;
    QSet<QString> m_demandSet;
    bool m_dashboardIdsExplicit = false;
    bool m_active = false;
    bool m_busy = false;
    bool m_demandChanged = false;
    bool m_rescanPending = true;
    double m_intervalSeconds = 5;
    int m_retrySeconds = 10;
    qint64 m_nextRecovery = 0;
    qint64 m_lastCompletion = 0;
    qint64 m_lastWallCompletion = 0;
    int m_scheduledDelay = 0;
};

} // namespace qsensors
