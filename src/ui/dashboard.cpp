// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QAction>
#include <QCoreApplication>
#include <QCloseEvent>
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
#include <QSystemTrayIcon>

namespace qsensors {

Dashboard::Dashboard(SensorTableModel *model, QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 500);
    auto *toolbar = addToolBar(tr("Sensors"));
    toolbar->setObjectName(QStringLiteral("sensorsToolbar"));
    toolbar->setMovable(false);
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
    m_trayWarning = new QLabel(tr("No system tray host — closing the window exits."), this);
    m_trayWarning->setWordWrap(true);
    m_trayWarning->setToolTip(tr("A compatible system tray host is required for background operation."));
    statusBar()->addPermanentWidget(m_trayWarning);
}

void Dashboard::setActions(QAction *show, QAction *hide, QAction *settings,
                           QAction *rescan, QAction *about, QAction *quit)
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(show);
    file->addAction(hide);
    file->addSeparator();
    file->addAction(settings);
    file->addSeparator();
    file->addAction(quit);
    auto *sensors = menuBar()->addMenu(tr("&Sensors"));
    sensors->addAction(rescan);
    menuBar()->addMenu(tr("&Help"))->addAction(about);
    auto *toolbar = findChild<QToolBar *>(QStringLiteral("sensorsToolbar"));
    toolbar->insertAction(toolbar->actions().first(), rescan);
    toolbar->addSeparator();
    toolbar->addAction(settings);
}

void Dashboard::setTrayAvailable(bool available)
{
    m_trayAvailable = available;
    m_trayWarning->setVisible(!available);
}

void Dashboard::closeEvent(QCloseEvent *event)
{
    // Recheck now rather than trusting the host watchdog's previous result.
    if (m_trayAvailable && QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        hide();
    } else {
        event->accept();
        emit quitRequested();
    }
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
