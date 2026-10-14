// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"
#include <QBrush>
#include <QPalette>
#include <QRectF>

class QPainter;

namespace qsensors {

enum class DashboardPalette { System, Classic, Graphite, Custom };
enum class DashboardBackground { Solid, Dots, Grid };

struct DashboardAppearance {
    QPalette colors;
    QColor pattern;
    QColor gaugeTrack;
    QColor needle;
    QColor uptime;
    QColor gaugeNormal;
    QColor gaugeTransition;
    QColor gaugeEnd;
    QColor voltageNormal;
    QColor voltageTransition;
    QColor voltageEnd;
    QColor voltageNeedle;
    bool coloredGauges = false;
};

QString dashboardPaletteKey(DashboardPalette preset);
DashboardPalette dashboardPaletteFromKey(const QString &key);
QString dashboardBackgroundKey(DashboardBackground pattern);
DashboardBackground dashboardBackgroundFromKey(const QString &key);
DashboardAppearance dashboardAppearance(const QPalette &system, DashboardPalette preset);
void saveCustomDashboardAppearance(const DashboardAppearance &appearance);
// Small brushes are rebuilt only when appearance changes, never per tile/frame.
QBrush dashboardBackgroundBrush(const DashboardAppearance &appearance, DashboardBackground pattern);
void drawDashboardGauge(QPainter &painter, const QRectF &arc, qreal width,
                        SensorKind kind, const DashboardAppearance &appearance, bool available);

} // namespace qsensors
