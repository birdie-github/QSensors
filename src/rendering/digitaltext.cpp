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
    // Independent normalized segment geometry, cached once for all tile sizes.
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
        add('9',{0,1,2,3,5,6}); add('H',{1,2,4,5,6}); add('M',{1,2,4,5,9,10});
        add('U',{1,2,3,4,5}); add('P',{0,1,4,5,6}); add('T',{0,7,8});
        add('I',{0,3,7,8}); add('E',{0,3,4,5,6}); add('-', {6});
        result.insert(QChar(0x2014), result.value(QLatin1Char('-')));
        QPainterPath colon;
        colon.addEllipse(QPointF(.5,.55), .07,.07);
        colon.addEllipse(QPointF(.5,1.45), .07,.07);
        result.insert(QLatin1Char(':'), colon);
        return result;
    }();
    return paths;
}
}
void drawDigitalText(QPainter &painter, const QRectF &rect, const QString &text,
                     const QColor &color, bool shadow)
{
    if (text.isEmpty() || rect.width() <= 0 || rect.height() <= 0) return;
    const qreal width = text.size() * 1.4 - .4;
    const qreal scale = std::min(rect.width() / (width + .3), rect.height() / 2.3);
    const auto draw = [&](const QColor &ink, qreal offset) {
        painter.save();
        painter.translate(rect.center().x() - width * scale / 2 + offset,
                          rect.center().y() - scale + offset);
        painter.scale(scale, scale);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ink, std::max(.09, .7 / scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const auto &paths = glyphs();
        for (QChar ch : text) {
            const auto it = paths.constFind(ch.toUpper());
            if (it != paths.cend()) painter.drawPath(it.value());
            painter.translate(1.4, 0);
        }
        painter.restore();
    };
    if (shadow) draw(color.darker(200), std::max(.6, scale * .07));
    draw(color, 0);
}
}
