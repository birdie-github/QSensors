// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"
#include "settings/sensorpreferences.h"

#include <QFont>
#include <QPalette>
#include <QRectF>

class QPainter;

namespace qsensors {

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
    QFont valueFont;
    QFont unitFont;
    QFont labelFont;
    QFont detailFont;
};

class SensorTileRenderer {
public:
    static TileStyle style(int size, const QFont &baseFont);
    static TileReading prepare(const SensorDescriptor &descriptor, const SensorSample &sample,
                               const SensorPreferences &preferences, TemperatureUnit unit);
    static void draw(QPainter &painter, const QRectF &rect, const SensorDescriptor &descriptor,
                     const TileReading &reading, const TileStyle &style,
                     const QPalette &palette, bool selected, const SensorPreferences &preferences);
};

} // namespace qsensors
