// SPDX-License-Identifier: GPL-3.0-only
#include "app/singleinstance.h"
#include "projectmetadata.h"
#include "settings/numberformat.h"
#include "alarms/alarmcontroller.h"
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
#include <QSettings>
#include <QTimer>
#include <QSystemTrayIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(QSENSORS_NAME));
    QCoreApplication::setOrganizationName(QStringLiteral(QSENSORS_ORGANIZATION));
    QCoreApplication::setOrganizationDomain(QStringLiteral(QSENSORS_DOMAIN));
    QCoreApplication::setApplicationVersion(QStringLiteral(QSENSORS_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(QSENSORS_DESKTOP_ID));
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/qsensors-64.png"));
    icon.addFile(QStringLiteral(":/icons/qsensors-256.png"));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", QSENSORS_DESCRIPTION));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    qsensors::SingleInstance instance;
    const auto result = instance.start();
    if (result == qsensors::SingleInstance::Result::ActivatedExisting)
        return 0;
    if (result == qsensors::SingleInstance::Result::Failed) {
        QMessageBox::critical(nullptr, QCoreApplication::applicationName(), instance.errorString());
        return 1;
    }
    app.setQuitOnLastWindowClosed(false);
    qsensors::SensorPreferencesStore preferences;
    qsensors::AlarmController alarms(&preferences);
    qsensors::SensorPresentation presentation(&preferences);
    QObject::connect(&qsensors::NumberFormatSettings::instance(), &qsensors::NumberFormatSettings::changed,
                     &presentation, &qsensors::SensorPresentation::refresh);
    qsensors::SensorTableModel model(&preferences);
    qsensors::SensorMonitor monitor(&preferences);
    monitor.setDashboardIds({});
    QList<qsensors::SensorDescriptor> rawCatalog;
    QPointer<qsensors::SettingsDialog> settingsDialog;
    qsensors::Dashboard dashboard(&model, &preferences);
    qsensors::TrayController tray(&dashboard, &preferences);
    dashboard.setAlarmController(&alarms);
    tray.setAlarmController(&alarms);
    QObject::connect(&alarms, &qsensors::AlarmController::notificationRequested,
                     &tray, &qsensors::TrayController::showAlarmNotification);
    QObject::connect(&tray, &qsensors::TrayController::alarmNotificationFailed,
                     &alarms, &qsensors::AlarmController::actionError);
    QObject::connect(&alarms, &qsensors::AlarmController::actionError, &dashboard,
                     [&dashboard](const QString &, const QString &message) { dashboard.statusBar()->showMessage(message, 10000); });
    dashboard.setIntervalSeconds(monitor.intervalSeconds());
    dashboard.setActions(tray.hideAction(), tray.settingsAction(),
                         tray.rescanAction(), tray.aboutAction(), tray.quitAction());
    tray.addApplicationSettingsMenu(dashboard.applicationSettingsMenu());
    dashboard.addAction(tray.showAction());

    const auto showDashboard = [&dashboard] {
        QSettings().remove(QStringLiteral("Dashboard/hiddenToTray"));
        auto state = dashboard.windowState();
        state.setFlag(Qt::WindowMinimized, false);
        dashboard.setWindowState(state);
        dashboard.show();
        dashboard.raise();
        dashboard.activateWindow(); // A request; Wayland focus policy remains authoritative.
    };
    const auto hideDashboard = [&dashboard] {
        QSettings().setValue(QStringLiteral("Dashboard/hiddenToTray"), true);
        dashboard.hide();
    };
    QObject::connect(&instance, &qsensors::SingleInstance::showRequested, &dashboard, showDashboard);
    QObject::connect(&tray, &qsensors::TrayController::showRequested, &dashboard, showDashboard);
    QObject::connect(&tray, &qsensors::TrayController::hideRequested, &dashboard, [hideDashboard, showDashboard] {
        if (QSystemTrayIcon::isSystemTrayAvailable())
            hideDashboard();
        else
            showDashboard();
    });
    QObject::connect(&tray, &qsensors::TrayController::toggleRequested, &dashboard, [&dashboard, hideDashboard, showDashboard] {
        if (dashboard.isVisible() && !dashboard.isMinimized() && QSystemTrayIcon::isSystemTrayAvailable())
            hideDashboard();
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
                     &alarms, &qsensors::AlarmController::applySnapshot);
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
    const auto showSettings = [&dashboard, &preferences, &rawCatalog, &settingsDialog, &monitor, &alarms](const QString &id) {
        if (!settingsDialog) {
            settingsDialog = new qsensors::SettingsDialog(&preferences, rawCatalog, &dashboard);
            settingsDialog->setAlarmController(&alarms);
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
    QObject::connect(&tray, &qsensors::TrayController::sensorRemoved, &dashboard, [&settingsDialog](const QString &id) {
        if (settingsDialog) settingsDialog->disableTraySensor(id);
    });
    monitor.setBackgroundIds(tray.selectedIds());
    tray.start();
    const bool trayAvailable = tray.isAvailable();
    const bool showOnStartup = !trayAvailable
        || !QSettings().value(QStringLiteral("Dashboard/hiddenToTray"), false).toBool();
    tray.setDashboardVisible(showOnStartup);
    if (showOnStartup) dashboard.show();
    if (!trayAvailable) {
        // Startup warning only: later host checks must not repeat the dialog.
        QTimer::singleShot(0, &dashboard, [&dashboard] {
            QMessageBox::warning(&dashboard, QCoreApplication::translate("main", "Warning"),
                QCoreApplication::translate("main", "Systray is not available"), QMessageBox::Ok);
        });
    }
    const int exitCode = app.exec();
    delete settingsDialog.data();
    return exitCode;
}
