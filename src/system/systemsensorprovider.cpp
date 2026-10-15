// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemsensorprovider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {
namespace {
QByteArray read(const QString &path, qint64 limit = 4 * 1024 * 1024)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.read(limit + 1);
    return file.error() == QFile::NoError && bytes.size() <= limit ? bytes : QByteArray();
}
struct PressureSensor { const char *resource; const char *label; };
constexpr std::array<PressureSensor, 3> pressureSensors = {{
    {"cpu", "CPU Pressure"}, {"memory", "Memory Pressure"}, {"io", "I/O Pressure"}
}};
QString pressureId(const PressureSensor &sensor)
{
    return QStringLiteral("system/pressure/") + QString::fromLatin1(sensor.resource);
}
QString cpuPath(int cpu) { return QStringLiteral("/sys/devices/system/cpu/cpu%1/").arg(cpu); }
bool online(int cpu)
{
    const QString path = cpuPath(cpu) + QStringLiteral("online");
    // Non-hotpluggable CPUs need not expose this attribute.
    return !QFile::exists(path) || read(path).trimmed() == "1";
}

// Keep ranges rather than expanding the kernel CPU list into an unbounded set.
using CpuRanges = QList<QPair<int, int>>;
std::optional<CpuRanges> cpuRanges(const QByteArray &mask)
{
    if (mask.isEmpty()) return {};
    CpuRanges ranges;
    int previous = -1;
    for (const auto &part : mask.split(',')) {
        const auto ends = part.split('-');
        if (ends.isEmpty() || ends.size() > 2) return {};
        // Accept only the kernel's decimal list syntax, not signs or whitespace.
        for (const auto &end : ends) {
            if (end.isEmpty()) return {};
            for (const char character : end)
                if (character < '0' || character > '9') return {};
        }
        bool firstOk = false, lastOk = false;
        const int first = ends.first().toInt(&firstOk);
        const int last = ends.last().toInt(&lastOk);
        if (!firstOk || !lastOk || first <= previous || last < first) return {};
        ranges.append({first, last});
        previous = last;
    }
    return ranges;
}
}

void SystemSensorProvider::clearFrequencyMetadata()
{
    m_onlineMask.clear();
    m_onlineRanges.clear();
    m_cpuPolicyPaths.clear();
    m_policyMaxima.clear();
}

std::optional<double> SystemSensorProvider::frequencyMaximum(int cpu, QSet<QString> &failedPolicies)
{
    QString path = m_cpuPolicyPaths.value(cpu);
    if (path.isEmpty()) {
        const QString policy = QFileInfo(cpuPath(cpu) + QStringLiteral("cpufreq")).canonicalFilePath();
        path = (policy.isEmpty() ? cpuPath(cpu) + QStringLiteral("cpufreq") : policy)
            + QStringLiteral("/cpuinfo_max_freq");
        m_cpuPolicyPaths.insert(cpu, path);
    }
    const auto cached = m_policyMaxima.constFind(path);
    if (cached != m_policyMaxima.cend()) return cached.value();
    if (failedPolicies.contains(path)) {
        m_cpuPolicyPaths.remove(cpu);
        return {};
    }
    const auto limit = systemmetrics::frequencyLimitMHz(read(path));
    if (limit) m_policyMaxima.insert(path, *limit);
    else {
        // Don't cache a failure. Resolve this CPU's policy again on its next
        // demand, so a late driver or replaced policy can recover.
        m_cpuPolicyPaths.remove(cpu);
        failedPolicies.insert(path); // At most one failed read per policy in this batch.
    }
    return limit;
}

SensorDiscovery SystemSensorProvider::discover()
{
    m_previousCpu.reset();
    clearFrequencyMetadata();
    m_cores.clear();
    m_batteries.clear();
    QMap<QPair<int, int>, QList<int>> cores;
    bool complete = true;
    const QRegularExpression pattern(QStringLiteral("^cpu([0-9]+)$"));
    for (const auto &name : QDir(QStringLiteral("/sys/devices/system/cpu")).entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs)) {
        const auto match = pattern.match(name);
        if (!match.hasMatch()) continue;
        const int cpu = match.captured(1).toInt();
        bool packageOk = false, coreOk = false;
        const int package = read(cpuPath(cpu) + QStringLiteral("topology/physical_package_id")).trimmed().toInt(&packageOk);
        const int core = read(cpuPath(cpu) + QStringLiteral("topology/core_id")).trimmed().toInt(&coreOk);
        if (!packageOk || !coreOk || package < 0 || core < 0) {
            if (online(cpu)) complete = false;
            continue;
        }
        cores[{package, core}].append(cpu);
    }
    // Unknown topology cannot safely distinguish SMT siblings from cores.
    if (complete) {
        for (auto it = cores.cbegin(); it != cores.cend(); ++it) {
            auto threads = it.value();
            std::sort(threads.begin(), threads.end());
            if (std::none_of(threads.cbegin(), threads.cend(), online)) continue;
            const QString id = QStringLiteral("system/cpu/package%1/core%2/frequency").arg(it.key().first).arg(it.key().second);
            m_cores.append({it.key().first, it.key().second, id, threads});
        }
    }
    const QString model = systemmetrics::cpuModel(read(QStringLiteral("/proc/cpuinfo")));
    const QString cpu = model.isEmpty() ? QStringLiteral("CPU") : QStringLiteral("CPU — %1").arg(model);
    SensorDiscovery result;
    result.descriptors = {
        {QStringLiteral("system/cpu/activity"), QStringLiteral("Usage"), SensorKind::Percentage, QStringLiteral("%"), cpu},
        {QStringLiteral("system/cpu/frequency"), QStringLiteral("Average frequency"), SensorKind::Frequency, QStringLiteral("MHz"), cpu},
        {QStringLiteral("system/cpu/maximum-frequency"), QStringLiteral("Maximum frequency"), SensorKind::Frequency, QStringLiteral("MHz"), cpu},
        {QStringLiteral("system/memory/ram"), QStringLiteral("RAM usage (%)"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/ram-used"), QStringLiteral("RAM usage (MiB)"), SensorKind::Memory, QStringLiteral("MiB"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/swap-used"), QStringLiteral("Swap usage (MiB)"), SensorKind::Memory, QStringLiteral("MiB"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/available"), QStringLiteral("Available RAM"), SensorKind::Memory, QStringLiteral("MiB"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/swap"), QStringLiteral("Swap usage (%)"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/uptime"), QStringLiteral("Uptime"), SensorKind::Duration, QStringLiteral("s"), QStringLiteral("System")}
    };
    for (const auto &sensor : pressureSensors)
        result.descriptors.append({pressureId(sensor), QString::fromLatin1(sensor.label), SensorKind::Percentage,
                                   QStringLiteral("%"), QStringLiteral("System Pressure")});
    int index = 0;
    for (const auto &core : m_cores)
        result.descriptors.append({core.id, QStringLiteral("Core %1 frequency").arg(index++), SensorKind::Frequency, QStringLiteral("MHz"), cpu});
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    for (const auto &name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = supplies.filePath(name);
        if (read(path + QStringLiteral("/type")).trimmed() != "Battery"
            || read(path + QStringLiteral("/scope")).trimmed() == "Device"
            || read(path + QStringLiteral("/present")).trimmed() == "0"
            || !QFile::exists(path + QStringLiteral("/capacity"))) continue;
        // Kernel supply names identify batteries independently of labels or order.
        const QString id = QStringLiteral("system/battery/%1/charge").arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
        m_batteries.insert(id, path);
        result.descriptors.append({id, QStringLiteral("Charge percentage"), SensorKind::Percentage,
                                   QStringLiteral("%"), QStringLiteral("Battery — %1").arg(name)});
    }
    return result;
}

QList<SensorSample> SystemSensorProvider::readBatch(const QStringList &ids)
{
    const bool cpu = ids.contains(QStringLiteral("system/cpu/activity"));
    const bool frequency = std::any_of(ids.cbegin(), ids.cend(), [](const QString &id) {
        return id.startsWith(QStringLiteral("system/cpu/")) && id.endsWith(QStringLiteral("frequency"));
    });
    const bool memory = std::any_of(ids.cbegin(), ids.cend(), [](const QString &id) { return id.startsWith(QStringLiteral("system/memory/")); });
    const QByteArray stat = cpu ? read(QStringLiteral("/proc/stat")) : QByteArray();
    const QByteArray info = frequency ? read(QStringLiteral("/proc/cpuinfo")) : QByteArray();
    const QByteArray mem = memory ? read(QStringLiteral("/proc/meminfo")) : QByteArray();
    const QByteArray up = ids.contains(QStringLiteral("system/uptime")) ? read(QStringLiteral("/proc/uptime")) : QByteArray();
    QHash<QString, double> frequencies;
    QHash<QString, double> coreMaxima;
    QList<double> onlineMaxima;
    const auto reported = frequency ? systemmetrics::cpuFrequencies(info) : QHash<int, double>();
    bool allFrequencies = true;
    const bool summary = ids.contains(QStringLiteral("system/cpu/frequency")) || ids.contains(QStringLiteral("system/cpu/maximum-frequency"));
    const QSet<QString> requested(ids.cbegin(), ids.cend());
    if (frequency) {
        // One lightweight topology check per frequency batch. Stable batches
        // reuse policy paths and limits; current MHz still comes from cpuinfo.
        const QByteArray mask = read(QStringLiteral("/sys/devices/system/cpu/online")).trimmed();
        bool onlineKnown = !m_onlineRanges.isEmpty() && mask == m_onlineMask;
        if (!onlineKnown) {
            clearFrequencyMetadata();
            if (const auto ranges = cpuRanges(mask)) {
                m_onlineMask = mask;
                m_onlineRanges = *ranges;
                onlineKnown = true;
            }
        }
        const auto isOnline = [this, onlineKnown](int cpu) {
            if (!onlineKnown) return online(cpu); // Missing/malformed aggregate file: retain per-CPU checks.
            return std::any_of(m_onlineRanges.cbegin(), m_onlineRanges.cend(), [cpu](const auto &range) {
                return cpu >= range.first && cpu <= range.second;
            });
        };
        QSet<QString> failedPolicies;
        for (const auto &core : m_cores) {
            if (!summary && !requested.contains(core.id)) continue;
            const auto first = std::find_if(core.threads.cbegin(), core.threads.cend(), isOnline);
            if (first == core.threads.cend()) continue;
            if (reported.contains(*first)) frequencies.insert(core.id, reported.value(*first));
            else {
                allFrequencies = false;
                // A missing current reading may indicate a replaced/offline
                // policy. Drop only the affected mapping and shared limit.
                const QString path = m_cpuPolicyPaths.take(*first);
                m_policyMaxima.remove(path);
            }
            const double limit = frequencyMaximum(*first, failedPolicies).value_or(std::numeric_limits<double>::quiet_NaN());
            coreMaxima.insert(core.id, limit);
            onlineMaxima.append(limit);
        }
    }
    const auto limits = systemmetrics::frequencyLimits(onlineMaxima);
    std::optional<double> activity;
    const auto current = cpu ? systemmetrics::cpuCounters(stat) : std::optional<systemmetrics::CpuCounters>();
    if (current && m_previousCpu)
        activity = systemmetrics::cpuActivity(*m_previousCpu, *current);
    // Partial batches for other sensors must not discard the CPU baseline.
    if (cpu) m_previousCpu = current;
    double average = 0, maximum = 0;
    for (double value : frequencies) { average += value / std::max<qsizetype>(1, frequencies.size()); maximum = std::max(maximum, value); }
    const bool ramWanted = ids.contains(QStringLiteral("system/memory/ram")) || ids.contains(QStringLiteral("system/memory/available"))
        || ids.contains(QStringLiteral("system/memory/ram-used"));
    const auto ram = ramWanted ? systemmetrics::memoryUsage(mem, false) : std::optional<systemmetrics::Usage>();
    const bool swapWanted = ids.contains(QStringLiteral("system/memory/swap")) || ids.contains(QStringLiteral("system/memory/swap-used"));
    const auto swap = swapWanted ? systemmetrics::memoryUsage(mem, true) : std::optional<systemmetrics::Usage>();
    QList<SensorSample> result;
    for (const auto &id : ids) {
        if (QThread::currentThread()->isInterruptionRequested()) break;
        SensorSample sample;
        sample.id = id;
        std::optional<double> value;
        if (id == QStringLiteral("system/cpu/activity")) {
            value = activity;
            sample.error = current ? QStringLiteral("Waiting for a valid CPU counter interval") : QStringLiteral("Aggregate CPU counters unavailable");
            sample.detail = QStringLiteral("Aggregate activity across all online logical CPUs, including SMT siblings");
        } else if (id.startsWith(QStringLiteral("system/cpu/")) && id.endsWith(QStringLiteral("frequency"))) {
            if (id == QStringLiteral("system/cpu/frequency") || id == QStringLiteral("system/cpu/maximum-frequency")) {
                if (allFrequencies && !frequencies.isEmpty()) value = id.endsWith(QStringLiteral("maximum-frequency")) ? maximum : average;
                if (limits) sample.maximum = id.endsWith(QStringLiteral("maximum-frequency")) ? limits->maximum : limits->average;
            } else {
                if (frequencies.contains(id)) value = frequencies.value(id);
                sample.maximum = coreMaxima.value(id, std::numeric_limits<double>::quiet_NaN());
            }
        } else if (id == QStringLiteral("system/memory/ram") || id == QStringLiteral("system/memory/available")) {
            if (ram) {
                value = id.endsWith(QStringLiteral("available")) ? ram->availableMiB : ram->percent;
                sample.reference = ram->totalMiB;
                if (id.endsWith(QStringLiteral("available"))) sample.maximum = ram->totalMiB;
                sample.detail = ram->detail;
            }
        } else if (id == QStringLiteral("system/memory/ram-used")) {
            if (ram) {
                value = std::max(0.0, ram->totalMiB - ram->availableMiB);
                sample.reference = ram->totalMiB;
                sample.maximum = ram->totalMiB;
                sample.detail = ram->detail;
            }
        } else if (id == QStringLiteral("system/memory/swap") || id == QStringLiteral("system/memory/swap-used")) {
            if (swap) {
                value = id.endsWith(QStringLiteral("swap-used")) ? std::max(0.0, swap->totalMiB - swap->availableMiB) : swap->percent;
                sample.reference = swap->totalMiB;
                sample.maximum = swap->totalMiB;
                sample.detail = swap->detail;
            }
        } else if (id.startsWith(QStringLiteral("system/pressure/"))) {
            sample.error = QStringLiteral("Unknown system pressure sensor");
            for (size_t index = 0; index < pressureSensors.size(); ++index) {
                const auto &sensor = pressureSensors[index];
                if (id != pressureId(sensor)) continue;
                if (!m_pressureClock.isValid()) m_pressureClock.start();
                const auto total = systemmetrics::pressureSomeTotal(read(
                    QStringLiteral("/proc/pressure/") + QString::fromLatin1(sensor.resource), 4096));
                value = m_pressure[index].update(total, m_pressureClock.nsecsElapsed());
                sample.error = total ? QStringLiteral("Waiting for a valid pressure counter interval")
                    : QStringLiteral("System pressure information missing, malformed or unsupported");
                sample.detail = QStringLiteral("System-wide some pressure since this sensor's previous successful acquisition");
                break;
            }
        } else if (m_batteries.contains(id)) {
            const QString path = m_batteries.value(id);
            const QString presencePath = path + QStringLiteral("/present");
            // Some drivers omit present. When exposed, only an explicit 1 is safe.
            if (QFile::exists(presencePath) && read(presencePath).trimmed() != "1")
                sample.error = QStringLiteral("Battery is absent or its presence is unavailable");
            else {
                value = systemmetrics::batteryChargePercent(read(path + QStringLiteral("/capacity")));
                sample.error = QStringLiteral("Battery charge percentage is missing, malformed or outside 0–100");
            }
            sample.detail = QStringLiteral("Kernel-reported remaining charge (%1)").arg(QFileInfo(path).fileName());
        } else if (id == QStringLiteral("system/uptime")) value = systemmetrics::uptimeSeconds(up);
        else sample.error = QStringLiteral("Unknown system sensor");
        if (value && std::isfinite(*value)) { sample.value = *value; sample.available = true; sample.error.clear(); }
        else if (sample.error.isEmpty()) sample.error = QStringLiteral("System reading missing, malformed or unsupported");
        sample.timestamp = QDateTime::currentDateTimeUtc();
        result.append(sample);
    }
    return result;
}

} // namespace qsensors
