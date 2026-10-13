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
                QFont font, bool elide)
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
    painter.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, visible);
}

} // namespace

TileStyle SensorTileRenderer::style(int size, const QFont &baseFont)
{
    TileStyle result;
    result.size = size;
    const auto font = [&baseFont, size](double fraction, int minimum, bool bold) {
        QFont font = baseFont;
        font.setPixelSize(std::max(minimum, static_cast<int>(std::round(size * fraction))));
        font.setBold(bold);
        return font;
    };
    result.valueFont = font(0.23, 10, true);
    result.unitFont = font(0.105, 7, false);
    result.labelFont = font(0.09, 7, false);
    result.detailFont = font(0.06, 6, false);
    return result;
}

TileReading SensorTileRenderer::prepare(const SensorDescriptor &descriptor, const SensorSample &sample,
                                       const SensorPreferences &preferences, TemperatureUnit unit)
{
    TileReading result;
    result.unit = descriptor.unit;
    if (descriptor.kind == SensorKind::Frequency) result.range = {0, 0};
    result.available = sample.available && std::isfinite(sample.value) && sample.timestamp.isValid();
    if (!result.available) {
        result.text = QStringLiteral("—");
        return result;
    }
    if (descriptor.id == QStringLiteral("system/uptime")) {
        const auto uptime = formatUptime(sample.value);
        result.available = sample.value >= 0;
        result.text = uptime.time;
        result.days = uptime.days;
        result.unit.clear();
        result.valueColor = preferences.foreground.isValid() ? preferences.foreground : QColor(Qt::red);
        return result;
    }
    const QLocale locale;
    const auto formatted = formatSensorValue(descriptor, sample.value);
    result.unit = formatted.unit;
    switch (descriptor.kind) {
    case SensorKind::Temperature:
        result.text = locale.toString(sample.value, 'f', unit == TemperatureUnit::Kelvin ? 2 : 1);
        break;
    case SensorKind::Humidity: result.text = locale.toString(sample.value, 'f', 1); break;
    case SensorKind::Percentage: result.text = formatted.number; break;
    case SensorKind::Frequency:
    case SensorKind::Memory: result.text = formatted.number; break;
    case SensorKind::Fan: result.text = locale.toString(sample.value, 'f', 0); break;
    default: result.text = locale.toString(sample.value, 'g', 6); break;
    }
    // Keep essential numbers readable at the smallest tile size.
    if (result.text.size() > 9)
        result.text = locale.toString(sample.value, 'g', 4);
    const bool temperature = descriptor.kind == SensorKind::Temperature;
    const double canonicalValue = temperature ? temperatureFromDisplay(sample.value, unit) : sample.value;
    result.range = preferences.customRange ? GaugeRange{preferences.rangeLower, preferences.rangeUpper}
                                          : gaugeRange(descriptor.kind, canonicalValue);
    if (!preferences.customRange && descriptor.kind == SensorKind::Frequency)
        result.range = std::isfinite(sample.maximum) && sample.maximum > 0
            ? GaugeRange{0, sample.maximum} : GaugeRange{0, 0};
    // Memory's canonical unit stays MiB even when its display descriptor is %.
    if (descriptor.id == QStringLiteral("system/memory/available") && preferences.memoryAsPercent && std::isfinite(sample.reference) && sample.reference > 0 && preferences.customRange) {
        result.range.lower = result.range.lower / sample.reference * 100;
        result.range.upper = result.range.upper / sample.reference * 100;
    }
    if (temperature) {
        result.range.lower = temperatureToDisplay(result.range.lower, unit);
        result.range.upper = temperatureToDisplay(result.range.upper, unit);
    }
    result.lowerText = locale.toString(result.range.lower, 'g', 6);
    result.upperText = locale.toString(result.range.upper, 'g', 6);
    if (descriptor.kind == SensorKind::Frequency || descriptor.kind == SensorKind::Memory) {
        const auto lower = formatSensorValue(descriptor, result.range.lower);
        const auto upper = formatSensorValue(descriptor, result.range.upper);
        result.lowerText = lower.number + lower.unit;
        result.upperText = upper.number + upper.unit;
    }
    const double span = result.range.upper - result.range.lower;
    if (span > 0 && std::isfinite(span))
        result.fraction = std::clamp((sample.value - result.range.lower) / span, 0.0, 1.0);
    return result;
}

void SensorTileRenderer::draw(QPainter &painter, const QRectF &rect,
                             const SensorDescriptor &descriptor, const TileReading &reading,
                             const TileStyle &style, const QPalette &palette,
                             bool gauge, bool selected)
{
    gauge = gauge && descriptor.kind != SensorKind::Duration;
    if (descriptor.kind == SensorKind::Frequency)
        gauge = gauge && std::isfinite(reading.range.upper) && reading.range.upper > reading.range.lower;
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.translate(rect.topLeft());
    const qreal size = style.size;
    const qreal padding = std::max(3.0, size * 0.05);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(palette.brush(QPalette::Base));
    painter.setPen(QPen(palette.color(selected ? QPalette::Highlight : QPalette::Mid),
                        std::max(1.0, size * (selected ? 0.014 : 0.006))));
    painter.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size * 0.06, size * 0.06);
    const QColor textColor = palette.color(reading.available ? QPalette::Active : QPalette::Disabled,
                                          QPalette::Text);
    painter.setPen(textColor);
    if (descriptor.id == QStringLiteral("system/uptime")) {
        // Small legends default to black; invert on dark tile backgrounds for readability.
        const QColor legend = palette.color(QPalette::Base).lightness() >= 128 ? QColor(Qt::black) : QColor(Qt::white);
        const QColor base = reading.available ? (reading.valueColor.isValid() ? reading.valueColor : QColor(Qt::red))
                                             : palette.color(QPalette::Disabled, QPalette::Text);
        if (reading.days.isEmpty()) {
            drawDigitalText(painter, QRectF(padding, size * .08, size - 2 * padding, size * .13), QStringLiteral("HH:MM"), legend);
            drawDigitalText(painter, QRectF(padding, size * .29, size - 2 * padding, size * .40), reading.text, base, true);
        } else {
            const QRectF daysRect(padding, size * .15, size - 2 * padding, size * .23);
            const qreal offset = std::max(.6, size * .007);
            painter.setPen(base.darker(200));
            fittedText(painter, daysRect.translated(offset, offset), reading.days, style.valueFont, false);
            painter.setPen(base);
            fittedText(painter, daysRect, reading.days, style.valueFont, false);
            drawDigitalText(painter, QRectF(padding, size * .44, size - 2 * padding, size * .29), reading.text, base, true);
        }
        drawDigitalText(painter, QRectF(padding, size * .83, size - 2 * padding, size * .10), QStringLiteral("UPTIME"), legend);
        painter.restore();
        return;
    }
    const bool compact = style.size < 96;
    fittedText(painter, QRectF(padding, size * (gauge ? 0.07 : 0.20), size - 2 * padding, size * 0.32),
               reading.text, style.valueFont, false);
    fittedText(painter, QRectF(padding, size * (gauge ? 0.36 : 0.52), size - 2 * padding, std::max(9.0, size * 0.13)),
               reading.unit, style.unitFont, true);

    if (gauge) {
        const QRectF arc = compact ? QRectF(size * 0.28, size * 0.53, size * 0.44, size * 0.44)
                                   : QRectF(size * 0.24, size * 0.50, size * 0.52, size * 0.52);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette.color(QPalette::Mid), std::max(1.0, size * 0.02), Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, 0, 180 * 16);
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
            painter.setPen(QPen(palette.color(QPalette::Highlight), std::max(1.0, size * 0.018), Qt::SolidLine, Qt::RoundCap));
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
    fittedText(painter, QRectF(padding, size * (compact ? 0.81 : 0.85), size - 2 * padding, std::max(8.0, size * 0.09)),
               descriptor.label, style.labelFont, true);
    if (style.size >= 192)
        fittedText(painter, QRectF(padding, size * 0.94, size - 2 * padding, size * 0.05),
                   descriptor.device, style.detailFont, true);
    painter.restore();
}

} // namespace qsensors
