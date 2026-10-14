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
#include <QWindow>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {
namespace {
// Keep cell-to-pixel conversions safe even at the largest supported tile size.
constexpr int coordinateLimit = dashboardCellLimit;
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
    m_style = SensorTileRenderer::style(valid && size >= minimumTileSize && size <= maximumTileSize ? size : 128, font());
    const auto preferencesById = m_preferences->values();
    QStringList ids = preferencesById.keys();
    std::sort(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const auto preferences = preferencesById.value(id);
        if (!preferences.dashboard || !preferences.position) continue;
        const QPoint cell = *preferences.position;
        const QRect next = m_bounds.united(QRect(cell, QSize(1, 1)));
        if (!m_positions.values().contains(cell) && next.width() <= coordinateLimit && next.height() <= coordinateLimit) {
            m_positions.insert(id, cell);
            m_bounds = next;
        }
    }
    // Normalize in memory; validated positions are saved once the catalog is supplied.
    m_layoutDirty = trimBounds();
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
QSize SensorGrid::fieldSize() const
{
    return fieldSizeForTileSize(tileSize());
}
QSize SensorGrid::fieldSizeForTileSize(int size) const
{
    const int stride = std::clamp(size, minimumTileSize, maximumTileSize) + m_gap;
    return QSize(m_gap + m_bounds.width() * stride, m_gap + m_bounds.height() * stride);
}
int SensorGrid::tileSizeForFieldSize(const QSize &size) const
{
    // Fit the complete field in both axes. Derive size from absolute geometry,
    // so changing drag direction cannot switch between competing estimates.
    const int columns = (size.width() - m_gap) / m_bounds.width() - m_gap;
    const int rows = (size.height() - m_gap) / m_bounds.height() - m_gap;
    return std::clamp(std::min(columns, rows), minimumTileSize, maximumTileSize);
}
void SensorGrid::setCompactView(bool compact)
{
    finishDrag(false);
    m_windowPressed = false;
    m_compact = compact;
    const auto policy = compact ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
    setHorizontalScrollBarPolicy(policy);
    setVerticalScrollBarPolicy(policy);
    setFrameShape(compact ? QFrame::NoFrame : QFrame::StyledPanel);
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
    updateLayout();
}
void SensorGrid::saveTileSize() const
{
    QSettings().setValue(QStringLiteral("Dashboard/tileSize"), tileSize());
}
void SensorGrid::setTileSize(int size, bool persist)
{
    size = std::clamp(size, minimumTileSize, maximumTileSize);
    if (size == tileSize()) return;
    finishDrag(false);
    m_style = SensorTileRenderer::style(size, font());
    if (persist) saveTileSize();
    updateLayout();
    ensureVisible(m_order.indexOf(m_selected));
    emit fieldSizeChanged();
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
    bool changed = m_layoutDirty;
    bool removed = false;
    for (auto it = m_positions.begin(); it != m_positions.end();) {
        if (!m_preferences->value(it.key()).dashboard) { it = m_positions.erase(it); changed = true; removed = true; }
        else ++it;
    }
    if (m_positions.isEmpty() && !descriptors.isEmpty()) {
        // Choose initial proportions once; never reflow a positioned layout.
        const auto count = descriptors.size();
        const auto rows = std::max<qsizetype>(1, static_cast<qsizetype>(std::sqrt(static_cast<double>(count))));
        const auto columns = count / rows + (count % rows != 0);
        m_bounds = QRect(0, 0, static_cast<int>(std::min<qsizetype>(columns, coordinateLimit)),
                         static_cast<int>(std::min<qsizetype>(rows, coordinateLimit)));
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
    if (removed) trimBounds();
    if (changed) saveLayout();
    updateLayout();
    emit fieldSizeChanged();
}
void SensorGrid::saveLayout()
{
    QString error;
    if (!m_preferences->savePositions(m_positions, error)) emit layoutSaveFailed(error);
    else m_layoutDirty = false;
}
void SensorGrid::applySamples(const QList<SensorSample> &samples, bool incremental)
{
    QHash<QString, SensorSample> batch;
    batch.reserve(samples.size());
    for (const auto &sample : samples)
        batch.insert(sample.id, sample);
    QRegion changed;
    for (int i = 0; i < m_order.size(); ++i) {
        if (incremental && !batch.contains(m_order[i])) continue;
        auto &tile = m_tiles[m_order[i]];
        auto sample = batch.value(m_order[i]);
        sample.id = m_order[i];
        const auto preferences = m_preferences->value(tile.descriptor.id);
        const auto reading = SensorTileRenderer::prepare(tile.descriptor, sample, preferences, m_preferences->temperatureUnit());
        const bool gauge = preferences.dashboardGauge && sensorSupportsGauge(tile.descriptor.id, tile.descriptor.kind);
        const bool digital = preferences.dashboardTextStyle == DashboardTextStyle::Digital;
        const bool different = reading.available != tile.reading.available
            || (digital ? (reading.digitalText != tile.reading.digitalText || reading.digitalUnit != tile.reading.digitalUnit)
                        : (reading.text != tile.reading.text || reading.unit != tile.reading.unit))
            || reading.days != tile.reading.days
            || reading.valueColor != tile.reading.valueColor
            || (gauge && (reading.fraction != tile.reading.fraction
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
    // Disable scrolling itself in compact mode, including wheel/keyboard offsets.
    horizontalScrollBar()->setRange(0, m_compact ? 0 : std::max(0, m_gap + m_bounds.width() * stride - viewport()->width()));
    verticalScrollBar()->setRange(0, m_compact ? 0 : std::max(0, m_gap + m_bounds.height() * stride - viewport()->height()));
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
    if (next == m_bounds || next.width() > coordinateLimit || next.height() > coordinateLimit) return;
    const QPoint compensation((m_bounds.left() - next.left()) * (tileSize() + m_gap),
                              (m_bounds.top() - next.top()) * (tileSize() + m_gap));
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    m_bounds = next;
    updateLayout();
    horizontalScrollBar()->setValue(scroll.x() + compensation.x());
    verticalScrollBar()->setValue(scroll.y() + compensation.y());
}
bool SensorGrid::trimBounds()
{
    QRect occupied;
    for (const auto &cell : m_positions)
        occupied = occupied.united(QRect(cell, QSize(1, 1)));
    // Keep one usable empty cell when the last tile is removed.
    if (occupied.isEmpty()) occupied = QRect(0, 0, 1, 1);
    const QRect normalized(QPoint(0, 0), occupied.size());
    if (occupied == m_bounds && occupied.topLeft().isNull()) return false;
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QPoint compensation((m_bounds.left() - occupied.left()) * (tileSize() + m_gap),
                              (m_bounds.top() - occupied.top()) * (tileSize() + m_gap));
    const QPoint origin = occupied.topLeft();
    for (auto it = m_positions.begin(); it != m_positions.end(); ++it) it.value() -= origin;
    m_bounds = normalized;
    updateLayout();
    horizontalScrollBar()->setValue(scroll.x() + compensation.x());
    verticalScrollBar()->setValue(scroll.y() + compensation.y());
    return true;
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
        if (prefs.dashboardTransparent) colors.setColor(QPalette::Base, palette().color(QPalette::Window));
        else if (prefs.background.isValid()) colors.setColor(QPalette::Base, prefs.background);
        SensorTileRenderer::draw(painter, rect, tile.descriptor, tile.reading, m_style, colors, id == m_selected, prefs);
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
            QString tooltip = tr("%1\n%2\n%3").arg(tile.descriptor.label, tile.descriptor.device, value);
            const QString detail = valid ? tile.sample.detail
                : tile.sample.error.isEmpty() ? tr("No current reading") : tile.sample.error;
            if (!detail.isEmpty()) tooltip += QLatin1Char('\n') + detail;
            QToolTip::showText(help->globalPos(), tooltip, viewport());
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
    if (event->button() == Qt::LeftButton && m_compact) {
        m_windowGrabOffset = event->globalPosition().toPoint() - window()->pos();
        // Start while the initiating press is active, as required by Wayland.
        const bool nativeMove = window()->windowHandle() && window()->windowHandle()->startSystemMove();
        m_windowPressed = !nativeMove && QGuiApplication::platformName() == QStringLiteral("xcb");
        event->accept(); return;
    }
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
    if (m_compact) {
        if (m_windowPressed && (event->buttons() & Qt::LeftButton))
            window()->move(event->globalPosition().toPoint() - m_windowGrabOffset);
        event->accept(); return;
    }
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
        else {
            m_bounds = m_beforeDrag;
            updateLayout();
            horizontalScrollBar()->setValue(m_beforeScroll.x());
            verticalScrollBar()->setValue(m_beforeScroll.y());
            if (trimBounds()) saveLayout();
        }
    }
    m_pressed.clear(); viewport()->unsetCursor(); viewport()->update(); notifyVisibleIds();
}
void SensorGrid::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_windowPressed = false;
        finishDrag(!m_compact); event->accept(); return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}
void SensorGrid::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (m_compact || indexAt(event->position().toPoint()) < 0)) {
        m_windowPressed = false;
        finishDrag(false); emit controlsToggleRequested(); event->accept(); return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}
void SensorGrid::contextMenuEvent(QContextMenuEvent *event)
{
    int index = event->reason() == QContextMenuEvent::Keyboard ? m_order.indexOf(m_selected) : indexAt(event->pos());
    finishDrag(false);
    const QString id = index >= 0 ? m_order[index] : QString();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (!id.isEmpty()) {
        connect(menu->addAction(tr("Configure")), &QAction::triggered, this, [this,id] { emit configureRequested(id); });
        connect(menu->addAction(tr("Delete")), &QAction::triggered, this, [this,id] { emit deleteRequested(id); });
    }
    connect(menu->addAction(m_compact ? tr("Show controls") : tr("Hide controls")), &QAction::triggered, this, &SensorGrid::controlsToggleRequested);
    const QPoint anchor = index >= 0 ? tileRect(index).center() : viewport()->rect().center();
    menu->popup(event->reason() == QContextMenuEvent::Keyboard ? viewport()->mapToGlobal(anchor) : event->globalPos());
    event->accept();
}
void SensorGrid::moveTile(const QString &id, const QPoint &cell)
{
    if (m_compact || !m_tiles.contains(id)) return;
    const QPoint old = m_positions.value(id);
    QRect occupied(cell, QSize(1, 1));
    for (auto it = m_positions.cbegin(); it != m_positions.cend(); ++it) {
        if (it.key() != id) occupied = occupied.united(QRect(it.value() == cell ? old : it.value(), QSize(1, 1)));
    }
    if (occupied.width() > coordinateLimit || occupied.height() > coordinateLimit) {
        if (trimBounds()) saveLayout();
        return;
    }
    for (auto it = m_positions.begin(); it != m_positions.end(); ++it)
        if (it.key() != id && it.value() == cell) { it.value() = old; break; }
    m_positions[id] = cell;
    // Temporary drag growth can be discarded once the destination is committed.
    trimBounds();
    saveLayout();
    updateLayout();
    select(id);
    ensureVisible(m_order.indexOf(id));
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
