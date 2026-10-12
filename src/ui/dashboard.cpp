// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>

namespace qsensors {

Dashboard::Dashboard(SensorTableModel *model, QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 500);
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *sensorMenu = menuBar()->addMenu(tr("&Sensors"));
    auto *rescanAction = sensorMenu->addAction(tr("&Rescan"));
    rescanAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(rescanAction, &QAction::triggered, this, &Dashboard::rescanRequested);
    auto *toolbar = addToolBar(tr("Sensors"));
    toolbar->setMovable(false);
    toolbar->addAction(rescanAction);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Update interval: "), toolbar));
    m_interval = new QSpinBox(toolbar);
    m_interval->setRange(1, 3600);
    m_interval->setSuffix(tr(" s"));
    m_interval->setValue(5);
    m_interval->setKeyboardTracking(false);
    m_interval->setAccessibleName(tr("Update interval in seconds"));
    toolbar->addWidget(m_interval);
    connect(m_interval, &QSpinBox::valueChanged, this, &Dashboard::intervalChanged);

    auto *stack = new QStackedWidget(this);
    auto *empty = new QLabel(tr("Scanning sensors…"), stack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    empty->setMargin(16);
    stack->addWidget(empty);
    auto *table = new QTableView(stack);
    table->setModel(model);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    stack->addWidget(table);
    setCentralWidget(stack);
    connect(model, &QAbstractItemModel::modelReset, this, [model, stack] {
        stack->setCurrentIndex(model->rowCount() ? 1 : 0);
    });
    connect(statusBar(), &QStatusBar::messageChanged, empty, &QLabel::setText);
}

void Dashboard::setIntervalSeconds(int seconds)
{
    const QSignalBlocker blocker(m_interval);
    m_interval->setValue(seconds);
}

void Dashboard::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    emit dashboardActiveChanged(!isMinimized());
}

void Dashboard::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    emit dashboardActiveChanged(false);
}

void Dashboard::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        emit dashboardActiveChanged(isVisible() && !isMinimized());
}

} // namespace qsensors
