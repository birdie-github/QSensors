// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QColor>
#include <QRectF>
#include <QString>
class QPainter;
namespace qsensors {
// Optional foreground bounds use painter-transformed coordinates, excluding shadow.
void drawDigitalText(QPainter &painter, const QRectF &rect, const QString &text,
                     const QColor &color, bool shadow = false, bool filledSegments = false,
                     QRectF *inkBounds = nullptr);
}
