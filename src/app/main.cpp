// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboard.h"
#include "ui/sensortablemodel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

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

    qsensors::Dashboard dashboard(&model);
    dashboard.show();
    return app.exec();
}
