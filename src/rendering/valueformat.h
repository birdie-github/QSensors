// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sensors/sensortypes.h"

namespace qsensors {
struct UptimeValue { QString time; QString days; };
UptimeValue formatUptime(double seconds);
struct FormattedValue { QString number; QString unit; };
FormattedValue formatSensorValue(const SensorDescriptor &descriptor, double value, bool tray = false);
}
