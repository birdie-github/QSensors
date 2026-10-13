// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>

class QSpinBox;
class QAction;
class QLabel;
class QCloseEvent;

namespace qsensors {

class SensorTableModel;

class Dashboard final : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(SensorTableModel *model, QWidget *parent = nullptr);
    void setIntervalSeconds(int seconds);
    void setTrayAvailable(bool available);
    void setActions(QAction *show, QAction *hide, QAction *settings,
                    QAction *rescan, QAction *about, QAction *quit);

signals:
    void quitRequested();
    void intervalChanged(int seconds);
    void dashboardActiveChanged(bool active);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QSpinBox *m_interval;
    QLabel *m_trayWarning;
    bool m_trayAvailable = false;
};

} // namespace qsensors
