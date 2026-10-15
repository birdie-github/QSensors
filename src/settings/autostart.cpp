// SPDX-License-Identifier: GPL-3.0-only
#include "settings/autostart.h"
#include "projectmetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace qsensors {
namespace Autostart {
namespace {
const QString relativePath = QStringLiteral("autostart/" QSENSORS_DESKTOP_ID ".desktop");

QString execArgument(const QString &path)
{
    QString argument = QStringLiteral("\"");
    for (const QChar character : path) {
        if (character == QLatin1Char('\\'))
            argument += QString(4, QLatin1Char('\\'));
        else if (character == QLatin1Char('"') || character == QLatin1Char('$') || character == QLatin1Char('`'))
            argument += QString(2, QLatin1Char('\\')) + character;
        else if (character == QLatin1Char('%'))
            argument += QStringLiteral("%%");
        else
            argument += character;
    }
    return argument + QLatin1Char('"');
}
}

bool enabled()
{
    const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, relativePath);
    if (path.isEmpty())
        return false;
    QSettings desktop(path, QSettings::IniFormat);
    return !desktop.value(QStringLiteral("Desktop Entry/Hidden"), false).toBool();
}

bool setEnabled(bool enable, QString &error)
{
    // AppImage's executable lives on a temporary mount; persist the outer file.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString executable = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    if (executable.contains(QLatin1Char('\n')) || executable.contains(QLatin1Char('\r'))
        || executable.contains(QLatin1Char('='))) {
        error = QCoreApplication::translate("Autostart", "The executable path cannot be represented in a desktop entry.");
        return false;
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1Char('/') + relativePath;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QCoreApplication::translate("Autostart", "Cannot create the autostart directory.");
        return false;
    }
    const QByteArray entry = (QStringLiteral("[Desktop Entry]\nType=Application\nName=" QSENSORS_NAME "\nTerminal=false\nHidden=")
        + (enable ? QStringLiteral("false") : QStringLiteral("true"))
        + QStringLiteral("\nExec=") + execArgument(executable) + QLatin1Char('\n')).toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(entry) != entry.size() || !file.commit()) {
        error = QCoreApplication::translate("Autostart", "Cannot save the autostart entry: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace Autostart
} // namespace qsensors
