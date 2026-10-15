// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/valueformat.h"
#include "settings/numberformat.h"
#include <QLocale>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>

namespace qsensors {
namespace {
struct ScaledMemory { double value; int unit; };
ScaledMemory scaleMemory(double value)
{
    if (value >= 0 && value < 1) value = 0;
    int unit = 0;
    while (std::abs(value) >= 999.5 && unit < 3) { value /= 1024; ++unit; }
    return {value, unit};
}
QString compact(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    if (value == 0) return QStringLiteral("0");
    // The sign also counts toward the three-character numeric budget.
    const int decimals = value > 0 && value < 9.95 ? 1 : 0;
    const double limit = value < 0 ? 99 : 999;
    return numberLocale().toString(std::clamp(value, -limit, limit), 'f', decimals);
}
}
QString localizeDecimalPoint(QString number)
{
    return number.replace(QLatin1Char('.'), numberLocale().decimalPoint());
}

QString formatTrayNumber(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    constexpr int characterBudget = 4;
    constexpr int maximumDecimals = 2;
    for (int decimals = maximumDecimals; decimals >= 0; --decimals) {
        QString text = QString::number(value, 'f', decimals);
        if (text.contains(QLatin1Char('.'))) {
            while (text.endsWith(QLatin1Char('0'))) text.chop(1);
            if (text.endsWith(QLatin1Char('.'))) text.chop(1);
        }
        if (text == QStringLiteral("0") || text == QStringLiteral("-0")) {
            if (value != 0) continue; // Do not turn a tiny nonzero reading into zero.
            text = QStringLiteral("0");
        }
        text = localizeDecimalPoint(text);
        if (text.size() <= characterBudget) return text;
    }
    // Drop the redundant plus and exponent padding: 1e+04 becomes 1e4.
    const QString scientific = QString::number(value, 'e', 0);
    const int exponent = scientific.indexOf(QLatin1Char('e'));
    const QString text = scientific.left(exponent) + QLatin1Char('e')
        + QString::number(scientific.mid(exponent + 1).toInt());
    return text.size() <= characterBudget ? text : QStringLiteral("ERR");
}

QString formatTrayMemoryNumber(double mebibytes)
{
    return formatTrayNumber(scaleMemory(mebibytes).value);
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
    if (days) result.days = numberLocale().toString(std::floor(seconds / 86400.0), 'f', 0) + QStringLiteral(" days");
    return result;
}
QString formatDigitalSensorValue(const SensorDescriptor &descriptor, double value, bool kelvin)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    const auto number = [](double value, int decimals, bool trim) {
        const double factor = decimals == 2 ? 100.0 : decimals == 1 ? 10.0 : 1.0;
        // Avoid overflowing a finite extreme measurement during display rounding.
        if (std::abs(value) <= std::numeric_limits<double>::max() / factor)
            value = std::round(value * factor) / factor;
        if (value == 0) value = 0; // Suppress negative zero after rounding.
        const bool scientific = std::abs(value) >= 1e12;
        QString text = QString::number(value, scientific ? 'g' : 'f', scientific ? 6 : decimals);
        if (trim && text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e'))) {
            while (text.endsWith(QLatin1Char('0'))) text.chop(1);
            if (text.endsWith(QLatin1Char('.'))) text.chop(1);
        }
        return localizeDecimalPoint(text);
    };
    switch (descriptor.kind) {
    case SensorKind::Memory: {
        constexpr std::array<char, 4> units = {{'M','G','T','P'}};
        if (std::abs(value) < 1) value = 0;
        int index = 0;
        while (std::abs(std::round(value)) > 9999 && index < 3) { value /= 1024; ++index; }
        return number(value, 0, false) + QLatin1Char(units[index]);
    }
    case SensorKind::Voltage: return number(value, 2, true) + QStringLiteral("V");
    case SensorKind::Temperature: return number(value, 1, false)
        + (kelvin ? QStringLiteral("K") : QString(QChar(0x00b0)));
    case SensorKind::Fan: return number(value, 0, false) + QStringLiteral(" RPM");
    case SensorKind::Frequency: return number(value, 0, false); // Original CPU LCD: whole MHz; unit drawn separately.
    case SensorKind::Humidity:
    case SensorKind::Percentage: return number(value, 1, true) + QStringLiteral("%");
    case SensorKind::Current:
    case SensorKind::Power:
    case SensorKind::Energy: return number(value, 2, true) + descriptor.unit;
    case SensorKind::Duration: return number(value, 0, false) + descriptor.unit;
    }
    return QStringLiteral("—");
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
        const auto scaled = scaleMemory(value);
        return {compact(scaled.value), tray ? shortUnits[scaled.unit] : units[scaled.unit]};
    }
    if (descriptor.kind == SensorKind::Percentage)
        return {compact(value), QStringLiteral("%")};
    return {numberLocale().toString(value, 'g', 8), descriptor.unit};
}
}
