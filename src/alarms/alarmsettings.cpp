// SPDX-License-Identifier: GPL-3.0-only
#include "alarms/alarmsettings.h"
#include <QSettings>
#include <cmath>
#include <utility>

namespace qsensors {
namespace {
// One field inventory shared by equality and persistence.
const auto flags = {
    std::pair{"enabled", &AlarmSettings::active},
    std::pair{"lowerEnabled", &AlarmSettings::lowerEnabled},
    std::pair{"upperEnabled", &AlarmSettings::upperEnabled},
    std::pair{"dashboard", &AlarmSettings::dashboard},
    std::pair{"table", &AlarmSettings::table},
    std::pair{"notify", &AlarmSettings::notify},
    std::pair{"command", &AlarmSettings::command},
    std::pair{"trayMarkers", &AlarmSettings::trayMarkers},
    std::pair{"trayBlink", &AlarmSettings::trayBlink},
    std::pair{"trayColor", &AlarmSettings::trayColor}
};
const auto numbers = {
    std::pair{"lower", &AlarmSettings::lower},
    std::pair{"upper", &AlarmSettings::upper},
    std::pair{"hysteresis", &AlarmSettings::hysteresis}
};
const auto colors = {
    std::pair{"dashboardColor", &AlarmSettings::dashboardColor},
    std::pair{"tableColor", &AlarmSettings::tableColor},
    std::pair{"trayTextColor", &AlarmSettings::trayTextColor}
};
}

bool AlarmSettings::hasActions() const
{
    return dashboard || table || notify || command || trayMarkers || trayBlink || trayColor;
}

void AlarmSettings::setActive(bool enabled)
{
    active = enabled;
    // Explicit enabling must select a useful action. Pausing and loading never
    // clear or invent settings, and re-enabling preserves existing choices.
    if (enabled && !hasActions()) notify = true;
}

bool AlarmSettings::valid() const
{
    return std::isfinite(lower) && std::abs(lower) <= 1e12
        && std::isfinite(upper) && std::abs(upper) <= 1e12
        && std::isfinite(hysteresis) && hysteresis >= 0 && hysteresis <= 1e12
        && (!(lowerEnabled && upperEnabled) || (lower < upper && hysteresis <= (upper - lower) / 2))
        && (!command || !program.trimmed().isEmpty())
        && dashboardColor.isValid() && tableColor.isValid() && trayTextColor.isValid();
}

bool AlarmSettings::operator==(const AlarmSettings &other) const
{
    for (const auto &[key, field] : flags) if (this->*field != other.*field) return false;
    for (const auto &[key, field] : numbers) if (this->*field != other.*field) return false;
    for (const auto &[key, field] : colors) if (this->*field != other.*field) return false;
    return program == other.program && arguments == other.arguments;
}

AlarmSettings readAlarmSettings(QSettings &settings)
{
    AlarmSettings alarm;
    settings.beginGroup(QStringLiteral("Alarm"));
    for (const auto &[key, field] : flags) {
        const auto value = settings.value(QLatin1String(key), alarm.*field).toString().toLower();
        alarm.*field = value == QStringLiteral("true") || value == QStringLiteral("1");
    }
    bool validNumbers = true;
    for (const auto &[key, field] : numbers) {
        bool ok = false;
        alarm.*field = settings.value(QLatin1String(key), alarm.*field).toDouble(&ok);
        validNumbers &= ok;
    }
    for (const auto &[key, field] : colors) {
        const QColor color(settings.value(QLatin1String(key), (alarm.*field).name()).toString());
        if (color.isValid()) alarm.*field = color;
    }
    alarm.program = settings.value(QStringLiteral("program")).toString();
    alarm.arguments = settings.value(QStringLiteral("arguments")).toString();
    settings.endGroup();
    // Malformed hand-edited thresholds must never accidentally arm an alarm.
    return validNumbers && alarm.valid() ? alarm : AlarmSettings{};
}

void writeAlarmSettings(QSettings &settings, const AlarmSettings &alarm)
{
    const AlarmSettings defaults;
    settings.beginGroup(QStringLiteral("Alarm"));
    const auto write = [&settings](const QString &key, const auto &value, const auto &fallback) {
        if (value == fallback) settings.remove(key);
        else settings.setValue(key, value);
    };
    for (const auto &[key, field] : flags) write(QLatin1String(key), alarm.*field, defaults.*field);
    for (const auto &[key, field] : numbers) write(QLatin1String(key), alarm.*field, defaults.*field);
    for (const auto &[key, field] : colors) write(QLatin1String(key), (alarm.*field).name(), (defaults.*field).name());
    write(QStringLiteral("program"), alarm.program, defaults.program);
    write(QStringLiteral("arguments"), alarm.arguments, defaults.arguments);
    settings.endGroup();
}

} // namespace qsensors
