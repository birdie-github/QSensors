// SPDX-License-Identifier: GPL-3.0-only
#include "settings/sensorpreferences.h"

#include <QSettings>
#include <QUrl>

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

bool SensorPreferences::operator==(const SensorPreferences &other) const
{
    return dashboard == other.dashboard && tray == other.tray && label == other.label
        && multiplier == other.multiplier && offset == other.offset
        && foreground == other.foreground && background == other.background
        && memoryAsPercent == other.memoryAsPercent
        && customRange == other.customRange && rangeLower == other.rangeLower && rangeUpper == other.rangeUpper;
}

QColor dashboardTextColor(const SensorPreferences &preferences, const QColor &systemText)
{
    if (preferences.foreground.isValid())
        return preferences.foreground;
    if (preferences.background.isValid())
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

SensorPreferences readPreferences(QSettings &settings)
{
    SensorPreferences p;
    p.dashboard = boolean(settings, QStringLiteral("dashboard"), false);
    p.tray = boolean(settings, QStringLiteral("tray"), false);
    p.label = settings.value(QStringLiteral("label")).toString();
    p.multiplier = number(settings, QStringLiteral("multiplier"), 1, 1e6);
    p.offset = number(settings, QStringLiteral("offset"), 0, 1e12);
    p.foreground = QColor(settings.value(QStringLiteral("foreground")).toString());
    p.background = QColor(settings.value(QStringLiteral("background")).toString());
    p.rangeLower = number(settings, QStringLiteral("rangeLower"), 0, 1e12);
    p.rangeUpper = number(settings, QStringLiteral("rangeUpper"), 100, 1e12);
    p.memoryAsPercent = boolean(settings, QStringLiteral("memoryAsPercent"), false);
    p.customRange = boolean(settings, QStringLiteral("customRange"), false) && p.rangeLower < p.rangeUpper;
    return p;
}

bool validPreferences(const SensorPreferences &p)
{
    return std::isfinite(p.multiplier) && std::abs(p.multiplier) <= 1e6
        && std::isfinite(p.offset) && std::abs(p.offset) <= 1e12
        && std::isfinite(p.rangeLower) && std::abs(p.rangeLower) <= 1e12
        && std::isfinite(p.rangeUpper) && std::abs(p.rangeUpper) <= 1e12
        && (!p.customRange || p.rangeLower < p.rangeUpper);
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
        if (!id.isEmpty()) m_values.insert(id, readPreferences(settings));
        settings.endGroup();
    }
    for (auto it = m_values.begin(); it != m_values.end();) {
        if (it.value() == SensorPreferences{}) it = m_values.erase(it);
        else ++it;
    }
}

QStringList SensorPreferencesStore::trayIds() const
{
    QStringList ids;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        if (it->tray)
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
            error = tr("Invalid calibration or display range for %1.").arg(it.key());
            return false;
        }
    }
    // Merge missing devices, then discard sensors that have no overrides.
    auto merged = m_values;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.value() == SensorPreferences{}) merged.remove(it.key());
        else merged.insert(it.key(), it.value());
    }
    QSettings settings;
    for (const auto &group : settings.childGroups()) {
        if (group.startsWith(QStringLiteral("Sensor-"))) settings.remove(group);
    }
    if (unit == TemperatureUnit::Celsius) settings.remove(QStringLiteral("Display/temperatureUnit"));
    else settings.setValue(QStringLiteral("Display/temperatureUnit"), unit == TemperatureUnit::Fahrenheit
        ? QStringLiteral("Fahrenheit") : QStringLiteral("Kelvin"));
    QStringList ids = merged.keys();
    std::sort(ids.begin(), ids.end());
    int index = 0;
    for (const auto &id : ids) {
        const auto p = merged.value(id);
        settings.beginGroup(QStringLiteral("Sensor-%1").arg(++index, 3, 10, QLatin1Char('0')));
        settings.setValue(QStringLiteral("id"), sensorConfigId(id));
        if (p.dashboard) settings.setValue(QStringLiteral("dashboard"), true);
        if (p.tray) settings.setValue(QStringLiteral("tray"), true);
        if (!p.label.isEmpty()) settings.setValue(QStringLiteral("label"), p.label);
        if (p.multiplier != 1) settings.setValue(QStringLiteral("multiplier"), p.multiplier);
        if (p.offset != 0) settings.setValue(QStringLiteral("offset"), p.offset);
        if (p.foreground.isValid()) settings.setValue(QStringLiteral("foreground"), p.foreground.name(QColor::HexArgb));
        if (p.background.isValid()) settings.setValue(QStringLiteral("background"), p.background.name(QColor::HexArgb));
        if (p.memoryAsPercent) settings.setValue(QStringLiteral("memoryAsPercent"), true);
        if (p.customRange) settings.setValue(QStringLiteral("customRange"), true);
        if (p.rangeLower != 0) settings.setValue(QStringLiteral("rangeLower"), p.rangeLower);
        if (p.rangeUpper != 100) settings.setValue(QStringLiteral("rangeUpper"), p.rangeUpper);
        settings.endGroup();
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        error = tr("The new settings could not be saved. Check configuration-directory permissions.");
        return false;
    }
    m_values = merged;
    m_unit = unit;
    emit changed();
    return true;
}

} // namespace qsensors
