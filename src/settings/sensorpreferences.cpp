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

bool SensorPreferences::operator==(const SensorPreferences &other) const
{
    return dashboard == other.dashboard && tray == other.tray && label == other.label
        && multiplier == other.multiplier && offset == other.offset
        && foreground == other.foreground && background == other.background
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
QString groupName(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

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
    const int unit = settings.value(QStringLiteral("Display/temperatureUnit"), 0).toInt();
    if (unit >= 0 && unit <= 2)
        m_unit = static_cast<TemperatureUnit>(unit);
    // Preserve selections made by the phase-3 dialog, including absent devices.
    for (const auto &id : settings.value(QStringLiteral("Tray/sensors")).toStringList()) {
        if (!id.isEmpty())
            m_values[id].tray = true;
    }
    settings.beginGroup(QStringLiteral("Sensors"));
    for (const auto &group : settings.childGroups()) {
        settings.beginGroup(group);
        const QString id = settings.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && group == groupName(id)) {
            SensorPreferences p;
            p.dashboard = boolean(settings, QStringLiteral("dashboard"), true);
            p.tray = boolean(settings, QStringLiteral("tray"), false);
            p.label = settings.value(QStringLiteral("label")).toString();
            p.multiplier = number(settings, QStringLiteral("multiplier"), 1, 1e6);
            p.offset = number(settings, QStringLiteral("offset"), 0, 1e12);
            p.foreground = QColor(settings.value(QStringLiteral("foreground")).toString());
            p.background = QColor(settings.value(QStringLiteral("background")).toString());
            p.rangeLower = number(settings, QStringLiteral("rangeLower"), 0, 1e12);
            p.rangeUpper = number(settings, QStringLiteral("rangeUpper"), 100, 1e12);
            p.customRange = boolean(settings, QStringLiteral("customRange"), false) && p.rangeLower < p.rangeUpper;
            m_values.insert(id, p);
        }
        settings.endGroup();
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
    QSettings settings;
    bool modified = unit != m_unit;
    if (modified)
        settings.setValue(QStringLiteral("Display/temperatureUnit"), static_cast<int>(unit));
    settings.beginGroup(QStringLiteral("Sensors"));
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (m_values.contains(it.key()) && m_values.value(it.key()) == it.value())
            continue;
        const auto &p = it.value();
        modified = true;
        settings.beginGroup(groupName(it.key()));
        settings.setValue(QStringLiteral("id"), it.key());
        settings.setValue(QStringLiteral("dashboard"), p.dashboard);
        settings.setValue(QStringLiteral("tray"), p.tray);
        settings.setValue(QStringLiteral("label"), p.label);
        settings.setValue(QStringLiteral("multiplier"), p.multiplier);
        settings.setValue(QStringLiteral("offset"), p.offset);
        settings.setValue(QStringLiteral("foreground"), p.foreground.isValid() ? p.foreground.name(QColor::HexArgb) : QString());
        settings.setValue(QStringLiteral("background"), p.background.isValid() ? p.background.name(QColor::HexArgb) : QString());
        settings.setValue(QStringLiteral("customRange"), p.customRange);
        settings.setValue(QStringLiteral("rangeLower"), p.rangeLower);
        settings.setValue(QStringLiteral("rangeUpper"), p.rangeUpper);
        settings.endGroup();
    }
    settings.endGroup();
    if (!modified)
        return true;
    // Merge rather than remove preferences for absent devices, but do not
    // change the active store until persistence succeeds.
    auto merged = m_values;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        merged.insert(it.key(), it.value());
    QStringList selected;
    for (auto it = merged.cbegin(); it != merged.cend(); ++it) {
        if (it->tray)
            selected.append(it.key());
    }
    std::sort(selected.begin(), selected.end());
    settings.setValue(QStringLiteral("Tray/sensors"), selected);
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
