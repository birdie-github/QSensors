// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/sensormonitor.h"
#include "sensors/lmsensorsprovider.h"
#include "system/systemsensorprovider.h"

#include <QSettings>

#include <algorithm>
#include <memory>

namespace qsensors {

class SensorWorker final : public QObject {
    Q_OBJECT
public:
    void collect(bool discover, bool readAll, const QStringList &requestedIds)
    {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        if (!m_provider)
            m_provider = std::make_unique<LmSensorsProvider>();
        SensorSnapshot snapshot;
        snapshot.rescanned = discover;
        QStringList ids = requestedIds;
        if (discover) {
            const auto discovery = m_provider->discover();
            snapshot.descriptors = discovery.descriptors;
            snapshot.error = discovery.error;
            snapshot.descriptors.append(m_system.discover().descriptors);
            if (readAll) {
                for (const auto &descriptor : snapshot.descriptors)
                    if (!ids.contains(descriptor.id))
                        ids.append(descriptor.id);
            }
        }
        QStringList hardwareIds, systemIds;
        for (const auto &id : ids) {
            if (id.startsWith(QStringLiteral("system/"))) systemIds.append(id);
            else hardwareIds.append(id);
        }
        // A libsensors failure must not suppress independent system readings.
        snapshot.samples = m_provider->readBatch(hardwareIds);
        snapshot.samples.append(m_system.readBatch(systemIds));
        emit finished(snapshot);
    }

signals:
    void finished(const qsensors::SensorSnapshot &snapshot);

private:
    std::unique_ptr<LmSensorsProvider> m_provider;
    SystemSensorProvider m_system;
};

SensorMonitor::SensorMonitor(QObject *parent) : QObject(parent), m_worker(new SensorWorker)
{
    qRegisterMetaType<SensorSnapshot>();
    QSettings settings;
    bool valid = false;
    const int interval = settings.value(QStringLiteral("Monitoring/intervalSeconds"), 5).toInt(&valid);
    if (valid && interval >= 1 && interval <= 3600)
        m_intervalSeconds = interval;
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SensorMonitor::request);
    m_worker->moveToThread(&m_thread);
    connect(this, &SensorMonitor::collectRequested, m_worker, &SensorWorker::collect, Qt::QueuedConnection);
    connect(m_worker, &SensorWorker::finished, this, &SensorMonitor::completed, Qt::QueuedConnection);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

SensorMonitor::~SensorMonitor()
{
    m_timer.stop();
    // Finish the current library call; cleanup runs in the worker's destructor.
    // Never terminate a thread while it owns libsensors state.
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}

void SensorMonitor::setDashboardActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    m_timer.stop();
    m_demandChanged = true;
    if (active || hasDemand())
        request();
}

void SensorMonitor::setDashboardIds(QStringList ids)
{
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (m_dashboardIdsExplicit && ids == m_dashboardIds)
        return;
    m_dashboardIdsExplicit = true;
    m_dashboardIds = ids;
    m_demandChanged = true;
    m_timer.stop();
    if (m_active && hasDemand())
        request();
}

void SensorMonitor::setBackgroundIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_backgroundIds)
        return;
    m_backgroundIds = ids;
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand())
        request();
}

void SensorMonitor::setPreviewIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_previewIds)
        return;
    m_previewIds = ids;
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand())
        request();
}

bool SensorMonitor::hasDemand() const
{
    return (m_active && (!m_dashboardIdsExplicit || !m_dashboardIds.isEmpty() || m_ids.isEmpty()))
        || !m_backgroundIds.isEmpty() || !m_previewIds.isEmpty();
}

QStringList SensorMonitor::demandedIds() const
{
    QStringList ids = m_active ? (m_dashboardIdsExplicit ? m_dashboardIds : m_ids) : QStringList();
    for (const auto &id : m_backgroundIds) {
        if (!ids.contains(id))
            ids.append(id);
    }
    for (const auto &id : m_previewIds) {
        if (!ids.contains(id))
            ids.append(id);
    }
    return ids;
}

void SensorMonitor::setIntervalSeconds(int seconds)
{
    seconds = std::clamp(seconds, 1, 3600);
    if (seconds == m_intervalSeconds)
        return;
    m_intervalSeconds = seconds;
    QSettings().setValue(QStringLiteral("Monitoring/intervalSeconds"), seconds);
    schedule();
}

void SensorMonitor::rescan()
{
    m_rescanPending = true; // Repeated requests coalesce into one follow-up.
    m_timer.stop();
    if (!m_busy)
        request();
}

void SensorMonitor::request()
{
    m_timer.stop();
    if (m_busy || (!hasDemand() && !m_rescanPending))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // A delayed timer after suspend (or showing a long-hidden window) rescans.
    if (m_lastCompletion && now - m_lastCompletion > std::max(30000, m_intervalSeconds * 3000))
        m_rescanPending = true;
    if (m_nextRecovery && now >= m_nextRecovery)
        m_rescanPending = true;
    const bool discover = m_rescanPending;
    m_rescanPending = false;
    m_busy = true;
    m_demandChanged = false;
    if (discover)
        emit statusChanged(tr("Scanning sensors…"));
    emit collectRequested(discover, discover && m_active && !m_dashboardIdsExplicit,
                          demandedIds());
}

void SensorMonitor::completed(const SensorSnapshot &snapshot)
{
    m_busy = false;
    m_lastCompletion = QDateTime::currentMSecsSinceEpoch();
    if (snapshot.rescanned) {
        m_ids.clear();
        for (const auto &descriptor : snapshot.descriptors)
            m_ids.append(descriptor.id);
    }
    emit snapshotReady(snapshot);
    const auto failures = std::count_if(snapshot.samples.cbegin(), snapshot.samples.cend(),
                                       [](const auto &sample) { return !sample.available; });
    const bool needsRecovery = !snapshot.error.isEmpty() || m_ids.isEmpty() || failures > 0;
    if (!snapshot.error.isEmpty())
        emit statusChanged(snapshot.error);
    else if (m_ids.isEmpty())
        emit statusChanged(tr("No supported readable sensors found. Use Rescan after hardware changes."));
    else if (failures)
        emit statusChanged(tr("%1 channels; %2 unavailable").arg(m_ids.size()).arg(failures));
    else
        emit statusChanged(tr("%1 channels").arg(m_ids.size()));

    if (needsRecovery) {
        if (snapshot.rescanned || !m_nextRecovery) {
            m_nextRecovery = m_lastCompletion + m_retrySeconds * 1000;
            m_retrySeconds = std::min(m_retrySeconds * 2, 300);
        }
    } else {
        m_nextRecovery = 0;
        m_retrySeconds = 10;
    }
    if (hasDemand() && (m_rescanPending
        || (snapshot.error.isEmpty() && (m_demandChanged
            || (snapshot.samples.isEmpty() && !m_ids.isEmpty() && !demandedIds().isEmpty()))))) {
        request();
        return;
    }
    schedule();
}

void SensorMonitor::schedule()
{
    m_timer.stop();
    if (!hasDemand() || m_busy)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int delay = m_intervalSeconds * 1000;
    if (m_nextRecovery) {
        const int recoveryDelay = static_cast<int>(std::clamp<qint64>(m_nextRecovery - now, 1000, 300000));
        delay = m_ids.isEmpty() ? recoveryDelay : std::min(delay, recoveryDelay);
    }
    m_timer.start(delay);
}

} // namespace qsensors

#include "sensormonitor.moc"
