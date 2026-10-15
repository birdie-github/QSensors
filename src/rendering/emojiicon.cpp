// SPDX-License-Identifier: GPL-3.0-only
#include "rendering/emojiicon.h"

#include <QFont>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <algorithm>

namespace qsensors {
namespace {
class EmojiIconEngine final : public QIconEngine {
public:
    explicit EmojiIconEngine(const QString &emoji) : m_emoji(emoji) {}
    QIconEngine *clone() const override { return new EmojiIconEngine(m_emoji); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        QFont font = QGuiApplication::font();
        font.setPixelSize(std::max(1, int(std::min(rect.width(), rect.height()) * .8)));
        painter->setFont(font);
        painter->setPen(QGuiApplication::palette().color(
            mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText));
        if (mode == QIcon::Disabled) painter->setOpacity(painter->opacity() * .5);
        painter->drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, m_emoji);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QString m_emoji;
};
}

QIcon emojiIcon(const QString &emoji)
{
    return QIcon(new EmojiIconEngine(emoji));
}
}
