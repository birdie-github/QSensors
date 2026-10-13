// SPDX-License-Identifier: GPL-3.0-only
#include "ui/sensorgrid.h"
#include "rendering/valueformat.h"
#include "settings/sensorpreferences.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {
namespace {
// Keep cell-to-pixel conversions safe even at the largest supported tile size.
constexpr int coordinateLimit = (std::numeric_limits<int>::max() / 2056 - 4) / 2;
}
SensorGrid::SensorGrid(SensorPreferencesStore *preferences, QWidget *parent)
    : QAbstractScrollArea(parent), m_preferences(preferences)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(true);
    viewport()->setBackgroundRole(QPalette::Window);
    QSettings settings;
    bool valid = false;
    const int size = settings.value(QStringLiteral("Dashboard/tileSize"), 128).toInt(&valid);
    m_style = SensorTileRenderer::style(valid && size >= 48 && size <= 2048 ? size : 128, font());
    m_gauges = settings.value(QStringLiteral("Dashboard/gauges"), true).toBool();
    const int left = settings.value(QStringLiteral("Dashboard/gridLeft"), 0).toInt();
    const int top = settings.value(QStringLiteral("Dashboard/gridTop"), 0).toInt();
    const int columns = settings.value(QStringLiteral("Dashboard/gridColumns"), 3).toInt();
    const int rows = settings.value(QStringLiteral("Dashboard/gridRows"), 1).toInt();
    if (left >= -coordinateLimit && top >= -coordinateLimit && columns > 0 && rows > 0
        && qint64(left) + columns <= coordinateLimit && qint64(top) + rows <= coordinateLimit)
        m_bounds = QRect(left, top, columns, rows);
    for (const auto &group : settings.childGroups()) {
        if (!group.startsWith(QStringLiteral("Tile-"))) continue;
        settings.beginGroup(group);
        const QString id = sensorInternalId(settings.value(QStringLiteral("id")).toString());
        bool xOk = false, yOk = false;
        const int x = settings.value(QStringLiteral("column")).toInt(&xOk);
        const int y = settings.value(QStringLiteral("row")).toInt(&yOk);
        const QPoint cell(x, y);
        if (!id.isEmpty() && xOk && yOk && x >= -coordinateLimit && x < coordinateLimit
            && y >= -coordinateLimit && y < coordinateLimit && !m_positions.values().contains(cell)) {
            m_positions.insert(id, cell);
            m_bounds = m_bounds.united(QRect(cell, QSize(1, 1)));
        }
        settings.endGroup();
    }
    m_autoScroll.setInterval(75);
    connect(&m_autoScroll, &QTimer::timeout, this, [this] {
        if (!m_dragging) { m_autoScroll.stop(); return; }
        const QPoint oldScroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const QRect oldBounds = m_bounds;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + m_autoScrollDirection.x() * 24);
        verticalScrollBar()->setValue(verticalScrollBar()->value() + m_autoScrollDirection.y() * 24);
        updateDrag();
        if (oldScroll == QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()) && oldBounds == m_bounds) m_autoScroll.stop();
    });
}
void SensorGrid::setTileSize(int size)
{
    size = std::clamp(size, 48, 2048);
    if (size == tileSize()) return;
    finishDrag(false);
    m_style = SensorTileRenderer::style(size, font());
    QSettings().setValue(QStringLiteral("Dashboard/tileSize"), size);
    updateLayout();
    ensureVisible(m_order.indexOf(m_selected));
}
void SensorGrid::setGaugesVisible(bool visible)
{
    if (m_gauges == visible) return;
    m_gauges = visible;
    QSettings().setValue(QStringLiteral("Dashboard/gauges"), visible);
    viewport()->update();
}
QPoint SensorGrid::firstEmptyCell()
{
    // Occupied positions are sparse; stop after at most occupied-count + 1 cells.
    QSet<QPoint> occupied;
    for (const auto &cell : m_positions) occupied.insert(cell);
    for (int row = m_bounds.top(); row <= m_bounds.bottom(); ++row) {
        for (int column = m_bounds.left(); column <= m_bounds.right(); ++column) {
            const QPoint cell(column, row);
            if (!occupied.contains(cell)) return cell;
        }
    }
    QPoint cell;
    if (m_bounds.bottom() < coordinateLimit - 1) cell = QPoint(m_bounds.left(), m_bounds.bottom() + 1);
    else if (m_bounds.right() < coordinateLimit - 1) cell = QPoint(m_bounds.right() + 1, m_bounds.top());
    else if (m_bounds.top() > -coordinateLimit) cell = QPoint(m_bounds.left(), m_bounds.top() - 1);
    else cell = QPoint(m_bounds.left() - 1, m_bounds.top());
    expandTo(cell);
    return cell;
}
void SensorGrid::setDescriptors(const QList<SensorDescriptor> &descriptors)
{
    finishDrag(false);
    bool changed = false;
    for (auto it = m_positions.begin(); it != m_positions.end();) {
        if (!m_preferences->value(it.key()).dashboard) { it = m_positions.erase(it); changed = true; }
        else ++it;
    }
    QHash<QString, Tile> tiles;
    m_order.clear();
    for (const auto &descriptor : descriptors) {
        Tile tile = m_tiles.value(descriptor.id);
        tile.descriptor = descriptor;
        tile.reading = SensorTileRenderer::prepare(descriptor, tile.sample, m_preferences->value(descriptor.id), m_preferences->temperatureUnit());
        tiles.insert(descriptor.id, tile);
        m_order.append(descriptor.id);
        if (!m_positions.contains(descriptor.id)) { m_positions.insert(descriptor.id, firstEmptyCell()); changed = true; }
    }
    // A selected sensor that disappears still occupies its cell visibly.
    // No value or unit is invented if no prior descriptor is cached.
    QStringList positioned = m_positions.keys();
    std::sort(positioned.begin(), positioned.end());
    for (const auto &id : positioned) {
        if (tiles.contains(id)) continue;
        Tile tile = m_tiles.value(id);
        if (tile.descriptor.id.isEmpty()) {
            tile.descriptor.id = id;
            tile.descriptor.label = m_preferences->value(id).label;
            if (tile.descriptor.label.isEmpty()) tile.descriptor.label = tr("Unavailable sensor");
            tile.descriptor.kind = SensorKind::Duration;
        }
        tile.sample = SensorSample{};
        tile.sample.id = id;
        tile.sample.error = tr("Sensor is not currently detected");
        tile.reading = SensorTileRenderer::prepare(tile.descriptor, tile.sample, m_preferences->value(id), m_preferences->temperatureUnit());
        tiles.insert(id, tile);
        m_order.append(id);
    }
    m_tiles = tiles;
    if (!m_tiles.contains(m_selected)) m_selected = m_order.value(0);
    if (changed) saveLayout();
    updateLayout();
}
void SensorGrid::saveLayout()
{
    QSettings settings;
    for (const auto &group : settings.childGroups())
        if (group.startsWith(QStringLiteral("Tile-"))) settings.remove(group);
    QStringList ids = m_positions.keys();
    std::sort(ids.begin(), ids.end());
    int index = 0;
    for (const auto &id : ids) {
        settings.beginGroup(QStringLiteral("Tile-%1").arg(++index, 3, 10, QLatin1Char('0')));
        settings.setValue(QStringLiteral("id"), sensorConfigId(id));
        settings.setValue(QStringLiteral("column"), m_positions.value(id).x());
        settings.setValue(QStringLiteral("row"), m_positions.value(id).y());
        settings.endGroup();
    }
    settings.setValue(QStringLiteral("Dashboard/gridLeft"), m_bounds.left());
    settings.setValue(QStringLiteral("Dashboard/gridTop"), m_bounds.top());
    settings.setValue(QStringLiteral("Dashboard/gridColumns"), m_bounds.width());
    settings.setValue(QStringLiteral("Dashboard/gridRows"), m_bounds.height());
    settings.remove(QStringLiteral("Dashboard/sensorOrder"));
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
        const auto reading = SensorTileRenderer::prepare(tile.descriptor, sample, m_preferences->value(tile.descriptor.id), m_preferences->temperatureUnit());
        const bool different = reading.available != tile.reading.available
            || reading.text != tile.reading.text
            || reading.unit != tile.reading.unit
            || reading.days != tile.reading.days
            || reading.valueColor != tile.reading.valueColor
            || (m_gauges && (reading.fraction != tile.reading.fraction
                || reading.range.lower != tile.reading.range.lower || reading.range.upper != tile.reading.range.upper));
        tile.sample = sample;
        tile.reading = reading;
        if (different && isVisible() && !window()->isMinimized()) {
            const QRect rect = (m_dragging && m_order[i] == m_pressed ? cellRect(m_dropCell) : tileRect(i)).intersected(viewport()->rect());
            if (!rect.isEmpty())
                changed += rect;
        }
    }
    if (!changed.isEmpty())
        viewport()->update(changed);
}

QRect SensorGrid::cellRect(const QPoint &cell) const
{
    const int stride = tileSize() + m_gap;
    return QRect(m_gap + (cell.x() - m_bounds.left()) * stride - horizontalScrollBar()->value(),
                 m_gap + (cell.y() - m_bounds.top()) * stride - verticalScrollBar()->value(), tileSize(), tileSize());
}
QRect SensorGrid::tileRect(int index) const { return cellRect(m_positions.value(m_order[index])); }
QPoint SensorGrid::cellAt(const QPoint &position) const
{
    const double stride = tileSize() + m_gap;
    return QPoint(std::clamp(m_bounds.left() + int(std::floor((position.x() + double(horizontalScrollBar()->value()) - m_gap) / stride)), -coordinateLimit, coordinateLimit - 1),
                  std::clamp(m_bounds.top() + int(std::floor((position.y() + double(verticalScrollBar()->value()) - m_gap) / stride)), -coordinateLimit, coordinateLimit - 1));
}
QStringList SensorGrid::visibleIds() const
{
    QStringList ids;
    if (!isVisible() || window()->isMinimized()) return ids;
    for (int i = 0; i < m_order.size(); ++i)
        if (tileRect(i).intersects(viewport()->rect())) ids.append(m_order[i]);
    if (m_dragging && !ids.contains(m_pressed) && cellRect(m_dropCell).intersects(viewport()->rect())) ids.append(m_pressed);
    return ids;
}
void SensorGrid::notifyVisibleIds()
{
    const auto ids = visibleIds();
    if (ids != m_lastVisible) { m_lastVisible = ids; emit visibleIdsChanged(ids); }
}
void SensorGrid::updateLayout()
{
    if (m_updatingLayout) return;
    m_updatingLayout = true;
    const QSize previous = viewport()->size();
    const int stride = tileSize() + m_gap;
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, std::max(0, m_gap + m_bounds.width() * stride - viewport()->width()));
    verticalScrollBar()->setRange(0, std::max(0, m_gap + m_bounds.height() * stride - viewport()->height()));
    horizontalScrollBar()->setSingleStep(std::max(16, tileSize() / 4));
    verticalScrollBar()->setSingleStep(std::max(16, tileSize() / 4));
    m_updatingLayout = false;
    if (viewport()->size() != previous) QTimer::singleShot(0, this, [this] { updateLayout(); });
    viewport()->update();
    notifyVisibleIds();
}
void SensorGrid::expandTo(const QPoint &cell)
{
    const QRect next = m_bounds.united(QRect(cell, QSize(1, 1)));
    if (next == m_bounds) return;
    const QPoint compensation((m_bounds.left() - next.left()) * (tileSize() + m_gap),
                              (m_bounds.top() - next.top()) * (tileSize() + m_gap));
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    m_bounds = next;
    updateLayout();
    horizontalScrollBar()->setValue(scroll.x() + compensation.x());
    verticalScrollBar()->setValue(scroll.y() + compensation.y());
}
void SensorGrid::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setClipRegion(event->region());
    // Empty cells are outlines only; no cell objects or canvas-sized image.
    painter.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
    const QPoint first = cellAt(QPoint(0, 0)), last = cellAt(viewport()->rect().bottomRight());
    for (int y = std::max(first.y(), m_bounds.top()); y <= std::min(last.y(), m_bounds.bottom()); ++y)
        for (int x = std::max(first.x(), m_bounds.left()); x <= std::min(last.x(), m_bounds.right()); ++x) {
            const QRect rect = cellRect(QPoint(x, y));
            if (event->region().intersects(rect)) painter.drawRect(rect.adjusted(1, 1, -1, -1));
        }
    const auto draw = [&](const QString &id, const QRect &rect) {
        const auto &tile = m_tiles.constFind(id).value();
        QPalette colors = palette();
        const auto prefs = m_preferences->value(id);
        if (prefs.foreground.isValid() || prefs.background.isValid()) colors.setColor(QPalette::Text, dashboardTextColor(prefs, colors.color(QPalette::Text)));
        if (prefs.background.isValid()) colors.setColor(QPalette::Base, prefs.background);
        SensorTileRenderer::draw(painter, rect, tile.descriptor, tile.reading, m_style, colors, m_gauges, id == m_selected);
    };
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_dragging && m_order[i] == m_pressed) continue;
        const QRect rect = tileRect(i);
        if (event->region().intersects(rect)) draw(m_order[i], rect);
    }
    if (m_dragging && m_tiles.contains(m_pressed) && event->region().intersects(cellRect(m_dropCell))) {
        painter.save(); painter.setOpacity(.85); draw(m_pressed, cellRect(m_dropCell)); painter.restore();
    }
    if (m_order.isEmpty()) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(viewport()->rect().adjusted(16,16,-16,-16), Qt::AlignCenter | Qt::TextWordWrap,
                         tr("No dashboard sensors selected. Choose sensors in Settings. Double-click empty space to show or hide controls."));
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
    if (event->type() == QEvent::WindowDeactivate) finishDrag(false);
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
    finishDrag(false);
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
            const auto formatted = formatSensorValue(tile.descriptor, tile.sample.value);
            const QString value = valid ? QStringLiteral("%1 %2").arg(formatted.number, formatted.unit)
                                        : tr("Unavailable");
            QToolTip::showText(help->globalPos(), tr("%1\n%2\n%3\n%4")
                .arg(tile.descriptor.label, tile.descriptor.device, value,
                     valid ? (tile.sample.detail.isEmpty() ? tile.descriptor.id : tile.sample.detail)
                           : tile.sample.error.isEmpty() ? tr("No current reading") : tile.sample.error), viewport());
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
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

int SensorGrid::indexAt(const QPoint &position) const
{
    for (int i = 0; i < m_order.size(); ++i) if (tileRect(i).contains(position)) return i;
    return -1;
}
void SensorGrid::ensureVisible(int index)
{
    if (index < 0 || index >= m_order.size()) return;
    const QRect rect = tileRect(index);
    if (rect.left() < 0) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + rect.left());
    else if (rect.right() >= viewport()->width()) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + rect.right() - viewport()->width() + 1);
    if (rect.top() < 0) verticalScrollBar()->setValue(verticalScrollBar()->value() + rect.top());
    else if (rect.bottom() >= viewport()->height()) verticalScrollBar()->setValue(verticalScrollBar()->value() + rect.bottom() - viewport()->height() + 1);
}
void SensorGrid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = indexAt(event->position().toPoint());
        if (index >= 0) {
            m_pressed = m_order[index]; m_pressPosition = event->position().toPoint();
            m_grabOffset = m_pressPosition - tileRect(index).topLeft();
            select(m_pressed); setFocus(); event->accept(); return;
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}
void SensorGrid::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_pressed.isEmpty()) {
        if (!m_dragging && (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            m_beforeDrag = m_bounds;
            m_beforeScroll = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
            m_dragging = true;
            viewport()->setCursor(Qt::SizeAllCursor);
        }
        if (m_dragging) {
            m_dragPosition = event->position().toPoint(); updateDrag();
            event->accept(); return;
        }
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}
void SensorGrid::updateDrag()
{
    m_dropCell = cellAt(m_dragPosition - m_grabOffset + QPoint(tileSize()/2, tileSize()/2));
    expandTo(m_dropCell);
    constexpr int edge = 24;
    m_autoScrollDirection = QPoint(m_dragPosition.x() < edge ? -1 : m_dragPosition.x() > viewport()->width() - edge ? 1 : 0,
                                  m_dragPosition.y() < edge ? -1 : m_dragPosition.y() > viewport()->height() - edge ? 1 : 0);
    if (m_autoScrollDirection.isNull()) m_autoScroll.stop();
    else if (!m_autoScroll.isActive()) m_autoScroll.start();
    viewport()->update(); notifyVisibleIds();
}
void SensorGrid::finishDrag(bool commit)
{
    m_autoScroll.stop();
    if (m_dragging) {
        const QString source = m_pressed;
        m_dragging = false;
        if (commit) moveTile(source, m_dropCell);
        else { m_bounds = m_beforeDrag; updateLayout(); horizontalScrollBar()->setValue(m_beforeScroll.x()); verticalScrollBar()->setValue(m_beforeScroll.y()); }
    }
    m_pressed.clear(); viewport()->unsetCursor(); viewport()->update(); notifyVisibleIds();
}
void SensorGrid::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { finishDrag(true); event->accept(); return; }
    QAbstractScrollArea::mouseReleaseEvent(event);
}
void SensorGrid::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && indexAt(event->position().toPoint()) < 0) {
        finishDrag(false); emit controlsToggleRequested(); event->accept(); return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}
void SensorGrid::contextMenuEvent(QContextMenuEvent *event)
{
    int index = event->reason() == QContextMenuEvent::Keyboard ? m_order.indexOf(m_selected) : indexAt(event->pos());
    if (index < 0) return;
    const QRect rect = tileRect(index);
    const QString id = m_order[index];
    const bool bottomTitle = event->pos().y() >= rect.top() + tileSize() * .8;
    const bool uptimeTitle = id == QStringLiteral("system/uptime") && m_tiles.value(id).reading.days.isEmpty()
        && event->pos().y() <= rect.top() + tileSize() * .25;
    if (event->reason() != QContextMenuEvent::Keyboard && !bottomTitle && !uptimeTitle) return;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    connect(menu->addAction(tr("Configure")), &QAction::triggered, this, [this,id] { emit configureRequested(id); });
    connect(menu->addAction(tr("Delete")), &QAction::triggered, this, [this,id] { emit deleteRequested(id); });
    connect(menu->addAction(tr("Show/Hide Main Window")), &QAction::triggered, this, &SensorGrid::controlsToggleRequested);
    menu->popup(event->reason() == QContextMenuEvent::Keyboard ? viewport()->mapToGlobal(rect.center()) : event->globalPos());
    event->accept();
}
void SensorGrid::moveTile(const QString &id, const QPoint &cell)
{
    if (!m_tiles.contains(id)) return;
    const QPoint old = m_positions.value(id);
    for (auto it = m_positions.begin(); it != m_positions.end(); ++it)
        if (it.key() != id && it.value() == cell) { it.value() = old; break; }
    m_positions[id] = cell; expandTo(cell); saveLayout(); updateLayout(); select(id);
}
void SensorGrid::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_dragging) { finishDrag(false); event->accept(); return; }
    const int current = m_order.indexOf(m_selected);
    if (current < 0) { QAbstractScrollArea::keyPressEvent(event); return; }
    QPoint delta;
    switch (event->key()) {
    case Qt::Key_Left: delta = {-1,0}; break;
    case Qt::Key_Right: delta = {1,0}; break;
    case Qt::Key_Up: delta = {0,-1}; break;
    case Qt::Key_Down: delta = {0,1}; break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
    const QPoint cell = m_positions.value(m_selected) + delta;
    if (cell.x() <= -coordinateLimit || cell.x() >= coordinateLimit || cell.y() <= -coordinateLimit || cell.y() >= coordinateLimit) return;
    if (event->modifiers() & Qt::ControlModifier) moveTile(m_selected, cell);
    else {
        for (const auto &id : m_order) if (m_positions.value(id) == cell) { select(id); break; }
    }
    ensureVisible(m_order.indexOf(m_selected)); event->accept();
}
} // namespace qsensors
