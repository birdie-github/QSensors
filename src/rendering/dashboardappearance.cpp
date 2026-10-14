// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/dashboardappearance.h"

#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <array>

namespace qsensors {

QString dashboardPaletteKey(DashboardPalette preset)
{
    switch (preset) {
    case DashboardPalette::Classic: return QStringLiteral("classic");
    case DashboardPalette::Graphite: return QStringLiteral("graphite");
    case DashboardPalette::Custom: return QStringLiteral("custom");
    default: return QStringLiteral("system");
    }
}
DashboardPalette dashboardPaletteFromKey(const QString &key)
{
    if (key == QStringLiteral("classic")) return DashboardPalette::Classic;
    if (key == QStringLiteral("graphite")) return DashboardPalette::Graphite;
    if (key == QStringLiteral("custom")) return DashboardPalette::Custom;
    return DashboardPalette::System;
}
QString dashboardBackgroundKey(DashboardBackground pattern)
{
    switch (pattern) {
    case DashboardBackground::Dots: return QStringLiteral("dots");
    case DashboardBackground::Grid: return QStringLiteral("grid");
    default: return QStringLiteral("solid");
    }
}
DashboardBackground dashboardBackgroundFromKey(const QString &key)
{
    if (key == QStringLiteral("dots")) return DashboardBackground::Dots;
    if (key == QStringLiteral("grid")) return DashboardBackground::Grid;
    return DashboardBackground::Solid;
}
namespace {
// One inventory drives custom-color persistence; no widget/application palette is modified.
struct PaletteRole { const char *key; QPalette::ColorGroup group; QPalette::ColorRole role; };
constexpr PaletteRole paletteRoles[] = {
    {"canvas", QPalette::Active, QPalette::Window},
    {"tile", QPalette::Active, QPalette::Base},
    {"text", QPalette::Active, QPalette::Text},
    {"unavailableText", QPalette::Disabled, QPalette::Text},
    {"border", QPalette::Active, QPalette::Mid},
    {"selection", QPalette::Active, QPalette::Highlight}
};
struct AppearanceColor { const char *key; QColor DashboardAppearance::*member; };
constexpr AppearanceColor appearanceColors[] = {
    {"pattern", &DashboardAppearance::pattern},
    {"gaugeTrack", &DashboardAppearance::gaugeTrack},
    {"needle", &DashboardAppearance::needle},
    {"uptime", &DashboardAppearance::uptime},
    {"gaugeNormal", &DashboardAppearance::gaugeNormal},
    {"gaugeTransition", &DashboardAppearance::gaugeTransition},
    {"gaugeEnd", &DashboardAppearance::gaugeEnd},
    {"voltageNormal", &DashboardAppearance::voltageNormal},
    {"voltageTransition", &DashboardAppearance::voltageTransition},
    {"voltageEnd", &DashboardAppearance::voltageEnd},
    {"voltageNeedle", &DashboardAppearance::voltageNeedle}
};
}
DashboardAppearance dashboardAppearance(const QPalette &system, DashboardPalette preset)
{
    DashboardAppearance appearance;
    QPalette &colors = appearance.colors;
    colors = system;
    if (preset != DashboardPalette::System) {
        // Custom colors fall back to Classic if a stored entry is absent/invalid.
        const bool classic = preset != DashboardPalette::Graphite;
        colors.setColor(QPalette::Window, classic ? QColor(Qt::white) : QColor(24, 26, 29));
        colors.setColor(QPalette::Base, classic ? QColor(Qt::white) : QColor(36, 39, 43));
        colors.setColor(QPalette::Text, classic ? QColor(Qt::black) : QColor(235, 237, 239));
        colors.setColor(QPalette::Mid, classic ? QColor(128, 128, 128) : QColor(104, 110, 118));
        colors.setColor(QPalette::Highlight, classic ? QColor(160, 0, 0) : QColor(255, 190, 80));
        colors.setColor(QPalette::Disabled, QPalette::Text,
                        classic ? QColor(128, 128, 128) : QColor(144, 150, 158));
    }
    appearance.pattern = colors.color(QPalette::Text);
    appearance.pattern.setAlpha(28);
    appearance.gaugeTrack = colors.color(QPalette::Mid);
    appearance.needle = preset == DashboardPalette::Classic || preset == DashboardPalette::Custom
        ? colors.color(QPalette::Text) : colors.color(QPalette::Highlight);
    appearance.uptime = QColor(Qt::red);
    appearance.gaugeNormal = QColor(Qt::green);
    appearance.gaugeTransition = QColor(127, 127, 0);
    appearance.gaugeEnd = QColor(Qt::red);
    appearance.voltageNormal = QColor(0, 200, 150);
    appearance.voltageTransition = QColor(0, 100, 202);
    appearance.voltageEnd = QColor(Qt::blue);
    appearance.coloredGauges = preset == DashboardPalette::Classic || preset == DashboardPalette::Custom;
    appearance.voltageNeedle = appearance.coloredGauges ? QColor(Qt::darkBlue) : appearance.needle;
    if (preset == DashboardPalette::Custom) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Dashboard/CustomColors"));
        for (const auto &entry : paletteRoles) {
            const QColor color(settings.value(QLatin1String(entry.key)).toString());
            if (!color.isValid()) continue;
            if (entry.group == QPalette::Disabled) colors.setColor(entry.group, entry.role, color);
            else colors.setColor(entry.role, color);
        }
        for (const auto &entry : appearanceColors) {
            const QColor color(settings.value(QLatin1String(entry.key)).toString());
            if (color.isValid()) appearance.*entry.member = color;
        }
        appearance.coloredGauges = settings.value(QStringLiteral("coloredGauges"), true).toBool();
    }
    return appearance;
}
void saveCustomDashboardAppearance(const DashboardAppearance &appearance)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Dashboard/CustomColors"));
    for (const auto &entry : paletteRoles)
        settings.setValue(QLatin1String(entry.key), appearance.colors.color(entry.group, entry.role).name(QColor::HexArgb));
    for (const auto &entry : appearanceColors)
        settings.setValue(QLatin1String(entry.key), (appearance.*entry.member).name(QColor::HexArgb));
    settings.setValue(QStringLiteral("coloredGauges"), appearance.coloredGauges);
}
QBrush dashboardBackgroundBrush(const DashboardAppearance &appearance, DashboardBackground pattern)
{
    const QColor background = appearance.colors.color(QPalette::Window);
    if (pattern == DashboardBackground::Solid) return QBrush(background);
    QPixmap texture(16, 16);
    texture.fill(background);
    QPainter painter(&texture);
    painter.setPen(appearance.pattern);
    if (pattern == DashboardBackground::Dots) painter.drawPoint(0, 0);
    else {
        painter.drawLine(0, 0, 15, 0);
        painter.drawLine(0, 1, 0, 15);
    }
    painter.end();
    return QBrush(texture);
}
void drawDashboardGauge(QPainter &painter, const QRectF &arc, qreal width,
                        SensorKind kind, const DashboardAppearance &appearance, bool available)
{
    if (!appearance.coloredGauges || !available) {
        painter.setPen(QPen(appearance.gaugeTrack, width, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, 0, 180 * 16);
        return;
    }
    // Original KSensors used green/red, with blue/teal for voltage. These
    // fixed bands are decorative range guides, not hardware alarm thresholds.
    const QColor green = appearance.gaugeNormal, red = appearance.gaugeEnd, olive = appearance.gaugeTransition;
    std::array<QColor, 5> bands{green, green, green, olive, red};
    if (kind == SensorKind::Fan) bands = {red, olive, green, green, green};
    else if (kind == SensorKind::Voltage)
        bands = {appearance.voltageEnd, appearance.voltageTransition, appearance.voltageNormal,
                 appearance.voltageTransition, appearance.voltageEnd};
    constexpr std::array<int, 6> boundaries{0, 10, 20, 80, 90, 100};
    for (int i = 0; i < 5; ++i) {
        painter.setPen(QPen(bands[i], width, Qt::SolidLine, Qt::FlatCap));
        const int start = 180 * 16 * (100 - boundaries[i]) / 100;
        const int span = -180 * 16 * (boundaries[i + 1] - boundaries[i]) / 100;
        painter.drawArc(arc, start, span);
    }
}

} // namespace qsensors
