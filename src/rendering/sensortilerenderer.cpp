// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/sensortilerenderer.h"
#include "rendering/valueformat.h"
#include "rendering/digitaltext.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {
namespace {

// Display guidance only, never alarm thresholds. Expand ordinary type-aware
// bounds for readings outside them; never interpret negative values as failures.
GaugeRange gaugeRange(SensorKind kind, double value)
{
    GaugeRange range;
    switch (kind) {
    case SensorKind::Temperature: range = {-20, 120}; break;
    case SensorKind::Fan: range = {0, 3000}; break;
    case SensorKind::Voltage: range = value < 0 ? GaugeRange{-16, 0} : GaugeRange{0, 16}; break;
    case SensorKind::Current: range = {0, 10}; break;
    case SensorKind::Power: range = {0, 100}; break;
    case SensorKind::Energy: range = {0, 1000}; break;
    case SensorKind::Humidity:
    case SensorKind::Percentage: range = {0, 100}; break;
    case SensorKind::Frequency: return {0, 0}; // Only a provider limit or explicit custom range is valid.
    case SensorKind::Duration: range = {0, 86400}; break;
    case SensorKind::Memory: range = {0, 1024}; break;
    }
    const auto extend = [](double magnitude) {
        if (magnitude == 0)
            return 1.0;
        const double step = std::pow(10.0, std::floor(std::log10(magnitude)));
        const double rounded = std::ceil(magnitude / step) * step;
        return std::isfinite(rounded) ? rounded : magnitude;
    };
    if (value < range.lower)
        range.lower = -extend(-value);
    if (value > range.upper)
        range.upper = extend(value);
    return range;
}

void fittedText(QPainter &painter, const QRectF &rect, const QString &text,
                QFont font, bool elide, bool shadow = false)
{
    const QFontMetricsF metrics(font);
    qreal scale = std::min(1.0, rect.height() / metrics.height());
    const qreal measuredWidth = metrics.horizontalAdvance(text);
    if (!elide && measuredWidth > 0)
        scale = std::min(scale, rect.width() / measuredWidth);
    if (scale < 1.0)
        font.setPixelSize(std::max(1, static_cast<int>(font.pixelSize() * scale)));
    // Font hinting can round dimensions up after fitting. Keep complete numeric
    // values inside the box even with unusually wide fonts and tiny tiles.
    while (!elide && font.pixelSize() > 1
           && (QFontMetricsF(font).horizontalAdvance(text) > rect.width()
               || QFontMetricsF(font).height() > rect.height()))
        font.setPixelSize(font.pixelSize() - 1);
    painter.setFont(font);
    const QString visible = elide ? QFontMetricsF(font).elidedText(text, Qt::ElideRight, rect.width()) : text;
    if (shadow) {
        const QPen ink = painter.pen();
        painter.setPen(ink.color().lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white));
        const qreal offset = std::max(.6, rect.height() * .04);
        painter.drawText(rect.translated(offset, offset), Qt::AlignCenter | Qt::TextSingleLine, visible);
        painter.setPen(ink);
    }
    painter.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, visible);
}

} // namespace

TileStyle SensorTileRenderer::style(int size, const QFont &baseFont,
                                  DashboardTileShape shape, int circleShrinkPercent,
                                  const QFont *nameFont, const QFont *dataFont)
{
    TileStyle result;
    result.size = size;
    result.shape = shape;
    result.contentScale = shape == DashboardTileShape::Circle
        ? 1.0 - std::clamp(circleShrinkPercent, 0, 50) / 100.0 : 1.0;
    result.customNameFont = nameFont != nullptr;
    const auto font = [&baseFont, size](double fraction, int minimum, bool bold, const QFont *overrideFont) {
        QFont font = overrideFont ? *overrideFont : baseFont;
        font.setPixelSize(std::max(minimum, static_cast<int>(std::round(size * fraction))));
        if (!overrideFont) font.setBold(bold);
        return font;
    };
    result.valueFont = font(0.23, 10, true, dataFont);
    result.unitFont = font(0.105, 7, false, dataFont);
    result.labelFont = font(0.09, 7, false, nameFont);
    result.detailFont = font(0.06, 6, false, dataFont);
    return result;
}

TileReading SensorTileRenderer::prepare(const SensorDescriptor &descriptor, const SensorSample &sample,
                                       const SensorPreferences &preferences, TemperatureUnit unit, bool gaugeLabels)
{
    TileReading result;
    const bool digital = preferences.dashboardTextStyle == DashboardTextStyle::Digital;
    if (!digital) result.unit = descriptor.unit;
    if (descriptor.kind == SensorKind::Frequency) result.range = {0, 0};
    result.available = sample.available && std::isfinite(sample.value) && sample.timestamp.isValid();
    if (!result.available) {
        if (digital && descriptor.id != QStringLiteral("system/uptime")) result.digitalText = QStringLiteral("—");
        else result.text = QStringLiteral("—");
        // Uptime's digital painter uses the shared clock text field.
        if (descriptor.id == QStringLiteral("system/uptime")) result.digitalText = result.text;
        return result;
    }
    if (descriptor.id == QStringLiteral("system/uptime")) {
        const auto uptime = formatUptime(sample.value);
        result.available = sample.value >= 0;
        result.text = uptime.time;
        result.days = uptime.days;
        result.digitalText = uptime.time;
        result.unit.clear();
        result.valueColor = preferences.foreground;
        return result;
    }
    const QLocale locale;
    if (digital) {
        result.digitalText = formatDigitalSensorValue(descriptor, sample.value, unit == TemperatureUnit::Kelvin);
        if (descriptor.kind == SensorKind::Frequency) result.digitalUnit = QStringLiteral("MHz");
    } else {
        switch (descriptor.kind) {
        case SensorKind::Temperature: {
            const double rounded = std::abs(sample.value) <= std::numeric_limits<double>::max() / 10
                ? std::round(sample.value * 10) / 10 : sample.value;
            result.text = locale.toString(rounded == 0 ? 0.0 : rounded, 'f', 1);
            break;
        }
        case SensorKind::Humidity: result.text = locale.toString(sample.value, 'f', 1); break;
        case SensorKind::Percentage:
        case SensorKind::Frequency:
        case SensorKind::Memory: {
            const auto formatted = formatSensorValue(descriptor, sample.value);
            result.text = formatted.number;
            result.unit = formatted.unit;
            break;
        }
        case SensorKind::Fan: result.text = locale.toString(sample.value, 'f', 0); break;
        default: result.text = locale.toString(sample.value, 'g', 6); break;
        }
        // Keep essential numbers readable at the smallest tile size.
        if (result.text.size() > 9)
            result.text = locale.toString(sample.value, 'g', 4);
    }
    if (!preferences.dashboardGauge || !sensorSupportsGauge(descriptor.id, descriptor.kind)) return result;
    const bool temperature = descriptor.kind == SensorKind::Temperature;
    const double canonicalValue = temperature ? temperatureFromDisplay(sample.value, unit) : sample.value;
    result.range = preferences.customRange ? GaugeRange{preferences.rangeLower, preferences.rangeUpper}
                                          : gaugeRange(descriptor.kind, canonicalValue);
    if (!preferences.customRange && descriptor.kind == SensorKind::Frequency)
        result.range = std::isfinite(sample.maximum) && sample.maximum > 0
            ? GaugeRange{0, sample.maximum} : GaugeRange{0, 0};
    if (!preferences.customRange && descriptor.kind == SensorKind::Memory && std::isfinite(sample.maximum))
        result.range = {0, std::max(0.0, sample.maximum)};
    // Memory's canonical unit stays MiB even when its display descriptor is %.
    if (descriptor.id == QStringLiteral("system/memory/available") && preferences.memoryAsPercent && std::isfinite(sample.reference) && sample.reference > 0 && preferences.customRange) {
        result.range.lower = result.range.lower / sample.reference * 100;
        result.range.upper = result.range.upper / sample.reference * 100;
    }
    if (temperature) {
        result.range.lower = temperatureToDisplay(result.range.lower, unit);
        result.range.upper = temperatureToDisplay(result.range.upper, unit);
    }
    const double span = result.range.upper - result.range.lower;
    if (span > 0 && std::isfinite(span))
        result.fraction = std::clamp((sample.value - result.range.lower) / span, 0.0, 1.0);
    // Endpoint labels are drawn only on sufficiently large, valid gauges.
    if (gaugeLabels && span > 0 && std::isfinite(span)) {
        result.lowerText = locale.toString(result.range.lower, 'g', 6);
        result.upperText = locale.toString(result.range.upper, 'g', 6);
        if (descriptor.kind == SensorKind::Frequency || descriptor.kind == SensorKind::Memory) {
            const auto lower = formatSensorValue(descriptor, result.range.lower);
            const auto upper = formatSensorValue(descriptor, result.range.upper);
            result.lowerText = lower.number + lower.unit;
            result.upperText = upper.number + upper.unit;
        }
    }
    return result;
}

void SensorTileRenderer::draw(QPainter &painter, const QRectF &rect,
                             const SensorDescriptor &descriptor, const TileReading &reading,
                             const TileStyle &style, const QPalette &palette,
                             bool selected, const SensorPreferences &preferences, const DashboardAppearance &appearance)
{
    bool gauge = preferences.dashboardGauge && sensorSupportsGauge(descriptor.id, descriptor.kind);
    if (descriptor.kind == SensorKind::Frequency || descriptor.kind == SensorKind::Memory)
        gauge = gauge && std::isfinite(reading.range.upper) && reading.range.upper > reading.range.lower;
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.translate(rect.topLeft());
    const qreal size = style.size;
    const qreal padding = std::max(3.0, size * 0.05);
    painter.setRenderHint(QPainter::Antialiasing);
    if (preferences.dashboardTransparent) painter.setBrush(Qt::NoBrush);
    else painter.setBrush(palette.brush(QPalette::Base));
    const qreal borderWidth = style.shape == DashboardTileShape::Square ? 1.0
        : std::max(1.0, size * (selected ? 0.014 : 0.006));
    painter.setPen(QPen(palette.color(selected ? QPalette::Highlight : QPalette::Mid), borderWidth));
    if (style.shape == DashboardTileShape::Square)
        painter.drawRect(QRectF(.5, .5, size - 1, size - 1));
    else if (style.shape == DashboardTileShape::Circle)
        painter.drawEllipse(QRectF(borderWidth / 2, borderWidth / 2, size - borderWidth, size - borderWidth));
    else painter.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size * 0.06, size * 0.06);
    // Shrink all contents about the center, leaving the circular border intact.
    // Keep the rectangular cell clip: contents may protrude beyond the circle.
    if (style.contentScale != 1) {
        const qreal inset = size * (1 - style.contentScale) / 2;
        painter.translate(inset, inset);
        painter.scale(style.contentScale, style.contentScale);
    }
    const QColor textColor = palette.color(reading.available ? QPalette::Active : QPalette::Disabled,
                                          QPalette::Text);
    painter.setPen(textColor);
    const bool digital = preferences.dashboardTextStyle == DashboardTextStyle::Digital;
    if (descriptor.id == QStringLiteral("system/uptime") && digital) {
        const QColor legend = textColor;
        const QColor base = reading.available ? (reading.valueColor.isValid() ? reading.valueColor : appearance.uptime)
                                             : palette.color(QPalette::Disabled, QPalette::Text);
        if (reading.days.isEmpty()) {
            drawDigitalText(painter, QRectF(padding, size * .08, size - 2 * padding, size * .13), QStringLiteral("HH:MM"), legend);
            drawDigitalText(painter, QRectF(padding, size * .29, size - 2 * padding, size * .40), reading.text, base, preferences.dashboardShadow);
        } else {
            const QRectF daysRect(padding, size * .15, size - 2 * padding, size * .23);
            const qreal offset = std::max(.6, size * .007);
            if (preferences.dashboardShadow) {
                painter.setPen(base.darker(200));
                fittedText(painter, daysRect.translated(offset, offset), reading.days, style.valueFont, false);
            }
            painter.setPen(base);
            fittedText(painter, daysRect, reading.days, style.valueFont, false);
            drawDigitalText(painter, QRectF(padding, size * .44, size - 2 * padding, size * .29), reading.text, base, preferences.dashboardShadow);
        }
        const QRectF labelRect(padding, size * .84, size - 2 * padding, size * .11);
        if (preferences.label.isEmpty() && !style.customNameFont)
            drawDigitalText(painter, labelRect, QStringLiteral("UPTIME"), legend);
        else {
            painter.setPen(textColor);
            const QString label = preferences.label.isEmpty() ? QStringLiteral("UPTIME") : descriptor.label;
            fittedText(painter, labelRect, label, style.labelFont, true, preferences.dashboardShadow);
        }
        painter.restore();
        return;
    }
    const bool compact = style.size < 96;
    if (digital) {
        const bool watch = descriptor.kind == SensorKind::Frequency;
        const QRectF readout(padding, size * (gauge ? .07 : .12), size - 2 * padding,
                             size * (gauge ? (watch ? .29 : .38) : (watch ? .53 : .66)));
        drawDigitalText(painter, readout, reading.digitalText, textColor, preferences.dashboardShadow, watch);
        if (!reading.digitalUnit.isEmpty())
            fittedText(painter, QRectF(padding, size * (gauge ? .38 : .68), size - 2 * padding, size * .11),
                       reading.digitalUnit, style.unitFont, true, preferences.dashboardShadow);
    } else {
        const QColor valueColor = reading.valueColor.isValid() ? reading.valueColor
            : descriptor.id == QStringLiteral("system/uptime") ? appearance.uptime : textColor;
        const QColor ink = reading.available ? valueColor : textColor;
        painter.setPen(ink);
        fittedText(painter, QRectF(padding, size * (gauge ? .07 : .20), size - 2 * padding, size * .32),
                   reading.text, style.valueFont, false, preferences.dashboardShadow);
        painter.setPen(textColor);
        const QString detail = reading.days.isEmpty() ? reading.unit : reading.days;
        fittedText(painter, QRectF(padding, size * (gauge ? .36 : .56), size - 2 * padding, std::max(9.0, size * .13)),
                   detail, style.unitFont, true, preferences.dashboardShadow);
    }

    if (gauge) {
        const QRectF arc = compact ? QRectF(size * 0.28, size * 0.53, size * 0.44, size * 0.44)
                                   : QRectF(size * 0.24, size * 0.50, size * 0.52, size * 0.52);
        painter.setBrush(Qt::NoBrush);
        drawDashboardGauge(painter, arc, std::max(1.0, size * 0.02), descriptor.kind,
                           appearance, reading.available);
        const double span = reading.range.upper - reading.range.lower;
        if (reading.available && span > 0 && std::isfinite(span)) {
            // The sample's value is represented by the range fraction passed as
            // a separate prepared field; formatting is never parsed back to a number.
            const double fraction = std::clamp(reading.fraction, 0.0, 1.0);
            const double angle = (1.0 - fraction) * 3.14159265358979323846;
            const QPointF center = arc.center();
            const qreal radius = arc.width() * 0.45;
            const QPointF tip(center.x() + std::cos(angle) * radius,
                              center.y() - std::sin(angle) * radius);
            QColor needle = descriptor.kind == SensorKind::Voltage ? appearance.voltageNeedle : appearance.needle;
            if (preferences.foreground.isValid()) needle = textColor;
            painter.setPen(QPen(needle, std::max(1.0, size * 0.018), Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center, tip);
            if (style.size >= 96) {
                painter.setPen(textColor);
                fittedText(painter, QRectF(size * 0.05, size * 0.78, size * 0.34, size * 0.07),
                           reading.lowerText, style.detailFont, false);
                fittedText(painter, QRectF(size * 0.61, size * 0.78, size * 0.34, size * 0.07),
                           reading.upperText, style.detailFont, false);
            }
        }
    }
    painter.setPen(textColor);
    fittedText(painter, QRectF(padding, size * (compact ? .83 : .87), size - 2 * padding, size * .10),
               descriptor.label, style.labelFont, true, preferences.dashboardShadow);
    painter.restore();
}

} // namespace qsensors
