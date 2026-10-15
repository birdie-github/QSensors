// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/sensormonitor.h"
#include "sensors/lmsensorsprovider.h"
#include "system/systemsensorprovider.h"
#include "system/disksensorprovider.h"
#include "settings/sensorpreferences.h"

#include <QSettings>

#include <algorithm>
#include <cmath>
#include <limits>
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
            snapshot.descriptors.append(m_disks.discover().descriptors);
            if (readAll) {
                QSet<QString> known(ids.cbegin(), ids.cend());
                for (const auto &descriptor : snapshot.descriptors) {
                    if (known.contains(descriptor.id)) continue;
                    known.insert(descriptor.id);
                    ids.append(descriptor.id);
                }
            }
        }
        QStringList hardwareIds, systemIds, diskIds;
        for (const auto &id : ids) {
            if (id.startsWith(QStringLiteral("system/disk/"))) diskIds.append(id);
            else if (id.startsWith(QStringLiteral("system/"))) systemIds.append(id);
            else hardwareIds.append(id);
        }
        // A libsensors failure must not suppress independent system readings.
        snapshot.samples = m_provider->readBatch(hardwareIds);
        snapshot.samples.append(m_system.readBatch(systemIds));
        snapshot.samples.append(m_disks.readBatch(diskIds));
        emit finished(snapshot);
    }

signals:
    void finished(const qsensors::SensorSnapshot &snapshot);

private:
    std::unique_ptr<LmSensorsProvider> m_provider;
    SystemSensorProvider m_system;
    DiskSensorProvider m_disks;
};

SensorMonitor::SensorMonitor(SensorPreferencesStore *preferences, QObject *parent)
    : QObject(parent), m_preferences(preferences), m_worker(new SensorWorker)
{
    m_clock.start();
    connect(preferences, &SensorPreferencesStore::changed, this, &SensorMonitor::resetSchedule);
    qRegisterMetaType<SensorSnapshot>();
    QSettings settings;
    bool valid = false;
    const double interval = settings.value(QStringLiteral("Monitoring/intervalSeconds"), 5).toDouble(&valid);
    if (valid && std::isfinite(interval))
        m_intervalSeconds = std::round(std::clamp(interval, minimumGlobalRefreshSeconds,
                                                 maximumGlobalRefreshSeconds) * 100) / 100;
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SensorMonitor::request);
    m_worker->moveToThread(&m_thread);
    connect(this, &SensorMonitor::collectRequested, m_worker, &SensorWorker::collect, Qt::QueuedConnection);
    connect(m_worker, &SensorWorker::finished, this, &SensorMonitor::completed, Qt::QueuedConnection);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    rebuildDemand();
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
    rebuildDemand();
    pruneDeadlines();
    m_timer.stop();
    m_demandChanged = true;
    if (active || hasDemand()) request();
    else schedule();
}

void SensorMonitor::setDashboardIds(QStringList ids)
{
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (m_dashboardIdsExplicit && ids == m_dashboardIds)
        return;
    m_dashboardIdsExplicit = true;
    m_dashboardIds = ids;
    rebuildDemand();
    pruneDeadlines();
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand()) request();
    else schedule();
}

void SensorMonitor::setBackgroundIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_backgroundIds)
        return;
    m_backgroundIds = ids;
    rebuildDemand();
    pruneDeadlines();
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand()) request();
    else schedule();
}

void SensorMonitor::setPreviewIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    if (ids == m_previewIds)
        return;
    m_previewIds = ids;
    rebuildDemand();
    pruneDeadlines();
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand()) request();
    else schedule();
}

const QStringList &SensorMonitor::dashboardDemand() const
{
    return m_dashboardDemand;
}

bool SensorMonitor::hasDemand() const
{
    // An empty enabled dashboard is not a periodic acquisition consumer.
    return !m_demandIds.isEmpty();
}

const QStringList &SensorMonitor::demandedIds() const
{
    return m_demandIds;
}

void SensorMonitor::rebuildDemand()
{
    // Rebuild only when consumers or the discovered catalog change. Preserve
    // dashboard/tray/preview order while indexing membership once.
    m_dashboardDemand = m_active ? (m_dashboardIdsExplicit ? m_dashboardIds : m_ids) : QStringList();
    m_dashboardSet = QSet<QString>(m_dashboardDemand.cbegin(), m_dashboardDemand.cend());
    m_backgroundSet = QSet<QString>(m_backgroundIds.cbegin(), m_backgroundIds.cend());
    m_previewSet = QSet<QString>(m_previewIds.cbegin(), m_previewIds.cend());
    m_demandIds = m_dashboardDemand;
    m_demandSet = m_dashboardSet;
    const auto append = [this](const QStringList &ids) {
        for (const auto &id : ids) {
            if (m_demandSet.contains(id)) continue;
            m_demandSet.insert(id);
            m_demandIds.append(id);
        }
    };
    append(m_backgroundIds);
    append(m_previewIds);
    m_alarmSet.clear();
    const auto values = m_preferences->values();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it->alarm.enabled()) m_alarmSet.insert(it.key());
    }
    append(m_alarmSet.values());
}

qint64 SensorMonitor::globalIntervalMilliseconds() const
{
    return static_cast<qint64>(std::llround(m_intervalSeconds * 1000));
}

qint64 SensorMonitor::intervalMilliseconds(const QString &id, bool tray) const
{
    const auto p = m_preferences->value(id);
    const double seconds = (tray ? p.trayUseGlobal : p.dashboardUseGlobal) ? m_intervalSeconds
        : (tray ? p.trayInterval : p.dashboardInterval);
    return std::max<qint64>(50, static_cast<qint64>(std::llround(seconds * 1000)));
}

qint64 SensorMonitor::acquisitionMilliseconds(const QString &id) const
{
    qint64 period = std::numeric_limits<qint64>::max();
    if (m_dashboardSet.contains(id)) period = std::min(period, intervalMilliseconds(id, false));
    if (m_backgroundSet.contains(id)) period = std::min(period, intervalMilliseconds(id, true));
    if (m_alarmSet.contains(id)) period = std::min(period, globalIntervalMilliseconds());
    if (m_previewSet.contains(id)) period = std::min(period, globalIntervalMilliseconds());
    return period;
}

void SensorMonitor::pruneDeadlines()
{
    const auto prune = [](QHash<QString, qint64> &deadlines, const QSet<QString> &ids) {
        for (auto it = deadlines.begin(); it != deadlines.end();) {
            if (!ids.contains(it.key())) it = deadlines.erase(it);
            else ++it;
        }
    };
    const auto &ids = demandedIds();
    prune(m_acquisitionDue, m_demandSet);
    prune(m_acquisitionPeriods, m_demandSet);
    for (const auto &id : ids) {
        const qint64 period = acquisitionMilliseconds(id);
        if (m_acquisitionPeriods.value(id, 0) != period) m_acquisitionDue.remove(id);
        m_acquisitionPeriods[id] = period;
    }
    prune(m_dashboardDue, m_dashboardSet);
    prune(m_trayDue, m_backgroundSet);
    prune(m_previewDue, m_previewSet);
    for (auto it = m_failedIds.begin(); it != m_failedIds.end();) {
        if (!m_demandSet.contains(*it)) it = m_failedIds.erase(it);
        else ++it;
    }
}

void SensorMonitor::resetSchedule()
{
    rebuildDemand();
    m_acquisitionDue.clear();
    m_acquisitionPeriods.clear();
    m_dashboardDue.clear();
    m_trayDue.clear();
    m_previewDue.clear();
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand()) request();
}

void SensorMonitor::setIntervalSeconds(double seconds)
{
    if (!std::isfinite(seconds)) return;
    seconds = std::round(std::clamp(seconds, minimumGlobalRefreshSeconds,
                                    maximumGlobalRefreshSeconds) * 100) / 100;
    if (seconds == m_intervalSeconds)
        return;
    m_intervalSeconds = seconds;
    QSettings().setValue(QStringLiteral("Monitoring/intervalSeconds"), seconds);
    // Only global consumers move to the new cadence. Custom presentations keep
    // their deadlines; acquisition follows the new fastest active requirement.
    for (const auto &id : m_dashboardDue.keys()) {
        if (m_preferences->value(id).dashboardUseGlobal) m_dashboardDue.remove(id);
    }
    for (const auto &id : m_trayDue.keys()) {
        if (m_preferences->value(id).trayUseGlobal) m_trayDue.remove(id);
    }
    m_previewDue.clear();
    pruneDeadlines();
    m_demandChanged = true;
    m_timer.stop();
    if (hasDemand()) request();
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
    if (m_busy || (!hasDemand() && !m_rescanPending)) return;
    m_demandChanged = false;
    pruneDeadlines();
    const qint64 now = m_clock.elapsed();
    const qint64 wallNow = QDateTime::currentMSecsSinceEpoch();
    // Preserve recovery after suspend/long inactivity without mistaking a slow
    // configured interval for a delayed timer.
    if (m_lastWallCompletion && wallNow - m_lastWallCompletion > std::max(30000, m_scheduledDelay + 30000))
        m_rescanPending = true;
    if (m_nextRecovery && now >= m_nextRecovery) m_rescanPending = true;
    const bool discover = m_rescanPending;
    m_rescanPending = false;
    m_requestedIds.clear();
    m_dashboardRefreshIds.clear();
    m_trayRefreshIds.clear();
    bool previewDue = false;
    const auto due = [this, now, discover, &previewDue](const QStringList &ids, QHash<QString, qint64> &deadlines,
                                          QStringList *refresh, bool tray, bool preview) {
        for (const auto &id : ids) {
            if (!discover && deadlines.value(id, 0) > now) continue;
            const qint64 period = preview ? globalIntervalMilliseconds() : intervalMilliseconds(id, tray);
            // Skip missed ticks; a slow acquisition never queues catch-up reads.
            deadlines[id] = now + period;
            if (refresh) refresh->append(id);
            if (preview) previewDue = true;
        }
    };
    due(dashboardDemand(), m_dashboardDue, &m_dashboardRefreshIds, false, false);
    due(m_backgroundIds, m_trayDue, &m_trayRefreshIds, true, false);
    due(m_previewIds, m_previewDue, nullptr, false, true);
    // Acquire once at the fastest active cadence. A slower presentation may
    // refresh between acquisitions using the same provider cache.
    for (const auto &id : demandedIds()) {
        if (discover || m_acquisitionDue.value(id, 0) <= now) {
            m_acquisitionDue[id] = now + m_acquisitionPeriods.value(id);
            m_requestedIds.append(id);
        }
    }
    if (!discover && m_requestedIds.isEmpty()) {
        if (previewDue || !m_dashboardRefreshIds.isEmpty() || !m_trayRefreshIds.isEmpty()) {
            SensorSnapshot refresh;
            refresh.incremental = true;
            refresh.error = m_discoveryError;
            refresh.dashboardRefreshIds = m_dashboardRefreshIds;
            refresh.trayRefreshIds = m_trayRefreshIds;
            m_busy = true;
            m_demandChanged = false;
            emit snapshotReady(refresh);
            m_busy = false;
        }
        if (m_rescanPending || m_demandChanged) { request(); return; }
        schedule();
        return;
    }
    m_busy = true;
    m_demandChanged = false;
    if (discover) emit statusChanged(tr("Scanning sensors…"));
    emit collectRequested(discover, discover && m_active && !m_dashboardIdsExplicit, m_requestedIds);
}

void SensorMonitor::completed(const SensorSnapshot &snapshot)
{
    m_lastCompletion = m_clock.elapsed();
    m_lastWallCompletion = QDateTime::currentMSecsSinceEpoch();
    if (snapshot.rescanned) {
        m_discoveryError = snapshot.error;
        m_ids.clear();
        for (const auto &descriptor : snapshot.descriptors)
            m_ids.append(descriptor.id);
        rebuildDemand();
    }
    auto batch = snapshot;
    batch.incremental = !snapshot.rescanned;
    batch.dashboardRefreshIds = m_dashboardRefreshIds;
    batch.trayRefreshIds = m_trayRefreshIds;
    QSet<QString> returned;
    returned.reserve(batch.samples.size() + m_requestedIds.size());
    for (const auto &sample : batch.samples) returned.insert(sample.id);
    for (const auto &id : m_requestedIds) {
        if (!returned.contains(id)) {
            returned.insert(id);
            SensorSample sample;
            sample.id = id;
            sample.error = tr("No reading returned");
            batch.samples.append(sample);
        }
    }
    batch.error = m_discoveryError;
    // Keep busy while synchronous listeners update demand/settings. They can
    // request a follow-up without overwriting this batch's scheduling metadata.
    emit snapshotReady(batch);
    for (const auto &sample : batch.samples) {
        // Interval sensors need two counter samples. A first pending value is not
        // a discovery failure, especially with a deliberately long interval.
        const bool primingCpu = sample.id == QStringLiteral("system/cpu/activity")
            && sample.error == QStringLiteral("Waiting for a valid CPU counter interval");
        const bool primingDisk = sample.id.startsWith(QStringLiteral("system/disk/"))
            && sample.error == QStringLiteral("Waiting for a valid disk counter interval");
        const bool primingPressure = sample.id.startsWith(QStringLiteral("system/pressure/"))
            && sample.error == QStringLiteral("Waiting for a valid pressure counter interval");
        if (sample.available || primingCpu || primingDisk || primingPressure) m_failedIds.remove(sample.id);
        else m_failedIds.insert(sample.id);
    }
    pruneDeadlines();
    const auto failures = m_failedIds.size();
    const bool needsRecovery = !m_discoveryError.isEmpty() || m_ids.isEmpty() || failures > 0;
    if (!m_discoveryError.isEmpty())
        emit statusChanged(m_discoveryError);
    else if (m_ids.isEmpty())
        emit statusChanged(tr("No supported readable sensors found. Use Rescan after hardware changes."));
    else if (failures)
        emit statusChanged(tr("%1 sensors unavailable").arg(failures));
    else
        emit statusChanged(QString());

    if (needsRecovery) {
        if (snapshot.rescanned || !m_nextRecovery) {
            m_nextRecovery = m_lastCompletion + m_retrySeconds * 1000;
            m_retrySeconds = std::min(m_retrySeconds * 2, 300);
        }
    } else {
        m_nextRecovery = 0;
        m_retrySeconds = 10;
    }
    m_busy = false;
    if (m_rescanPending || (hasDemand() && m_demandChanged)) {
        request();
        return;
    }
    schedule();
}

void SensorMonitor::schedule()
{
    m_timer.stop();
    if (!hasDemand() || m_busy) return;
    const qint64 now = m_clock.elapsed();
    qint64 next = std::numeric_limits<qint64>::max();
    const auto earliest = [&next](const QStringList &ids, const QHash<QString, qint64> &deadlines) {
        for (const auto &id : ids) next = std::min(next, deadlines.value(id, 0));
    };
    earliest(demandedIds(), m_acquisitionDue);
    earliest(dashboardDemand(), m_dashboardDue);
    earliest(m_backgroundIds, m_trayDue);
    earliest(m_previewIds, m_previewDue);
    if (m_nextRecovery) {
        // When discovery failed, retain the existing bounded backoff.
        next = m_ids.isEmpty() ? m_nextRecovery : std::min(next, m_nextRecovery);
    }
    // One lightweight timer; one worker batch for the union of due consumers.
    m_scheduledDelay = static_cast<int>(std::clamp<qint64>(next - now, 1, 3600000));
    m_timer.start(m_scheduledDelay);
}

} // namespace qsensors

#include "sensormonitor.moc"
