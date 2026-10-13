// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>

namespace qsensors {
namespace Autostart {
bool enabled();
bool setEnabled(bool enabled, QString &error);
} // namespace Autostart
} // namespace qsensors
