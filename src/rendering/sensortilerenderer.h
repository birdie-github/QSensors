// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"
#include "rendering/dashboardappearance.h"
#include "settings/sensorpreferences.h"

#include <QFont>
#include <QPalette>
#include <QRectF>

class QPainter;

namespace qsensors {

inline constexpr int minimumTileSize = 64;
inline constexpr int maximumTileSize = 2048;
enum class DashboardTileShape { RoundedRectangle, Square, Circle };

struct GaugeRange {
    double lower = 0;
    double upper = 1;
};

struct TileReading {
    bool available = false;
    QString text;
    QString unit;
    QString digitalText;
    QString digitalUnit;
    QString days;
    QColor valueColor;
    double fraction = 0;
    GaugeRange range;
    QString lowerText;
    QString upperText;
};

// Geometry/fonts are shared by the whole grid and rebuilt only on size/font changes.
struct TileStyle {
    int size = 128;
    DashboardTileShape shape = DashboardTileShape::RoundedRectangle;
    qreal contentScale = 1;
    QFont valueFont;
    QFont unitFont;
    QFont labelFont;
    QFont detailFont;
};

class SensorTileRenderer {
public:
    static TileStyle style(int size, const QFont &baseFont,
                           DashboardTileShape shape = DashboardTileShape::RoundedRectangle,
                           int circleShrinkPercent = 20);
    // Prepare only the selected readout and enabled gauge data; the grid tells
    // us whether endpoint labels are present at its current tile size.
    static TileReading prepare(const SensorDescriptor &descriptor, const SensorSample &sample,
                               const SensorPreferences &preferences, TemperatureUnit unit, bool gaugeLabels);
    static void draw(QPainter &painter, const QRectF &rect, const SensorDescriptor &descriptor,
                     const TileReading &reading, const TileStyle &style,
                     const QPalette &palette, bool selected, const SensorPreferences &preferences,
                     const DashboardAppearance &appearance);
};

} // namespace qsensors
