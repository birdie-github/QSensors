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

std::optional<double> meanCpuMHz(const QByteArray &contents)
{
    long double sum = 0;
    quint64 count = 0;
    for (const auto &line : contents.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).trimmed() != "cpu MHz") continue;
        bool ok = false;
        const double value = line.mid(colon + 1).trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value) || value <= 0) return {};
        sum += value;
        ++count;
    }
    if (!count) return {};
    const double mean = static_cast<double>(sum / count);
    return std::isfinite(mean) ? std::optional<double>(mean) : std::nullopt;
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
