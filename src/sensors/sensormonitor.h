// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace qsensors {

class SensorWorker;

// GUI-side demand/scheduling. At most one request and one completion are in flight.
class SensorMonitor final : public QObject {
    Q_OBJECT
public:
    explicit SensorMonitor(QObject *parent = nullptr);
    ~SensorMonitor() override;
    int intervalSeconds() const { return m_intervalSeconds; }
    void setDashboardActive(bool active);
    void setBackgroundIds(QStringList ids);
    void setIntervalSeconds(int seconds);
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
    QStringList demandedIds() const;

    QThread m_thread;
    SensorWorker *m_worker;
    QTimer m_timer;
    QStringList m_ids;
    QStringList m_backgroundIds;
    bool m_active = false;
    bool m_busy = false;
    bool m_demandChanged = false;
    bool m_rescanPending = true;
    int m_intervalSeconds = 5;
    int m_retrySeconds = 10;
    qint64 m_nextRecovery = 0;
    qint64 m_lastCompletion = 0;
};

} // namespace qsensors
