// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "rendering/sensortilerenderer.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QStringList>
#include <QTimer>

class QMimeData;

namespace qsensors {

class SensorPreferencesStore;

// Paints visible tiles directly into one viewport; no per-tile widgets/pixmaps.
class SensorGrid final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SensorGrid(SensorPreferencesStore *preferences, QWidget *parent = nullptr);
    int tileSize() const { return m_style.size; }
    bool gaugesVisible() const { return m_gauges; }
    void setTileSize(int size);
    void setGaugesVisible(bool visible);
    void setDescriptors(const QList<SensorDescriptor> &descriptors);
    void applySamples(const QList<SensorSample> &samples);
    QStringList visibleIds() const;

signals:
    void visibleIdsChanged(const QStringList &ids);

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
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Tile {
        SensorDescriptor descriptor;
        SensorSample sample;
        TileReading reading;
    };
    void rebuildOrder();
    void saveOrder();
    void updateLayout();
    void notifyVisibleIds();
    QRect tileRect(int index) const;
    int indexAt(const QPoint &position, bool nearest = false) const;
    void select(const QString &id);
    void ensureVisible(int index);
    void moveTile(const QString &source, const QString &target, bool after);
    bool acceptDrag(const QMimeData *data, QObject *source) const;
    void updateDropTarget(const QPoint &position);

    SensorPreferencesStore *m_preferences;
    QHash<QString, Tile> m_tiles;
    QStringList m_savedOrder;
    QStringList m_order;
    QStringList m_lastVisible;
    TileStyle m_style;
    bool m_gauges = true;
    bool m_updatingLayout = false;
    int m_columns = 1;
    int m_gap = 8;
    QString m_selected;
    QString m_pressed;
    QPoint m_pressPosition;
    QString m_dropTarget;
    bool m_dropAfter = false;
    QPoint m_dragPosition;
    QPoint m_autoScrollDirection;
    QTimer m_autoScroll;
};

} // namespace qsensors
