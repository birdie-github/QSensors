// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/digitaltext.h"
#include <QHash>
#include <QPainter>
#include <QLineF>
#include <QPainterPath>
#include <array>
#include <algorithm>

namespace qsensors {
namespace {
const QHash<QChar, QPainterPath> &glyphs()
{
    // Normalized segment geometry, cached once for every tile size and sensor.
    static const auto paths = [] {
        const std::array<QLineF, 13> segments = {{
            {0,0,1,0}, {1,0,1,1}, {1,1,1,2}, {0,2,1,2},
            {0,1,0,2}, {0,0,0,1}, {0,1,1,1}, {.5,0,.5,1}, {.5,1,.5,2},
            {0,0,.5,1}, {1,0,.5,1}, {.5,1,0,2}, {.5,1,1,2}
        }};
        QHash<QChar, QPainterPath> result;
        const auto add = [&](char ch, std::initializer_list<int> indices) {
            QPainterPath path;
            for (int index : indices) { path.moveTo(segments[index].p1()); path.lineTo(segments[index].p2()); }
            result.insert(QLatin1Char(ch), path);
        };
        add('0',{0,1,2,3,4,5}); add('1',{1,2}); add('2',{0,1,3,4,6});
        add('3',{0,1,2,3,6}); add('4',{1,2,5,6}); add('5',{0,2,3,5,6});
        add('6',{0,2,3,4,5,6}); add('7',{0,1,2}); add('8',{0,1,2,3,4,5,6});
        add('9',{0,1,2,3,5,6});
        add('A',{0,1,2,4,5,6}); add('B',{0,1,2,3,6,7,8}); add('C',{0,3,4,5});
        add('D',{0,1,2,3,7,8}); add('E',{0,3,4,5,6}); add('F',{0,4,5,6});
        add('G',{0,2,3,4,5,6}); add('H',{1,2,4,5,6}); add('I',{0,3,7,8});
        add('J',{1,2,3,4}); add('K',{4,5,10,12}); add('L',{3,4,5});
        add('M',{1,2,4,5,9,10}); add('N',{1,2,4,5,9,12}); add('O',{0,1,2,3,4,5});
        add('P',{0,1,4,5,6}); add('Q',{0,1,2,3,4,5,12}); add('R',{0,1,4,5,6,12});
        add('S',{0,2,3,5,6}); add('T',{0,7,8}); add('U',{1,2,3,4,5});
        add('W',{1,2,4,5,11,12}); add('X',{9,10,11,12}); add('Y',{9,10,8});
        add('Z',{0,3,10,11}); add('-', {6}); add('+',{6,7,8});
        QPainterPath v;
        v.moveTo(0,0); v.lineTo(.5,2); v.lineTo(1,0);
        result.insert(QLatin1Char('V'), v);
        result.insert(QChar(0x2014), result.value(QLatin1Char('-')));
        QPainterPath colon;
        colon.addEllipse(QPointF(.15,.55), .07,.07);
        colon.addEllipse(QPointF(.15,1.45), .07,.07);
        result.insert(QLatin1Char(':'), colon);
        QPainterPath point;
        point.addEllipse(QPointF(.1,1.94), .06,.06);
        for (QChar ch : {QChar('.'), QChar(','), QChar(0x066b)}) result.insert(ch, point);
        QPainterPath degree;
        degree.addRect(QRectF(0,0,.42,.52)); // KSensors' small square degree marker.
        result.insert(QChar(0x00b0), degree);
        QPainterPath percent;
        percent.addEllipse(QRectF(0,0,.35,.45));
        percent.moveTo(0,2); percent.lineTo(1,0);
        percent.addEllipse(QRectF(.65,1.55,.35,.45));
        result.insert(QLatin1Char('%'), percent);
        return result;
    }();
    return paths;
}

const QHash<QChar, QPainterPath> &watchDigits()
{
    // Filled, chamfered seven-segment digits, matching the old CPU-frequency
    // panel's QLCDNumber::Filled appearance without child widgets or bitmaps.
    static const auto paths = [] {
        std::array<QPainterPath, 7> segments;
        const auto horizontal = [](qreal y) {
            QPainterPath path;
            path.moveTo(.13,y-.06); path.lineTo(.87,y-.06); path.lineTo(.93,y);
            path.lineTo(.87,y+.06); path.lineTo(.13,y+.06); path.lineTo(.07,y);
            path.closeSubpath();
            return path;
        };
        const auto vertical = [](qreal x, qreal y) {
            QPainterPath path;
            path.moveTo(x-.06,y+.13); path.lineTo(x-.06,y+.87); path.lineTo(x,y+.93);
            path.lineTo(x+.06,y+.87); path.lineTo(x+.06,y+.13); path.lineTo(x,y+.07);
            path.closeSubpath();
            return path;
        };
        segments = {{horizontal(0), vertical(1,0), vertical(1,1), horizontal(2),
                     vertical(0,1), vertical(0,0), horizontal(1)}};
        constexpr std::array<unsigned, 10> masks = {{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f}};
        QHash<QChar, QPainterPath> result;
        for (int digit = 0; digit < 10; ++digit) {
            QPainterPath path;
            for (int segment = 0; segment < 7; ++segment)
                if (masks[digit] & (1u << segment)) path.addPath(segments[segment]);
            result.insert(QChar('0' + digit), path);
        }
        result.insert(QLatin1Char('-'), segments[6]);
        result.insert(QChar(0x2014), segments[6]);
        return result;
    }();
    return paths;
}

qreal characterWidth(QChar ch)
{
    if (ch == QLatin1Char(' ')) return .4;
    if (ch == QLatin1Char('.') || ch == QLatin1Char(',') || ch == QChar(0x066b)) return .2;
    if (ch == QLatin1Char(':')) return .3;
    if (ch == QChar(0x00b0)) return .45;
    return 1;
}
}

void drawDigitalText(QPainter &painter, const QRectF &rect, const QString &text,
                     const QColor &color, bool shadow, bool filledSegments)
{
    if (text.isEmpty() || rect.width() <= 0 || rect.height() <= 0) return;
    constexpr qreal gap = .25;
    qreal width = -gap;
    for (QChar ch : text) width += characterWidth(ch) + gap;
    // Reserve device pixels for the minimum stroke and shadow, then leave
    // normalized padding for segment caps. Even long strings fit tiny tiles.
    const qreal edge = shadow ? 1.2 : .5;
    const QRectF area = rect.adjusted(edge, edge, -edge, -edge);
    if (area.width() <= 0 || area.height() <= 0) return;
    const qreal scale = std::min(area.width() / (width + .3), area.height() / 2.3);
    const auto draw = [&](const QColor &ink, qreal offset) {
        painter.save();
        painter.translate(area.center().x() - width * scale / 2 + offset,
                          area.center().y() - scale + offset);
        painter.scale(scale, scale);
        const auto &paths = glyphs();
        const auto &filled = watchDigits();
        for (QChar ch : text) {
            const QChar upper = ch.toUpper();
            const bool solid = filledSegments && filled.contains(upper);
            if (solid) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink);
                painter.drawPath(filled.value(upper));
            } else {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(ink, std::max(.09, .7 / scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                const auto it = paths.constFind(upper);
                if (it != paths.cend()) painter.drawPath(it.value());
            }
            painter.translate(characterWidth(ch) + gap, 0);
        }
        painter.restore();
    };
    if (shadow) draw(color.darker(200), std::max(.6, scale * .07));
    draw(color, 0);
}
}
