// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QStringList>

namespace qsensors {

enum class TemperatureUnit { Celsius, Fahrenheit, Kelvin };

double temperatureToDisplay(double celsius, TemperatureUnit unit);
double temperatureFromDisplay(double value, TemperatureUnit unit);
double temperatureDifferenceToDisplay(double celsius, TemperatureUnit unit);
double temperatureDifferenceFromDisplay(double value, TemperatureUnit unit);
QString temperatureUnitText(TemperatureUnit unit);

struct SensorPreferences {
    bool dashboard = false;
    bool tray = false;
    QString label;
    double multiplier = 1;
    double offset = 0; // Canonical units; temperature offsets are Celsius differences.
    QColor foreground; // Invalid means system palette.
    QColor background;
    bool customRange = false;
    double rangeLower = 0; // Canonical absolute values, never alarm thresholds.
    double rangeUpper = 100;
    bool operator==(const SensorPreferences &other) const;
};

QColor dashboardTextColor(const SensorPreferences &preferences, const QColor &systemText);

class SensorPreferencesStore final : public QObject {
    Q_OBJECT
public:
    explicit SensorPreferencesStore(QObject *parent = nullptr);
    SensorPreferences value(const QString &id) const { return m_values.value(id); }
    QHash<QString, SensorPreferences> values() const { return m_values; }
    TemperatureUnit temperatureUnit() const { return m_unit; }
    QStringList trayIds() const;
    bool apply(const QHash<QString, SensorPreferences> &values, TemperatureUnit unit, QString &error);

signals:
    void changed();

private:
    QHash<QString, SensorPreferences> m_values;
    TemperatureUnit m_unit = TemperatureUnit::Celsius;
};

} // namespace qsensors
