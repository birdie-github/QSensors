// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sensors/sensortypes.h"

namespace qsensors {
struct UptimeValue { QString time; QString days; };
UptimeValue formatUptime(double seconds);
struct FormattedValue { QString number; QString unit; };
// Dashboard-only digital formatting; never used for calibration or sorting.
QString formatDigitalSensorValue(const SensorDescriptor &descriptor, double value, bool kelvin = false);
// Convert an ungrouped C-format number to the locale decimal separator.
QString localizeDecimalPoint(QString number);
// Unit-free tray numbers: four characters, including decimal point/sign.
QString formatTrayNumber(double value);
QString formatTrayMemoryNumber(double mebibytes);
FormattedValue formatSensorValue(const SensorDescriptor &descriptor, double value, bool tray = false);
}
