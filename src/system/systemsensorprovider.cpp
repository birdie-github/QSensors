// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemsensorprovider.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace qsensors {
namespace {
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 limit = 4 * 1024 * 1024;
    const QByteArray bytes = file.read(limit + 1);
    return file.error() == QFile::NoError && bytes.size() <= limit ? bytes : QByteArray();
}
QString cpuPath(int cpu) { return QStringLiteral("/sys/devices/system/cpu/cpu%1/").arg(cpu); }
bool online(int cpu)
{
    const QString path = cpuPath(cpu) + QStringLiteral("online");
    // Non-hotpluggable CPUs need not expose this attribute.
    return !QFile::exists(path) || read(path).trimmed() == "1";
}
}

SensorDiscovery SystemSensorProvider::discover()
{
    m_previousCpu.reset();
    m_cores.clear();
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
        {QStringLiteral("system/memory/ram"), QStringLiteral("RAM usage"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/available"), QStringLiteral("Available RAM"), SensorKind::Memory, QStringLiteral("MiB"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/swap"), QStringLiteral("Swap usage"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/uptime"), QStringLiteral("Uptime"), SensorKind::Duration, QStringLiteral("s"), QStringLiteral("System")}
    };
    int index = 0;
    for (const auto &core : m_cores)
        result.descriptors.append({core.id, QStringLiteral("Core %1 frequency").arg(index++), SensorKind::Frequency, QStringLiteral("MHz"), cpu});
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
    const auto reported = frequency ? systemmetrics::cpuFrequencies(info) : QHash<int, double>();
    bool allFrequencies = true;
    const bool summary = ids.contains(QStringLiteral("system/cpu/frequency")) || ids.contains(QStringLiteral("system/cpu/maximum-frequency"));
    const QSet<QString> requested(ids.cbegin(), ids.cend());
    if (frequency) {
        for (const auto &core : m_cores) {
            if (!summary && !requested.contains(core.id)) continue;
            const auto first = std::find_if(core.threads.cbegin(), core.threads.cend(), online);
            if (first == core.threads.cend()) continue;
            if (reported.contains(*first)) frequencies.insert(core.id, reported.value(*first));
            else allFrequencies = false;
        }
    }
    std::optional<double> activity;
    const auto current = cpu ? systemmetrics::cpuCounters(stat) : std::optional<systemmetrics::CpuCounters>();
    if (current && m_previousCpu)
        activity = systemmetrics::cpuActivity(*m_previousCpu, *current);
    m_previousCpu = current;
    double average = 0, maximum = 0;
    for (double value : frequencies) { average += value / std::max<qsizetype>(1, frequencies.size()); maximum = std::max(maximum, value); }
    const bool ramWanted = ids.contains(QStringLiteral("system/memory/ram")) || ids.contains(QStringLiteral("system/memory/available"));
    const auto ram = ramWanted ? systemmetrics::memoryUsage(mem, false) : std::optional<systemmetrics::Usage>();
    const auto swap = ids.contains(QStringLiteral("system/memory/swap")) ? systemmetrics::memoryUsage(mem, true) : std::optional<systemmetrics::Usage>();
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
            } else if (frequencies.contains(id)) value = frequencies.value(id);
            sample.detail = QStringLiteral("Current reported MHz; lowest online thread per physical core, excluding SMT siblings. Maximum is a current reading, not a rated boost limit.");
        } else if (id == QStringLiteral("system/memory/ram") || id == QStringLiteral("system/memory/available")) {
            if (ram) {
                value = id.endsWith(QStringLiteral("available")) ? ram->availableMiB : ram->percent;
                sample.reference = ram->totalMiB;
                sample.detail = ram->detail;
            }
        } else if (id == QStringLiteral("system/memory/swap")) {
            if (swap) { value = swap->percent; sample.detail = swap->detail; }
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
