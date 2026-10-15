// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "rendering/sensortilerenderer.h"
#include <QAbstractScrollArea>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <optional>

namespace qsensors {
class SensorPreferencesStore;
// Sparse fixed cells; viewport resizing never rearranges sensors.
class AlarmController;
class SensorGrid final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SensorGrid(SensorPreferencesStore *preferences, QWidget *parent = nullptr);
    int tileSize() const { return m_style.size; }
    QSize fieldSize() const;
    void setAlarmController(AlarmController *alarms);
    QSize fieldSizeForTileSize(int size) const;
    int tileSizeForFieldSize(const QSize &size) const;
    void setCompactView(bool compact);
    void setTileSize(int size, bool persist = true);
    void saveTileSize() const;
    DashboardTileShape tileShape() const { return m_tileShape; }
    int circleShrinkPercent() const { return m_circleShrinkPercent; }
    void setTileShape(DashboardTileShape shape);
    void setCircleShrinkPercent(int percent);
    void chooseTileFont(bool sensorName);
    void resetTileFonts();
    DashboardPalette palettePreset() const { return m_palettePreset; }
    DashboardBackground backgroundPattern() const { return m_backgroundPattern; }
    void setPalettePreset(DashboardPalette preset);
    void customizeColors();
    void setBackgroundPattern(DashboardBackground pattern);
    void setDescriptors(const QList<SensorDescriptor> &descriptors);
    void applySamples(const QList<SensorSample> &samples, bool incremental = false);
    QStringList visibleIds() const;
signals:
    void visibleIdsChanged(const QStringList &ids);
    void configureRequested(const QString &id);
    void deleteRequested(const QString &id);
    void controlsToggleRequested();
    void layoutSaveFailed(const QString &error);
    void fieldSizeChanged();
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    struct Tile { SensorDescriptor descriptor; SensorSample sample; TileReading reading; };
    void saveLayout();
    void updateLayout();
    void updateAppearance();
    void updateStyle(int size);
    void notifyVisibleIds();
    QRect tileRect(int index) const;
    QRect cellRect(const QPoint &cell) const;
    QPoint cellAt(const QPoint &position) const;
    int indexAt(const QPoint &position) const;
    int navigationTarget(int current, bool vertical, bool forward) const;
    void select(const QString &id);
    void ensureVisible(int index);
    void moveTile(const QString &id, const QPoint &cell);
    void updateDrag();
    void finishDrag(bool commit);
    QPoint firstEmptyCell();
    void expandTo(const QPoint &cell);
    bool trimBounds();

    AlarmController *m_alarms = nullptr;
    SensorPreferencesStore *m_preferences;
    QHash<QString, Tile> m_tiles;
    QHash<QString, QPoint> m_positions;
    QStringList m_order;
    QHash<QString, int> m_indices;
    QStringList m_lastVisible;
    QRect m_bounds{0, 0, 1, 1};
    TileStyle m_style;
    std::optional<QFont> m_nameFont;
    std::optional<QFont> m_dataFont;
    DashboardTileShape m_tileShape = DashboardTileShape::RoundedRectangle;
    int m_circleShrinkPercent = 20;
    DashboardPalette m_palettePreset = DashboardPalette::System;
    DashboardBackground m_backgroundPattern = DashboardBackground::Solid;
    DashboardAppearance m_appearance;
    QBrush m_backgroundBrush;
    bool m_compact = false;
    bool m_windowPressed = false;
    QPoint m_windowGrabOffset;
    bool m_updatingLayout = false;
    bool m_dragging = false;
    bool m_layoutDirty = false;
    int m_gap = 8;
    QString m_selected;
    QString m_pressed;
    QPoint m_pressPosition;
    QPoint m_grabOffset;
    QPoint m_dragPosition;
    QPoint m_dropCell;
    QRect m_beforeDrag;
    QPoint m_beforeScroll;
    QPoint m_autoScrollDirection;
    QTimer m_autoScroll;
};
}
