// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/valueformat.h"
#include <QLocale>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace qsensors {
namespace {
QString compact(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    if (value == 0) return QStringLiteral("0");
    // The sign also counts toward the three-character numeric budget.
    const int decimals = value > 0 && value < 9.95 ? 1 : 0;
    const double limit = value < 0 ? 99 : 999;
    return QLocale().toString(std::clamp(value, -limit, limit), 'f', decimals);
}
}
UptimeValue formatUptime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0) return {QStringLiteral("—"), {}};
    const bool days = seconds >= 100.0 * 3600;
    const double remaining = days ? std::fmod(seconds, 86400.0) : seconds;
    const int hours = static_cast<int>(remaining / 3600);
    const int minutes = static_cast<int>(std::fmod(remaining, 3600.0) / 60);
    UptimeValue result;
    result.time = QStringLiteral("%1:%2").arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0'));
    if (days) result.days = QLocale().toString(std::floor(seconds / 86400.0), 'f', 0) + QStringLiteral(" days");
    return result;
}
FormattedValue formatSensorValue(const SensorDescriptor &descriptor, double value, bool tray)
{
    if (descriptor.id == QStringLiteral("system/uptime")) {
        const auto uptime = formatUptime(value);
        return {uptime.days.isEmpty() ? uptime.time : uptime.days + QLatin1Char(' ') + uptime.time, {}};
    }
    if (descriptor.kind == SensorKind::Frequency) {
        const bool giga = std::abs(value) >= 1000;
        return {compact(giga ? value / 1000 : value), tray ? (giga ? QStringLiteral("G") : QStringLiteral("M"))
            : (giga ? QStringLiteral("GHz") : QStringLiteral("MHz"))};
    }
    if (descriptor.kind == SensorKind::Memory) {
        const QStringList units = {QStringLiteral("MiB"), QStringLiteral("GiB"), QStringLiteral("TiB"), QStringLiteral("PiB")};
        const QStringList shortUnits = {QStringLiteral("M"), QStringLiteral("G"), QStringLiteral("T"), QStringLiteral("P")};
        if (value >= 0 && value < 1) value = 0; // Less than one MiB is displayed as zero.
        int index = 0;
        // Promote just before four-digit rounding would exceed the text budget.
        while (std::abs(value) >= 999.5 && index < 3) { value /= 1024; ++index; }
        return {compact(value), tray ? shortUnits[index] : units[index]};
    }
    if (descriptor.kind == SensorKind::Percentage)
        return {compact(value), QStringLiteral("%")};
    return {QLocale().toString(value, 'g', 8), descriptor.unit};
}
}
