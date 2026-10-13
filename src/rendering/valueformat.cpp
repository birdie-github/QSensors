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
FormattedValue formatSensorValue(const SensorDescriptor &descriptor, double value, bool tray)
{
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
