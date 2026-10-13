// SPDX-License-Identifier: GPL-3.0-only
#include "app/singleinstance.h"
#include "sensors/sensormonitor.h"
#include "tray/traycontroller.h"
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>
#include <QStatusBar>
#include <QSystemTrayIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QSensors"));
    QCoreApplication::setOrganizationName(QStringLiteral("QSensors"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("birdie-github.github.io"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QSENSORS_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.birdie_github.QSensors"));
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/qsensors-64.png"));
    icon.addFile(QStringLiteral(":/icons/qsensors-256.png"));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Linux hardware sensor monitor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    qsensors::SingleInstance instance;
    const auto result = instance.start();
    if (result == qsensors::SingleInstance::Result::ActivatedExisting)
        return 0;
    if (result == qsensors::SingleInstance::Result::Failed) {
        QMessageBox::critical(nullptr, QStringLiteral("QSensors"), instance.errorString());
        return 1;
    }
    app.setQuitOnLastWindowClosed(false);
    qsensors::SensorTableModel model;
    qsensors::SensorMonitor monitor;
    monitor.setDashboardIds({});
    qsensors::Dashboard dashboard(&model);
    qsensors::TrayController tray(&dashboard);
    dashboard.setIntervalSeconds(monitor.intervalSeconds());
    dashboard.setActions(tray.showAction(), tray.hideAction(), tray.settingsAction(),
                         tray.rescanAction(), tray.aboutAction(), tray.quitAction());

    const auto showDashboard = [&dashboard] {
        auto state = dashboard.windowState();
        state.setFlag(Qt::WindowMinimized, false);
        dashboard.setWindowState(state);
        dashboard.show();
        dashboard.raise();
        dashboard.activateWindow(); // A request; Wayland focus policy remains authoritative.
    };
    QObject::connect(&instance, &qsensors::SingleInstance::showRequested, &dashboard, showDashboard);
    QObject::connect(&tray, &qsensors::TrayController::showRequested, &dashboard, showDashboard);
    QObject::connect(&tray, &qsensors::TrayController::hideRequested, &dashboard, [&dashboard, showDashboard] {
        if (QSystemTrayIcon::isSystemTrayAvailable())
            dashboard.hide();
        else
            showDashboard();
    });
    QObject::connect(&tray, &qsensors::TrayController::toggleRequested, &dashboard, [&dashboard, showDashboard] {
        if (dashboard.isVisible() && !dashboard.isMinimized() && QSystemTrayIcon::isSystemTrayAvailable())
            dashboard.hide();
        else
            showDashboard();
    });
    QObject::connect(&tray, &qsensors::TrayController::availabilityChanged,
                     &dashboard, &qsensors::Dashboard::setTrayAvailable);
    QObject::connect(&tray, &qsensors::TrayController::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&dashboard, &qsensors::Dashboard::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&dashboard, &qsensors::Dashboard::dashboardActiveChanged,
                     &tray, &qsensors::TrayController::setDashboardVisible);
    QObject::connect(&dashboard, &qsensors::Dashboard::dashboardActiveChanged,
                     &monitor, [&monitor, &model, &dashboard, wasActive = false](bool active) mutable {
        if (active && !wasActive) {
            model.applySamples({});
            dashboard.clearReadings();
        }
        wasActive = active;
        monitor.setDashboardActive(active);
    });
    QObject::connect(&dashboard, &qsensors::Dashboard::visibleSensorIdsChanged,
                     &monitor, &qsensors::SensorMonitor::setDashboardIds);
    QObject::connect(&tray, &qsensors::TrayController::rescanRequested,
                     &monitor, &qsensors::SensorMonitor::rescan);
    QObject::connect(&tray, &qsensors::TrayController::selectionChanged,
                     &monitor, &qsensors::SensorMonitor::setBackgroundIds);
    QObject::connect(&dashboard, &qsensors::Dashboard::intervalChanged,
                     &monitor, &qsensors::SensorMonitor::setIntervalSeconds);
    QObject::connect(&monitor, &qsensors::SensorMonitor::statusChanged,
                     dashboard.statusBar(), [bar = dashboard.statusBar()](const QString &text) {
        bar->showMessage(text);
    });
    QObject::connect(&monitor, &qsensors::SensorMonitor::snapshotReady,
                     &model, [&model](const qsensors::SensorSnapshot &snapshot) {
        if (snapshot.rescanned)
            model.setDescriptors(snapshot.descriptors);
        model.applySamples(snapshot.samples);
    });
    QObject::connect(&monitor, &qsensors::SensorMonitor::snapshotReady,
                     &dashboard, &qsensors::Dashboard::applySnapshot);
    QObject::connect(&monitor, &qsensors::SensorMonitor::snapshotReady,
                     &tray, &qsensors::TrayController::applySnapshot);
    monitor.setBackgroundIds(tray.selectedIds());
    tray.start();
    dashboard.show();
    return app.exec();
}
