// SPDX-License-Identifier: GPL-3.0-only
#include "settings/sensorpreferences.h"
#include "sensors/sensortypes.h"

#include <QSettings>
#include <QUrl>
#include <QSet>
#include <limits>

#include <algorithm>
#include <cmath>

namespace qsensors {

double temperatureToDisplay(double value, TemperatureUnit unit)
{
    switch (unit) {
    case TemperatureUnit::Fahrenheit: return value * 1.8 + 32;
    case TemperatureUnit::Kelvin: return value + 273.15;
    default: return value;
    }
}

double temperatureFromDisplay(double value, TemperatureUnit unit)
{
    switch (unit) {
    case TemperatureUnit::Fahrenheit: return (value - 32) / 1.8;
    case TemperatureUnit::Kelvin: return value - 273.15;
    default: return value;
    }
}

double temperatureDifferenceToDisplay(double value, TemperatureUnit unit)
{
    return unit == TemperatureUnit::Fahrenheit ? value * 1.8 : value;
}

double temperatureDifferenceFromDisplay(double value, TemperatureUnit unit)
{
    return unit == TemperatureUnit::Fahrenheit ? value / 1.8 : value;
}

QString temperatureUnitText(TemperatureUnit unit)
{
    switch (unit) {
    case TemperatureUnit::Fahrenheit: return QStringLiteral("°F");
    case TemperatureUnit::Kelvin: return QStringLiteral("K");
    default: return QStringLiteral("°C");
    }
}

QString sensorConfigId(const QString &id)
{
    return QUrl::fromPercentEncoding(id.toUtf8());
}

QString sensorInternalId(const QString &id)
{
    const QString prefix = QStringLiteral("libsensors/");
    if (!id.startsWith(prefix)) return id;
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    if (slash <= prefix.size() || slash == id.size() - 1) return {};
    return prefix + QString::fromLatin1(QUrl::toPercentEncoding(id.mid(prefix.size(), slash - prefix.size())))
        + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(id.mid(slash + 1)));
}

SensorPreferences defaultSensorPreferences(const QString &id)
{
    SensorPreferences p;
    const bool uptime = id == QStringLiteral("system/uptime");
    p.trayShowUnit = id == QStringLiteral("system/memory/available")
        || id == QStringLiteral("system/memory/ram-used") || id == QStringLiteral("system/memory/swap-used");
    p.dashboardShadow = uptime;
    p.dashboardGauge = !uptime;
    p.dashboardTextStyle = uptime ? DashboardTextStyle::Digital : DashboardTextStyle::Normal;
    return p;
}

bool SensorPreferences::operator==(const SensorPreferences &other) const
{
    return alarm == other.alarm && position == other.position && dashboard == other.dashboard && tray == other.tray && label == other.label
        && multiplier == other.multiplier && offset == other.offset
        && foreground == other.foreground && background == other.background
        && dashboardInterval == other.dashboardInterval && trayInterval == other.trayInterval
        && dashboardUseGlobal == other.dashboardUseGlobal && trayUseGlobal == other.trayUseGlobal
        && dashboardGauge == other.dashboardGauge && dashboardTextStyle == other.dashboardTextStyle
        && dashboardShadow == other.dashboardShadow
        && dashboardTransparent == other.dashboardTransparent
        && trayForeground == other.trayForeground && trayBackground == other.trayBackground
        && trayShadow == other.trayShadow && trayTransparent == other.trayTransparent
        && trayShowUnit == other.trayShowUnit
        && trayDigital == other.trayDigital && trayFontFamily == other.trayFontFamily
        && memoryAsPercent == other.memoryAsPercent
        && customRange == other.customRange && rangeLower == other.rangeLower && rangeUpper == other.rangeUpper;
}

QColor dashboardTextColor(const SensorPreferences &preferences, const QColor &systemText)
{
    if (preferences.foreground.isValid())
        return preferences.foreground;
    if (!preferences.dashboardTransparent && preferences.background.isValid())
        return preferences.background.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white);
    return systemText;
}

namespace {
bool boolean(QSettings &settings, const QString &key, bool fallback)
{
    const QString value = settings.value(key, fallback).toString().trimmed().toLower();
    if (value == QStringLiteral("true") || value == QStringLiteral("1"))
        return true;
    if (value == QStringLiteral("false") || value == QStringLiteral("0"))
        return false;
    return fallback;
}

double number(QSettings &settings, const QString &key, double fallback, double limit)
{
    bool valid = false;
    const double value = settings.value(key, fallback).toDouble(&valid);
    return valid && std::isfinite(value) && std::abs(value) <= limit ? value : fallback;
}

SensorPreferences readPreferences(QSettings &settings, const QString &id)
{
    SensorPreferences p = defaultSensorPreferences(id);
    p.alarm = readAlarmSettings(settings);
    p.dashboard = boolean(settings, QStringLiteral("dashboard"), false);
    p.tray = boolean(settings, QStringLiteral("tray"), false);
    p.label = settings.value(QStringLiteral("label")).toString();
    if (p.dashboard && settings.contains(QStringLiteral("column")) && settings.contains(QStringLiteral("row"))) {
        bool xOk = false, yOk = false;
        const int x = settings.value(QStringLiteral("column")).toInt(&xOk);
        const int y = settings.value(QStringLiteral("row")).toInt(&yOk);
        constexpr int limit = dashboardCellLimit;
        if (xOk && yOk && x >= -limit && x < limit && y >= -limit && y < limit) p.position = QPoint(x, y);
    }
    p.multiplier = number(settings, QStringLiteral("multiplier"), 1, 1e6);
    p.offset = number(settings, QStringLiteral("offset"), 0, 1e12);
    p.foreground = QColor(settings.value(QStringLiteral("foreground")).toString());
    p.background = QColor(settings.value(QStringLiteral("background")).toString());
    const QString trayColor = settings.value(QStringLiteral("trayForeground")).toString();
    p.trayForeground = trayColor == QStringLiteral("default") ? QColor() : QColor(trayColor);
    p.trayBackground = QColor(settings.value(QStringLiteral("trayBackground")).toString());
    p.dashboardShadow = boolean(settings, QStringLiteral("dashboardShadow"), p.dashboardShadow);
    p.dashboardTransparent = boolean(settings, QStringLiteral("dashboardTransparent"), false);
    p.trayShadow = boolean(settings, QStringLiteral("trayShadow"), true);
    p.trayTransparent = boolean(settings, QStringLiteral("trayTransparent"), true);
    p.dashboardUseGlobal = boolean(settings, QStringLiteral("dashboardUseGlobal"), true);
    p.trayUseGlobal = boolean(settings, QStringLiteral("trayUseGlobal"), true);
    const auto interval = [&settings](const QString &key) {
        const double value = number(settings, key, 5, maximumRefreshSeconds);
        return value >= minimumRefreshSeconds ? value : 5;
    };
    p.dashboardInterval = interval(QStringLiteral("dashboardInterval"));
    p.trayInterval = interval(QStringLiteral("trayInterval"));
    p.dashboardGauge = boolean(settings, QStringLiteral("dashboardGauge"), p.dashboardGauge)
        && id != QStringLiteral("system/uptime");
    const QString style = settings.value(QStringLiteral("dashboardTextStyle")).toString();
    if (style == QStringLiteral("Normal")) p.dashboardTextStyle = DashboardTextStyle::Normal;
    else if (style == QStringLiteral("Digital")) p.dashboardTextStyle = DashboardTextStyle::Digital;
    p.memoryAsPercent = boolean(settings, QStringLiteral("memoryAsPercent"), false);
    p.trayShowUnit = boolean(settings, QStringLiteral("trayShowUnit"), p.trayShowUnit && !p.memoryAsPercent);
    p.trayDigital = boolean(settings, QStringLiteral("trayDigital"), false);
    p.trayFontFamily = settings.value(QStringLiteral("trayFontFamily")).toString();
    p.rangeLower = number(settings, QStringLiteral("rangeLower"), 0, 1e12);
    p.rangeUpper = number(settings, QStringLiteral("rangeUpper"), 100, 1e12);

    p.customRange = boolean(settings, QStringLiteral("customRange"), false) && p.rangeLower < p.rangeUpper;
    return p;
}

bool validPreferences(const SensorPreferences &p)
{
    return p.alarm.valid() && std::isfinite(p.multiplier) && std::abs(p.multiplier) <= 1e6
        && std::isfinite(p.offset) && std::abs(p.offset) <= 1e12
        && std::isfinite(p.rangeLower) && std::abs(p.rangeLower) <= 1e12
        && std::isfinite(p.rangeUpper) && std::abs(p.rangeUpper) <= 1e12
        && (!p.customRange || p.rangeLower < p.rangeUpper)
        && std::isfinite(p.dashboardInterval) && p.dashboardInterval >= minimumRefreshSeconds && p.dashboardInterval <= maximumRefreshSeconds
        && std::isfinite(p.trayInterval) && p.trayInterval >= minimumRefreshSeconds && p.trayInterval <= maximumRefreshSeconds
        && (p.dashboardTextStyle == DashboardTextStyle::Normal || p.dashboardTextStyle == DashboardTextStyle::Digital);
}
}

SensorPreferencesStore::SensorPreferencesStore(QObject *parent) : QObject(parent)
{
    QSettings settings;
    const QString unit = settings.value(QStringLiteral("Display/temperatureUnit"), QStringLiteral("Celsius")).toString().toLower();
    if (unit == QStringLiteral("fahrenheit")) m_unit = TemperatureUnit::Fahrenheit;
    else if (unit == QStringLiteral("kelvin")) m_unit = TemperatureUnit::Kelvin;
    for (const auto &group : settings.childGroups()) {
        if (!group.startsWith(QStringLiteral("Sensor-"))) continue;
        settings.beginGroup(group);
        const QString id = sensorInternalId(settings.value(QStringLiteral("id")).toString());
        if (!id.isEmpty()) {
            auto p = readPreferences(settings, id);
            if (!sensorSupportsTray(id)) {
                p.tray = false;
                p.alarm.trayMarkers = p.alarm.trayBlink = p.alarm.trayColor = false;
            }
            m_values.insert(id, p);
        }
        settings.endGroup();
    }
    for (auto it = m_values.begin(); it != m_values.end();) {
        if (it.value() == defaultSensorPreferences(it.key())) it = m_values.erase(it);
        else ++it;
    }
}

QStringList SensorPreferencesStore::trayIds() const
{
    QStringList ids;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        if (it->tray && sensorSupportsTray(it.key()))
            ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool SensorPreferencesStore::apply(const QHash<QString, SensorPreferences> &values,
                                  TemperatureUnit unit, QString &error)
{
    if (unit != TemperatureUnit::Celsius && unit != TemperatureUnit::Fahrenheit && unit != TemperatureUnit::Kelvin) {
        error = tr("Invalid temperature unit.");
        return false;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.key().isEmpty() || !validPreferences(it.value())) {
            error = tr("Invalid alarm, calibration, display range, or refresh preferences for %1.").arg(it.key());
            return false;
        }
    }
    // Merge missing devices, then discard sensors that have no overrides.
    auto merged = m_values;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        auto p = it.value();
        // Settings drafts do not edit layout. A drag may have updated it since the dialog opened.
        p.position = p.dashboard ? m_values.value(it.key()).position : std::nullopt;
        if (!sensorSupportsTray(it.key())) {
            p.tray = false;
            p.alarm.trayMarkers = p.alarm.trayBlink = p.alarm.trayColor = false;
        }
        if (p == defaultSensorPreferences(it.key())) merged.remove(it.key());
        else merged.insert(it.key(), p);
    }
    if (!write(merged, unit, error)) return false;
    m_values = merged;
    const bool unitChanged = m_unit != unit;
    m_unit = unit;
    if (unitChanged) emit temperatureUnitChanged(unit);
    emit changed();
    return true;
}

bool SensorPreferencesStore::setTemperatureUnit(TemperatureUnit unit, QString &error)
{
    if (unit == m_unit) return true;
    // An empty sensor delta changes only the global unit and preserves overrides.
    return apply({}, unit, error);
}

bool SensorPreferencesStore::savePositions(const QHash<QString, QPoint> &positions, QString &error)
{
    auto updated = m_values;
    QSet<QPoint> occupied;
    constexpr int limit = dashboardCellLimit;
    for (auto it = updated.begin(); it != updated.end(); ++it) {
        if (!it->dashboard || !positions.contains(it.key())) { it->position.reset(); continue; }
        const QPoint cell = positions.value(it.key());
        if (cell.x() < 0 || cell.y() < 0 || cell.x() >= limit || cell.y() >= limit || occupied.contains(cell)) {
            error = tr("Invalid or conflicting dashboard cell.");
            return false;
        }
        occupied.insert(cell);
        it->position = cell;
    }
    if (updated == m_values) return true;
    if (!write(updated, m_unit, error)) return false;
    m_values = updated;
    return true;
}

bool SensorPreferencesStore::write(const QHash<QString, SensorPreferences> &values,
                                   TemperatureUnit unit, QString &error)
{
    QSettings settings;
    for (const auto &group : settings.childGroups()) {
        if (group.startsWith(QStringLiteral("Sensor-"))) settings.remove(group);
    }
    if (unit == TemperatureUnit::Celsius) settings.remove(QStringLiteral("Display/temperatureUnit"));
    else settings.setValue(QStringLiteral("Display/temperatureUnit"), unit == TemperatureUnit::Fahrenheit
        ? QStringLiteral("Fahrenheit") : QStringLiteral("Kelvin"));
    QStringList ids = values.keys();
    std::sort(ids.begin(), ids.end());
    int index = 0;
    for (const auto &id : ids) {
        const auto p = values.value(id);
        settings.beginGroup(QStringLiteral("Sensor-%1").arg(++index, 3, 10, QLatin1Char('0')));
        settings.setValue(QStringLiteral("id"), sensorConfigId(id));
        writeAlarmSettings(settings, p.alarm);
        if (p.dashboard) settings.setValue(QStringLiteral("dashboard"), true);
        if (p.dashboard && p.position) {
            settings.setValue(QStringLiteral("column"), p.position->x());
            settings.setValue(QStringLiteral("row"), p.position->y());
        }
        if (p.tray) settings.setValue(QStringLiteral("tray"), true);
        if (!p.label.isEmpty()) settings.setValue(QStringLiteral("label"), p.label);
        if (p.multiplier != 1) settings.setValue(QStringLiteral("multiplier"), p.multiplier);
        if (p.offset != 0) settings.setValue(QStringLiteral("offset"), p.offset);
        if (p.foreground.isValid()) settings.setValue(QStringLiteral("foreground"), p.foreground.name(QColor::HexArgb));
        if (p.background.isValid()) settings.setValue(QStringLiteral("background"), p.background.name(QColor::HexArgb));
        // Write the split tray foreground explicitly, including its system-default sentinel.
        settings.setValue(QStringLiteral("trayForeground"), p.trayForeground.isValid() ? p.trayForeground.name(QColor::HexArgb) : QStringLiteral("default"));
        if (p.trayBackground.isValid()) settings.setValue(QStringLiteral("trayBackground"), p.trayBackground.name(QColor::HexArgb));
        const auto defaults = defaultSensorPreferences(id);
        if (p.dashboardShadow != defaults.dashboardShadow) settings.setValue(QStringLiteral("dashboardShadow"), p.dashboardShadow);
        if (p.dashboardTransparent) settings.setValue(QStringLiteral("dashboardTransparent"), true);
        if (!p.trayShadow) settings.setValue(QStringLiteral("trayShadow"), false);
        if (!p.trayTransparent) settings.setValue(QStringLiteral("trayTransparent"), false);
        if (!p.dashboardUseGlobal) settings.setValue(QStringLiteral("dashboardUseGlobal"), false);
        if (!p.trayUseGlobal) settings.setValue(QStringLiteral("trayUseGlobal"), false);
        if (p.dashboardInterval != 5) settings.setValue(QStringLiteral("dashboardInterval"), p.dashboardInterval);
        if (p.trayInterval != 5) settings.setValue(QStringLiteral("trayInterval"), p.trayInterval);
        if (p.dashboardGauge != defaults.dashboardGauge) settings.setValue(QStringLiteral("dashboardGauge"), p.dashboardGauge);
        if (p.dashboardTextStyle != defaults.dashboardTextStyle) settings.setValue(QStringLiteral("dashboardTextStyle"),
            p.dashboardTextStyle == DashboardTextStyle::Digital ? QStringLiteral("Digital") : QStringLiteral("Normal"));
        if (p.trayShowUnit != defaults.trayShowUnit) settings.setValue(QStringLiteral("trayShowUnit"), p.trayShowUnit);
        if (p.trayDigital) settings.setValue(QStringLiteral("trayDigital"), true);
        if (!p.trayFontFamily.isEmpty()) settings.setValue(QStringLiteral("trayFontFamily"), p.trayFontFamily);
        if (p.memoryAsPercent) settings.setValue(QStringLiteral("memoryAsPercent"), true);
        if (p.customRange) settings.setValue(QStringLiteral("customRange"), true);
        if (p.rangeLower != 0) settings.setValue(QStringLiteral("rangeLower"), p.rangeLower);
        if (p.rangeUpper != 100) settings.setValue(QStringLiteral("rangeUpper"), p.rangeUpper);
        settings.endGroup();
    }
    settings.remove(QStringLiteral("Dashboard/gauges"));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        error = tr("The new settings could not be saved. Check configuration-directory permissions.");
        return false;
    }
    return true;
}

} // namespace qsensors
