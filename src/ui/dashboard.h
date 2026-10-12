// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>

namespace qsensors {

class SensorTableModel;

class Dashboard final : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(SensorTableModel *model, QWidget *parent = nullptr);
};

} // namespace qsensors
