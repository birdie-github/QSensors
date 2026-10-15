// SPDX-License-Identifier: GPL-3.0-only
#include "alarms/alarmsettings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace qsensors;
namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary settings directory");
        const QString path = directory.filePath(QStringLiteral("settings.ini"));
        AlarmSettings alarm;
        alarm.active = false;
        alarm.notify = true;
        alarm.trayColor = true;
        alarm.trayMarkers = true;
        alarm.upper = 15;
        alarm.upperEnabled = true;
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.beginGroup(QStringLiteral("Sensor-001"));
            settings.setValue(QStringLiteral("id"), QStringLiteral("example"));
            // Simulate redundant keys from an older expanded configuration.
            settings.setValue(QStringLiteral("Alarm/command"), false);
            settings.setValue(QStringLiteral("Alarm/lower"), 0);
            settings.setValue(QStringLiteral("Alarm/dashboardColor"), QStringLiteral("#ff5500"));
            settings.setValue(QStringLiteral("Alarm/program"), QStringLiteral("old-command"));
            settings.setValue(QStringLiteral("Alarm/arguments"), QStringLiteral("old-argument"));
            writeAlarmSettings(settings, alarm);
            QStringList expected = {QStringLiteral("id"), QStringLiteral("Alarm/enabled"),
                QStringLiteral("Alarm/notify"), QStringLiteral("Alarm/trayColor"),
                QStringLiteral("Alarm/trayMarkers"), QStringLiteral("Alarm/upper"),
                QStringLiteral("Alarm/upperEnabled")};
            expected.sort();
            auto actual = settings.allKeys();
            actual.sort();
            require(actual == expected, "only six alarm overrides remain; unrelated id preserved");
            require(readAlarmSettings(settings) == alarm, "sparse alarm round trip preserves paused master switch");
            settings.endGroup();
            settings.sync();
            require(settings.status() == QSettings::NoError, "write sparse INI to disk");
        }
        QSettings settings(path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Sensor-001"));
        require(readAlarmSettings(settings) == alarm, "reload sparse alarm from disk");
        AlarmSettings zeroUpper;
        zeroUpper.upper = 0;
        zeroUpper.tableColor = QColor(1, 2, 3);
        writeAlarmSettings(settings, zeroUpper);
        require(settings.contains(QStringLiteral("Alarm/upper")), "zero overriding a nonzero default must be stored");
        require(readAlarmSettings(settings) == zeroUpper, "numeric and custom color round trip");
        writeAlarmSettings(settings, AlarmSettings{});
        require(settings.childGroups().isEmpty(), "reset removes old alarm keys and empty group");
        require(readAlarmSettings(settings) == AlarmSettings{}, "missing keys reconstruct defaults");
        require(settings.value(QStringLiteral("id")).toString() == QStringLiteral("example"), "reset preserves unrelated sensor key");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
