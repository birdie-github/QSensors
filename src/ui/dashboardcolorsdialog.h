// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "rendering/dashboardappearance.h"
#include <optional>

class QWidget;

namespace qsensors {
// Returns a draft only on acceptance; the caller owns persistence and activation.
std::optional<DashboardAppearance> editDashboardColors(const DashboardAppearance &initial, QWidget *parent);
}
