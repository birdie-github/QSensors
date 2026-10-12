// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>

class QSpinBox;

namespace qsensors {

class SensorTableModel;

class Dashboard final : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(SensorTableModel *model, QWidget *parent = nullptr);
    void setIntervalSeconds(int seconds);

signals:
    void rescanRequested();
    void intervalChanged(int seconds);
    void dashboardActiveChanged(bool active);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QSpinBox *m_interval;
};

} // namespace qsensors
