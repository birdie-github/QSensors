// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/sensormonitor.h"
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStatusBar>

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

    qsensors::SensorTableModel model;

    qsensors::SensorMonitor monitor;
    qsensors::Dashboard dashboard(&model);
    dashboard.setIntervalSeconds(monitor.intervalSeconds());
    QObject::connect(&dashboard, &qsensors::Dashboard::dashboardActiveChanged,
                     &monitor, [&monitor, &model, wasActive = false](bool active) mutable {
        if (active && !wasActive)
            model.applySamples({}); // Do not show old readings as current on restoration.
        wasActive = active;
        monitor.setDashboardActive(active);
    });
    QObject::connect(&dashboard, &qsensors::Dashboard::rescanRequested,
                     &monitor, &qsensors::SensorMonitor::rescan);
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
    dashboard.show();
    return app.exec();
}
