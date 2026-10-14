// SPDX-License-Identifier: GPL-3.0-only
#include "app/singleinstance.h"
#include "settings/sensorpreferences.h"
#include "settings/settingsdialog.h"
#include "sensors/sensorpresentation.h"
#include "sensors/sensormonitor.h"
#include "tray/traycontroller.h"
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPointer>
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
    qsensors::SensorPreferencesStore preferences;
    qsensors::SensorPresentation presentation(&preferences);
    qsensors::SensorTableModel model(&preferences);
    qsensors::SensorMonitor monitor(&preferences);
    monitor.setDashboardIds({});
    QList<qsensors::SensorDescriptor> rawCatalog;
    QPointer<qsensors::SettingsDialog> settingsDialog;
    qsensors::Dashboard dashboard(&model, &preferences);
    qsensors::TrayController tray(&dashboard, &preferences);
    dashboard.setIntervalSeconds(monitor.intervalSeconds());
    dashboard.setActions(tray.hideAction(), tray.settingsAction(),
                         tray.rescanAction(), tray.aboutAction(), tray.quitAction());
    tray.addApplicationSettingsMenu(dashboard.applicationSettingsMenu());
    dashboard.addAction(tray.showAction());

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
                     &monitor, [&monitor, &dashboard, wasActive = false](bool active) mutable {
        if (active && !wasActive) {
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
                     &presentation, &qsensors::SensorPresentation::applySnapshot);
    QObject::connect(&presentation, &qsensors::SensorPresentation::snapshotReady,
                     &dashboard, &qsensors::Dashboard::applySnapshot);
    QObject::connect(&presentation, &qsensors::SensorPresentation::snapshotReady,
                     &tray, &qsensors::TrayController::applySnapshot);
    QObject::connect(&presentation, &qsensors::SensorPresentation::catalogChanged, &dashboard,
                     [&rawCatalog, &settingsDialog](const QList<qsensors::SensorDescriptor> &catalog) {
        rawCatalog = catalog;
        if (settingsDialog)
            settingsDialog->updateCatalog(catalog);
    });
    QObject::connect(&presentation, &qsensors::SensorPresentation::snapshotReady, &dashboard,
                     [&settingsDialog](const qsensors::SensorSnapshot &snapshot) {
        if (settingsDialog)
            settingsDialog->applySnapshot(snapshot);
    });
    const auto showSettings = [&dashboard, &preferences, &rawCatalog, &settingsDialog, &monitor](const QString &id) {
        if (!settingsDialog) {
            settingsDialog = new qsensors::SettingsDialog(&preferences, rawCatalog, &dashboard);
            QObject::connect(settingsDialog.data(), &qsensors::SettingsDialog::previewIdsChanged,
                             &monitor, &qsensors::SensorMonitor::setPreviewIds);
            // Also release demand if Quit deletes the dialog without a hide event.
            QObject::connect(settingsDialog.data(), &QObject::destroyed, &monitor, [&monitor] {
                monitor.setPreviewIds({});
            });
        }
        settingsDialog->show();
        settingsDialog->raise();
        settingsDialog->activateWindow();
        if (!id.isEmpty()) settingsDialog->selectSensor(id);
    };
    QObject::connect(&tray, &qsensors::TrayController::settingsRequested, &dashboard, [showSettings] { showSettings({}); });
    QObject::connect(&tray, &qsensors::TrayController::sensorSettingsRequested, &dashboard,
                     [showSettings, &settingsDialog](const QString &id) {
        showSettings(id);
        settingsDialog->selectTraySensor(id);
    });
    QObject::connect(&dashboard, &qsensors::Dashboard::sensorConfigureRequested, &dashboard, showSettings);
    QObject::connect(&dashboard, &qsensors::Dashboard::sensorDisabled, &dashboard, [&settingsDialog](const QString &id) {
        if (settingsDialog) settingsDialog->disableDashboardSensor(id);
    });
    monitor.setBackgroundIds(tray.selectedIds());
    tray.start();
    dashboard.show();
    const int exitCode = app.exec();
    delete settingsDialog.data();
    return exitCode;
}
