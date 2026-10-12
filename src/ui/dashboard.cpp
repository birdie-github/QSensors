// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QAction>
#include <QCoreApplication>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTableView>

namespace qsensors {

Dashboard::Dashboard(SensorTableModel *model, QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QCoreApplication::applicationName());
    resize(640, 400);
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    if (model->rowCount() == 0) {
        auto *empty = new QLabel(tr("Hardware monitoring is not implemented yet."), this);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->setMargin(16);
        setCentralWidget(empty);
    } else {
        auto *table = new QTableView(this);
        table->setModel(model);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        setCentralWidget(table);
    }
}

} // namespace qsensors
