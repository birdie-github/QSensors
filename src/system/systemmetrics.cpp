// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemmetrics.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors::systemmetrics {
namespace {
std::optional<quint64> unsignedNumber(const QByteArray &token)
{
    if (token.isEmpty()) return {};
    for (const char c : token)
        if (c < '0' || c > '9') return {};
    bool ok = false;
    const auto value = token.toULongLong(&ok);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

bool add(quint64 &sum, quint64 value)
{
    if (value > std::numeric_limits<quint64>::max() - sum) return false;
    sum += value;
    return true;
}
}

std::optional<quint64> pressureSomeTotal(const QByteArray &contents)
{
    std::optional<quint64> total;
    bool foundSome = false;
    for (const auto &line : contents.split('\n')) {
        const auto fields = line.simplified().split(' ');
        if (fields.isEmpty() || fields.front() != "some") continue;
        if (foundSome) return {}; // Ambiguous duplicate records are not readings.
        foundSome = true;
        for (qsizetype i = 1; i < fields.size(); ++i) {
            if (!fields[i].startsWith("total=")) continue;
            if (total) return {};
            total = unsignedNumber(fields[i].mid(6));
            if (!total) return {};
        }
    }
    return total; // Ignore rolling averages and the full-pressure record.
}

std::optional<double> PressureTracker::update(std::optional<quint64> totalMicroseconds, qint64 nanoseconds)
{
    // Failed reads retain both counter and time from the last successful read.
    if (!totalMicroseconds || nanoseconds < 0) return {};
    const Baseline current{*totalMicroseconds, nanoseconds};
    const auto previous = m_previous;
    m_previous = current;
    if (!previous || current.totalMicroseconds < previous->totalMicroseconds
        || current.nanoseconds <= previous->nanoseconds) return {};
    // Subtract in integer space first to retain small deltas on long uptimes.
    const double stalled = static_cast<double>(current.totalMicroseconds - previous->totalMicroseconds);
    const double elapsedMicroseconds = static_cast<double>(current.nanoseconds - previous->nanoseconds) / 1000.0;
    return std::clamp(stalled / elapsedMicroseconds * 100.0, 0.0, 100.0);
}

std::optional<DiskCounters> diskCounters(const QByteArray &contents)
{
    const auto fields = contents.simplified().split(' ');
    if (fields.size() < 11) return {};
    DiskCounters result{};
    for (qsizetype i = 0; i < fields.size(); ++i) {
        const auto value = unsignedNumber(fields[i]);
        if (!value) return {};
        if (i == 2) result.sectorsRead = *value;
        else if (i == 6) result.sectorsWritten = *value;
        else if (i == 9) result.busyMilliseconds = *value;
    }
    return result;
}

std::optional<DiskInterval> diskInterval(const DiskCounters &before, const DiskCounters &after, double elapsedMilliseconds)
{
    if (!std::isfinite(elapsedMilliseconds) || elapsedMilliseconds <= 0
        || after.sectorsRead < before.sectorsRead || after.sectorsWritten < before.sectorsWritten
        || after.busyMilliseconds < before.busyMilliseconds) return {};
    // Kernel disk-stat sectors are always 512 bytes, including on 4K-sector disks.
    return DiskInterval{static_cast<double>(after.sectorsRead - before.sectorsRead) * 512.0,
        static_cast<double>(after.sectorsWritten - before.sectorsWritten) * 512.0,
        std::clamp(static_cast<double>(after.busyMilliseconds - before.busyMilliseconds) / elapsedMilliseconds * 100.0, 0.0, 100.0)};
}

std::optional<double> batteryPowerWatts(double magnitude, const QByteArray &status)
{
    if (!std::isfinite(magnitude)) return {};
    const auto state = status.trimmed();
    const double power = std::abs(magnitude);
    if (state == "Discharging") return power == 0 ? 0.0 : -power;
    if (state == "Charging" || state == "Full" || state == "Not charging") return power;
    return {}; // Unknown direction must not look like charging.
}

std::optional<double> batteryChargePercent(const QByteArray &contents)
{
    // power_supply/capacity is an integer percentage, not charge or energy.
    const auto value = unsignedNumber(contents.trimmed());
    if (!value || *value > 100) return {};
    return static_cast<double>(*value);
}

std::optional<CpuCounters> cpuCounters(const QByteArray &contents)
{
    for (const auto &line : contents.split('\n')) {
        const auto parts = line.simplified().split(' ');
        if (parts.isEmpty() || parts.front() != "cpu") continue;
        if (parts.size() < 5) return {}; // Four historical fields are mandatory.
        CpuCounters result;
        for (qsizetype i = 1; i < parts.size(); ++i) {
            const auto value = unsignedNumber(parts[i]);
            if (!value) return {};
            if (i <= 8) result.fields[static_cast<size_t>(i - 1)] = *value;
            // Validate but do not add guest/guest_nice again.
        }
        return result;
    }
    return {};
}

QHash<int, double> cpuFrequencies(const QByteArray &contents)
{
    QHash<int, double> values;
    int cpu = -1;
    for (const auto &line : contents.split('\n')) {
        if (line.trimmed().isEmpty()) { cpu = -1; continue; }
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const auto key = line.left(colon).trimmed();
        bool ok = false;
        if (key == "processor") {
            cpu = line.mid(colon + 1).trimmed().toInt(&ok);
            if (!ok || cpu < 0) cpu = -1;
        } else if (key == "cpu MHz" && cpu >= 0) {
            const double value = line.mid(colon + 1).trimmed().toDouble(&ok);
            if (ok && std::isfinite(value) && value > 0 && !values.contains(cpu)) values.insert(cpu, value);
        }
    }
    return values;
}

std::optional<double> frequencyLimitMHz(const QByteArray &contents)
{
    const auto khz = unsignedNumber(contents.trimmed());
    if (!khz || !*khz) return {};
    return static_cast<double>(*khz) / 1000.0;
}

std::optional<FrequencyLimits> frequencyLimits(const QList<double> &coreMaxima)
{
    if (coreMaxima.isEmpty()) return {};
    FrequencyLimits limits{0, 0};
    for (double value : coreMaxima) {
        if (!std::isfinite(value) || value <= 0) return {};
        limits.average += value / coreMaxima.size();
        limits.maximum = std::max(limits.maximum, value);
    }
    return std::isfinite(limits.average) ? std::optional<FrequencyLimits>(limits) : std::nullopt;
}

std::optional<double> cpuActivity(const CpuCounters &previous, const CpuCounters &current)
{
    quint64 total = 0, idle = 0;
    for (size_t i = 0; i < current.fields.size(); ++i) {
        // Includes decreasing iowait: establish a new baseline rather than underflow.
        if (current.fields[i] < previous.fields[i]) return {};
        const auto delta = current.fields[i] - previous.fields[i];
        if (!add(total, delta)) return {};
        if ((i == 3 || i == 4) && !add(idle, delta)) return {};
    }
    if (!total) return {};
    return 100.0 * static_cast<double>(total - idle) / static_cast<double>(total);
}

std::optional<Usage> memoryUsage(const QByteArray &contents, bool swap)
{
    QHash<QByteArray, quint64> values;
    const QList<QByteArray> keys = swap ? QList<QByteArray>{"SwapTotal", "SwapFree"}
        : QList<QByteArray>{"MemTotal", "MemAvailable", "MemFree", "Buffers", "Cached", "SReclaimable", "Shmem"};
    for (const auto &line : contents.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QByteArray key = line.left(colon).trimmed();
        if (!keys.contains(key)) continue;
        const auto parts = line.mid(colon + 1).simplified().split(' ');
        if (parts.size() != 2 || parts[1] != "kB" || values.contains(key)) return {};
        const auto value = unsignedNumber(parts[0]);
        if (!value) return {};
        values.insert(key, *value);
    }
    Usage result;
    const QByteArray totalKey = swap ? "SwapTotal" : "MemTotal";
    if (!values.contains(totalKey)) return {};
    const quint64 total = values.value(totalKey);
    quint64 available = 0;
    if (swap) {
        if (!total) {
            result.detail = QStringLiteral("No swap configured");
            return result;
        }
        if (!values.contains("SwapFree")) return {};
        available = values.value("SwapFree");
    } else {
        if (!total) return {};
        if (values.contains("MemAvailable")) available = values.value("MemAvailable");
        else {
            if (!values.contains("MemFree") || !values.contains("Buffers") || !values.contains("Cached")) return {};
            available = values.value("MemFree");
            if (!add(available, values.value("Buffers")) || !add(available, values.value("Cached"))
                || !add(available, values.value("SReclaimable"))) return {};
            const quint64 shared = values.value("Shmem");
            available = shared >= available ? 0 : available - shared;
            available = std::min(available, total);
            result.detail = QStringLiteral("Estimated availability: free + buffers + cache + reclaimable − shared memory");
        }
    }
    if (available > total) return {};
    result.availableMiB = static_cast<double>(available) / 1024.0;
    result.totalMiB = static_cast<double>(total) / 1024.0;
    result.percent = 100.0 * static_cast<double>(total - available) / static_cast<double>(total);
    return result;
}

QString cpuModel(const QByteArray &contents)
{
    for (const auto &line : contents.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const auto key = line.left(colon).trimmed();
        if (key == "model name" || key == "Hardware" || key == "Processor") {
            const QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
            if (!value.isEmpty()) return value;
        }
    }
    return {};
}

std::optional<double> uptimeSeconds(const QByteArray &contents)
{
    const auto parts = contents.simplified().split(' ');
    if (parts.size() != 2) return {};
    bool ok = false;
    const double uptime = parts[0].toDouble(&ok);
    if (!ok || !std::isfinite(uptime) || uptime < 0) return {};
    const double idle = parts[1].toDouble(&ok);
    if (!ok || !std::isfinite(idle) || idle < 0) return {};
    return uptime;
}

} // namespace qsensors::systemmetrics
