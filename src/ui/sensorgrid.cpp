// SPDX-License-Identifier: GPL-3.0-only
#include "ui/sensorgrid.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QRegion>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {
namespace {
constexpr auto tileMime = "application/x-qsensors-tile-id";
}

SensorGrid::SensorGrid(QWidget *parent) : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(true);
    viewport()->setBackgroundRole(QPalette::Window);
    viewport()->setAcceptDrops(true);
    setAcceptDrops(true);
    QSettings settings;
    bool valid = false;
    const int size = settings.value(QStringLiteral("Dashboard/tileSize"), 128).toInt(&valid);
    m_style = SensorTileRenderer::style(valid && size >= 48 && size <= 2048 ? size : 128, font());
    m_gauges = settings.value(QStringLiteral("Dashboard/gauges"), true).toBool();
    m_savedOrder = settings.value(QStringLiteral("Dashboard/order")).toStringList();
    m_savedOrder.removeAll(QString());
    m_savedOrder.removeDuplicates();
    m_autoScroll.setInterval(75);
    connect(&m_autoScroll, &QTimer::timeout, this, [this] {
        const QPoint previous(horizontalScrollBar()->value(), verticalScrollBar()->value());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + m_autoScrollDirection.x() * 24);
        verticalScrollBar()->setValue(verticalScrollBar()->value() + m_autoScrollDirection.y() * 24);
        if (previous == QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()))
            m_autoScroll.stop();
        else
            updateDropTarget(m_dragPosition);
    });
}

void SensorGrid::setTileSize(int size)
{
    size = std::clamp(size, 48, 2048);
    if (size == tileSize())
        return;
    const QString anchor = visibleIds().value(0);
    m_style = SensorTileRenderer::style(size, font());
    QSettings().setValue(QStringLiteral("Dashboard/tileSize"), size);
    updateLayout();
    const int index = m_order.indexOf(anchor);
    if (index >= 0)
        ensureVisible(index);
}

void SensorGrid::setGaugesVisible(bool visible)
{
    if (m_gauges == visible)
        return;
    m_gauges = visible;
    QSettings().setValue(QStringLiteral("Dashboard/gauges"), visible);
    viewport()->update();
}

void SensorGrid::setDescriptors(const QList<SensorDescriptor> &descriptors)
{
    QHash<QString, Tile> tiles;
    tiles.reserve(descriptors.size());
    bool newIds = false;
    for (const auto &descriptor : descriptors) {
        Tile tile = m_tiles.value(descriptor.id);
        tile.descriptor = descriptor;
        tile.reading = SensorTileRenderer::prepare(descriptor, tile.sample);
        tiles.insert(descriptor.id, tile);
        if (!m_savedOrder.contains(descriptor.id)) {
            m_savedOrder.append(descriptor.id);
            newIds = true;
        }
    }
    m_tiles = tiles;
    if (newIds)
        QSettings().setValue(QStringLiteral("Dashboard/order"), m_savedOrder);
    rebuildOrder();
}

void SensorGrid::rebuildOrder()
{
    m_order.clear();
    for (const auto &id : m_savedOrder) {
        if (m_tiles.contains(id))
            m_order.append(id);
    }
    if (!m_tiles.contains(m_selected))
        m_selected = m_order.value(0);
    m_dropTarget.clear();
    updateLayout();
}

void SensorGrid::applySamples(const QList<SensorSample> &samples)
{
    QHash<QString, SensorSample> batch;
    batch.reserve(samples.size());
    for (const auto &sample : samples)
        batch.insert(sample.id, sample);
    QRegion changed;
    for (int i = 0; i < m_order.size(); ++i) {
        auto &tile = m_tiles[m_order[i]];
        auto sample = batch.value(m_order[i]);
        sample.id = m_order[i];
        const auto reading = SensorTileRenderer::prepare(tile.descriptor, sample);
        const bool different = reading.available != tile.reading.available
            || reading.text != tile.reading.text
            || (m_gauges && (reading.fraction != tile.reading.fraction
                || reading.range.lower != tile.reading.range.lower || reading.range.upper != tile.reading.range.upper));
        tile.sample = sample;
        tile.reading = reading;
        if (different && isVisible() && !window()->isMinimized()) {
            const QRect rect = tileRect(i).intersected(viewport()->rect());
            if (!rect.isEmpty())
                changed += rect;
        }
    }
    if (!changed.isEmpty())
        viewport()->update(changed);
}

QRect SensorGrid::tileRect(int index) const
{
    const int stride = tileSize() + m_gap;
    return QRect(m_gap + (index % m_columns) * stride - horizontalScrollBar()->value(),
                 m_gap + (index / m_columns) * stride - verticalScrollBar()->value(),
                 tileSize(), tileSize());
}

QStringList SensorGrid::visibleIds() const
{
    QStringList ids;
    if (!isVisible() || window()->isMinimized() || viewport()->width() <= 0 || viewport()->height() <= 0)
        return ids;
    const int stride = tileSize() + m_gap;
    const int firstRow = std::max(0, (verticalScrollBar()->value() - m_gap) / stride);
    const int lastRow = (verticalScrollBar()->value() + viewport()->height()) / stride;
    const int first = firstRow * m_columns;
    const int last = std::min(static_cast<int>(m_order.size()), (lastRow + 1) * m_columns);
    for (int i = first; i < last; ++i) {
        if (tileRect(i).intersects(viewport()->rect()))
            ids.append(m_order[i]);
    }
    return ids;
}

void SensorGrid::notifyVisibleIds()
{
    const auto ids = visibleIds();
    if (ids != m_lastVisible) {
        m_lastVisible = ids;
        emit visibleIdsChanged(ids);
    }
}

void SensorGrid::updateLayout()
{
    if (m_updatingLayout)
        return;
    m_updatingLayout = true;
    const QSize previousViewport = viewport()->size();
    m_columns = std::max(1, (viewport()->width() - m_gap) / (tileSize() + m_gap));
    const qint64 rows = (m_order.size() + m_columns - 1) / m_columns;
    const qint64 height = m_gap + rows * (tileSize() + m_gap);
    const qint64 width = m_order.isEmpty() ? 0 : m_gap + m_columns * (tileSize() + m_gap);
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setSingleStep(std::max(16, tileSize() / 4));
    verticalScrollBar()->setSingleStep(std::max(16, tileSize() / 4));
    horizontalScrollBar()->setRange(0, static_cast<int>(std::clamp<qint64>(width - viewport()->width(), 0, std::numeric_limits<int>::max())));
    verticalScrollBar()->setRange(0, static_cast<int>(std::clamp<qint64>(height - viewport()->height(), 0, std::numeric_limits<int>::max())));
    m_updatingLayout = false;
    if (viewport()->size() != previousViewport)
        QTimer::singleShot(0, this, [this] { updateLayout(); });
    viewport()->update();
    notifyVisibleIds();
}

void SensorGrid::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setClipRegion(event->region());
    const int stride = tileSize() + m_gap;
    const int firstRow = std::max(0, (verticalScrollBar()->value() - m_gap) / stride);
    const int lastRow = (verticalScrollBar()->value() + viewport()->height()) / stride;
    const int last = std::min(static_cast<int>(m_order.size()), (lastRow + 1) * m_columns);
    for (int index = firstRow * m_columns; index < last; ++index) {
        const QString &id = m_order[index];
        const QRect rect = tileRect(index);
        if (!event->region().intersects(rect))
            continue;
        const auto &tile = m_tiles.constFind(id).value();
        SensorTileRenderer::draw(painter, rect, tile.descriptor, tile.reading, m_style,
                                 palette(), m_gauges, id == m_selected);
        if (id == m_dropTarget) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
            const int x = m_dropAfter ? rect.right() : rect.left();
            painter.drawLine(x, rect.top(), x, rect.bottom());
        }
    }
}

void SensorGrid::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateLayout();
}

void SensorGrid::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        m_style = SensorTileRenderer::style(tileSize(), font());
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
        viewport()->update();
}

void SensorGrid::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    updateLayout();
}

void SensorGrid::hideEvent(QHideEvent *event)
{
    m_autoScroll.stop();
    QAbstractScrollArea::hideEvent(event);
    notifyVisibleIds();
}

void SensorGrid::scrollContentsBy(int, int)
{
    viewport()->update();
    notifyVisibleIds();
}

bool SensorGrid::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Resize)
        updateLayout();
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int index = indexAt(help->pos());
        if (index >= 0) {
            const auto &tile = m_tiles[m_order[index]];
            const bool valid = tile.reading.available;
            const QString value = valid ? QStringLiteral("%1 %2").arg(QLocale().toString(tile.sample.value, 'g', 12), tile.descriptor.unit)
                                        : tr("Unavailable");
            QToolTip::showText(help->globalPos(), tr("%1\n%2\n%3\n%4")
                .arg(tile.descriptor.label, tile.descriptor.device, value,
                     valid ? tile.descriptor.id : tile.sample.error.isEmpty() ? tr("No current reading") : tile.sample.error), viewport());
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

int SensorGrid::indexAt(const QPoint &position, bool nearest) const
{
    if (m_order.isEmpty())
        return -1;
    const int stride = tileSize() + m_gap;
    const int x = position.x() + horizontalScrollBar()->value() - m_gap;
    const int y = position.y() + verticalScrollBar()->value() - m_gap;
    if (!nearest && (x < 0 || y < 0))
        return -1;
    const int column = std::clamp(x / stride, 0, m_columns - 1);
    const int row = std::max(0, y / stride);
    const int index = std::min(row * m_columns + column, static_cast<int>(m_order.size()) - 1);
    return nearest || tileRect(index).contains(position) ? index : -1;
}

void SensorGrid::select(const QString &id)
{
    const int old = m_order.indexOf(m_selected);
    m_selected = id;
    if (old >= 0)
        viewport()->update(tileRect(old).intersected(viewport()->rect()));
    const int current = m_order.indexOf(id);
    if (current >= 0)
        viewport()->update(tileRect(current).intersected(viewport()->rect()));
}

void SensorGrid::ensureVisible(int index)
{
    const int stride = tileSize() + m_gap;
    const int x = m_gap + (index % m_columns) * stride;
    const int y = m_gap + (index / m_columns) * stride;
    if (tileSize() > viewport()->height() || y < verticalScrollBar()->value())
        verticalScrollBar()->setValue(y);
    else if (y + tileSize() > verticalScrollBar()->value() + viewport()->height())
        verticalScrollBar()->setValue(y + tileSize() - viewport()->height());
    if (tileSize() > viewport()->width() || x < horizontalScrollBar()->value())
        horizontalScrollBar()->setValue(x);
    else if (x + tileSize() > horizontalScrollBar()->value() + viewport()->width())
        horizontalScrollBar()->setValue(x + tileSize() - viewport()->width());
}

void SensorGrid::mousePressEvent(QMouseEvent *event)
{
    m_pressed.clear();
    if (event->button() == Qt::LeftButton) {
        const int index = indexAt(event->position().toPoint());
        if (index >= 0) {
            m_pressed = m_order[index];
            m_pressPosition = event->position().toPoint();
            select(m_pressed);
            setFocus();
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void SensorGrid::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_pressed.isEmpty()
        && (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
        const QString source = m_pressed;
        m_pressed.clear();
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setData(tileMime, source.toUtf8());
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
        m_autoScroll.stop();
        m_dropTarget.clear();
        viewport()->update();
        delete drag;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void SensorGrid::keyPressEvent(QKeyEvent *event)
{
    const int current = m_order.indexOf(m_selected);
    if (current < 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    int target = current;
    switch (event->key()) {
    case Qt::Key_Left: --target; break;
    case Qt::Key_Right: ++target; break;
    case Qt::Key_Up: target -= m_columns; break;
    case Qt::Key_Down: target += m_columns; break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End: target = m_order.size() - 1; break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
    target = std::clamp(target, 0, static_cast<int>(m_order.size()) - 1);
    if ((event->modifiers() & Qt::ControlModifier) && target != current)
        moveTile(m_selected, m_order[target], target > current);
    else
        select(m_order[target]);
    ensureVisible(m_order.indexOf(m_selected));
    event->accept();
}

bool SensorGrid::acceptDrag(const QMimeData *data, QObject *source) const
{
    return source == this && data->hasFormat(tileMime)
        && m_tiles.contains(QString::fromUtf8(data->data(tileMime)));
}

void SensorGrid::dragEnterEvent(QDragEnterEvent *event)
{
    if (acceptDrag(event->mimeData(), event->source()))
        event->acceptProposedAction();
}

void SensorGrid::updateDropTarget(const QPoint &position)
{
    const int index = indexAt(position, true);
    m_dropTarget = index >= 0 ? m_order[index] : QString();
    m_dropAfter = index >= 0 && (position.y() > tileRect(index).bottom()
        || position.x() > tileRect(index).center().x());
    viewport()->update();
}

void SensorGrid::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptDrag(event->mimeData(), event->source()))
        return;
    event->acceptProposedAction();
    m_dragPosition = event->position().toPoint();
    updateDropTarget(m_dragPosition);
    constexpr int edge = 24;
    m_autoScrollDirection = QPoint(m_dragPosition.x() < edge ? -1 : m_dragPosition.x() > viewport()->width() - edge ? 1 : 0,
                                  m_dragPosition.y() < edge ? -1 : m_dragPosition.y() > viewport()->height() - edge ? 1 : 0);
    if (m_autoScrollDirection.isNull())
        m_autoScroll.stop();
    else if (!m_autoScroll.isActive())
        m_autoScroll.start();
}

void SensorGrid::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_autoScroll.stop();
    m_dropTarget.clear();
    viewport()->update();
    event->accept();
}

void SensorGrid::dropEvent(QDropEvent *event)
{
    m_autoScroll.stop();
    if (!acceptDrag(event->mimeData(), event->source()))
        return;
    updateDropTarget(event->position().toPoint());
    moveTile(QString::fromUtf8(event->mimeData()->data(tileMime)), m_dropTarget, m_dropAfter);
    m_dropTarget.clear();
    viewport()->update();
    event->acceptProposedAction();
}

void SensorGrid::moveTile(const QString &source, const QString &target, bool after)
{
    if (source == target || !m_tiles.contains(source) || !m_tiles.contains(target))
        return;
    m_savedOrder.removeAll(source);
    const int index = m_savedOrder.indexOf(target);
    m_savedOrder.insert(index + (after ? 1 : 0), source);
    QSettings().setValue(QStringLiteral("Dashboard/order"), m_savedOrder);
    m_selected = source;
    rebuildOrder();
}

} // namespace qsensors
