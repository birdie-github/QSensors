// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemsensorprovider.h"

#include <QFile>
#include <QThread>

namespace qsensors {
namespace {
QByteArray read(const char *path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    // proc files often report size zero. Bound allocations without relying on size().
    constexpr qint64 limit = 4 * 1024 * 1024;
    const QByteArray bytes = file.read(limit + 1);
    return file.error() == QFile::NoError && bytes.size() <= limit ? bytes : QByteArray();
}
}

SensorDiscovery SystemSensorProvider::discover()
{
    m_previousCpu.reset();
    const QString model = systemmetrics::cpuModel(read("/proc/cpuinfo"));
    const QString cpu = model.isEmpty() ? QStringLiteral("CPU") : QStringLiteral("CPU — %1").arg(model);
    SensorDiscovery result;
    result.descriptors = {
        {QStringLiteral("system/cpu/activity"), QStringLiteral("CPU activity"), SensorKind::Percentage, QStringLiteral("%"), cpu},
        {QStringLiteral("system/cpu/frequency"), QStringLiteral("CPU frequency (reported mean)"), SensorKind::Frequency, QStringLiteral("MHz"), cpu},
        {QStringLiteral("system/memory/ram"), QStringLiteral("RAM usage"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/memory/swap"), QStringLiteral("Swap usage"), SensorKind::Percentage, QStringLiteral("%"), QStringLiteral("Memory")},
        {QStringLiteral("system/uptime"), QStringLiteral("Uptime"), SensorKind::Duration, QStringLiteral("s"), QStringLiteral("System")}
    };
    return result;
}

QList<SensorSample> SystemSensorProvider::readBatch(const QStringList &ids)
{
    const bool cpu = ids.contains(QStringLiteral("system/cpu/activity"));
    const bool frequency = ids.contains(QStringLiteral("system/cpu/frequency"));
    const bool memory = ids.contains(QStringLiteral("system/memory/ram")) || ids.contains(QStringLiteral("system/memory/swap"));
    const bool uptime = ids.contains(QStringLiteral("system/uptime"));
    // Each shared file is read at most once per requested batch, never per tile.
    const QByteArray stat = cpu ? read("/proc/stat") : QByteArray();
    const QByteArray info = frequency ? read("/proc/cpuinfo") : QByteArray();
    const QByteArray mem = memory ? read("/proc/meminfo") : QByteArray();
    const QByteArray up = uptime ? read("/proc/uptime") : QByteArray();
    std::optional<double> activity;
    std::optional<systemmetrics::CpuCounters> current;
    if (cpu) current = systemmetrics::cpuCounters(stat);
    if (current && m_previousCpu) activity = systemmetrics::cpuActivity(*m_previousCpu, *current);
    m_previousCpu = current;
    QList<SensorSample> result;
    for (const auto &id : ids) {
        if (QThread::currentThread()->isInterruptionRequested()) break;
        SensorSample sample;
        sample.id = id;
        std::optional<double> value;
        if (id == QStringLiteral("system/cpu/activity")) {
            value = activity;
            sample.error = current ? QStringLiteral("Waiting for a valid CPU counter interval") : QStringLiteral("Cannot read aggregate CPU counters");
        } else if (id == QStringLiteral("system/cpu/frequency")) {
            value = systemmetrics::meanCpuMHz(info);
            sample.detail = QStringLiteral("Arithmetic mean of cpu MHz reported in /proc/cpuinfo; a sampled estimate, not a base clock or effective work rate");
        } else if (id == QStringLiteral("system/memory/ram") || id == QStringLiteral("system/memory/swap")) {
            const auto usage = systemmetrics::memoryUsage(mem, id.endsWith(QStringLiteral("swap")));
            if (usage) {
                value = usage->percent;
                sample.detail = usage->detail;
            }
        } else if (id == QStringLiteral("system/uptime")) value = systemmetrics::uptimeSeconds(up);
        else sample.error = QStringLiteral("Unknown system sensor");
        if (value) {
            sample.value = *value;
            sample.available = true;
            sample.error.clear();
        } else if (sample.error.isEmpty()) sample.error = QStringLiteral("System reading missing, malformed or unsupported");
        sample.timestamp = QDateTime::currentDateTimeUtc();
        result.append(sample);
    }
    return result;
}

} // namespace qsensors
