// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QString>
class QSettings;

namespace qsensors {

struct AlarmSettings {
    bool lowerEnabled = false;
    bool upperEnabled = false;
    double lower = 0;
    double upper = 100;
    double hysteresis = 0;
    bool dashboard = false;
    bool table = false;
    bool notify = false;
    bool command = false;
    bool trayMarkers = false;
    bool trayBlink = false;
    bool trayColor = false;
    QColor dashboardColor = QColor(255, 85, 0);
    QColor tableColor = QColor(220, 40, 40);
    QColor trayTextColor = QColor(255, 85, 0);
    QString program;
    QString arguments;

    bool hasActions() const;
    bool enabled() const { return (lowerEnabled || upperEnabled) && hasActions(); }
    bool valid() const;
    bool operator==(const AlarmSettings &other) const;
};

AlarmSettings readAlarmSettings(QSettings &settings);
void writeAlarmSettings(QSettings &settings, const AlarmSettings &alarm);

} // namespace qsensors
