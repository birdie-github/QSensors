// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QPoint>
#include <optional>
#include <limits>

namespace qsensors {

// Safe cell span even for 2048-pixel tiles plus the eight-pixel gap.
inline constexpr int dashboardCellLimit = (std::numeric_limits<int>::max() / 2056 - 4) / 2;

enum class DashboardTextStyle { Normal, Digital };
inline constexpr double minimumRefreshSeconds = 0.05;
inline constexpr double maximumRefreshSeconds = 3600;

enum class TemperatureUnit { Celsius, Fahrenheit, Kelvin };

double temperatureToDisplay(double celsius, TemperatureUnit unit);
double temperatureFromDisplay(double value, TemperatureUnit unit);
double temperatureDifferenceToDisplay(double celsius, TemperatureUnit unit);
double temperatureDifferenceFromDisplay(double value, TemperatureUnit unit);
QString temperatureUnitText(TemperatureUnit unit);
// The file stores readable identities; runtime IDs retain provider encoding.
QString sensorConfigId(const QString &id);
QString sensorInternalId(const QString &id);

struct SensorPreferences {
    std::optional<QPoint> position; // Dashboard cell, saved with the sensor.
    bool dashboard = false;
    bool tray = false;
    QString label;
    double multiplier = 1;
    double offset = 0; // Canonical units; temperature offsets are Celsius differences.
    QColor foreground; // Invalid means system palette.
    QColor background;
    double dashboardInterval = 5;
    double trayInterval = 5;
    bool dashboardUseGlobal = true;
    bool trayUseGlobal = true;
    bool dashboardGauge = true;
    DashboardTextStyle dashboardTextStyle = DashboardTextStyle::Normal;
    bool dashboardShadow = false;
    bool dashboardTransparent = false;
    QColor trayForeground;
    QColor trayBackground;
    bool trayShadow = true; // Contrasting offset shadow behind numeric icons.
    bool trayTransparent = true;
    bool trayDigital = false; // Filled digital readout for frequency sensors only.
    QString trayFontFamily; // Empty follows the system fixed font.
    bool memoryAsPercent = false; // Available RAM display; canonical correction/range stays MiB.
    bool customRange = false;
    double rangeLower = 0; // Canonical absolute values, never alarm thresholds.
    double rangeUpper = 100;
    bool operator==(const SensorPreferences &other) const;
};

SensorPreferences defaultSensorPreferences(const QString &id);

QColor dashboardTextColor(const SensorPreferences &preferences, const QColor &systemText);

class SensorPreferencesStore final : public QObject {
    Q_OBJECT
public:
    explicit SensorPreferencesStore(QObject *parent = nullptr);
    SensorPreferences value(const QString &id) const { return m_values.value(id, defaultSensorPreferences(id)); }
    QHash<QString, SensorPreferences> values() const { return m_values; }
    TemperatureUnit temperatureUnit() const { return m_unit; }
    QStringList trayIds() const;
    bool apply(const QHash<QString, SensorPreferences> &values, TemperatureUnit unit, QString &error);
    // Layout-only updates preserve current preferences and do not refresh sensor consumers.
    bool savePositions(const QHash<QString, QPoint> &positions, QString &error);

signals:
    void changed();

private:
    bool write(const QHash<QString, SensorPreferences> &values, TemperatureUnit unit, QString &error);
    QHash<QString, SensorPreferences> m_values;
    TemperatureUnit m_unit = TemperatureUnit::Celsius;
};

} // namespace qsensors
