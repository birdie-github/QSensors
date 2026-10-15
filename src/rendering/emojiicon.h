// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QIcon>
#include <QString>

namespace qsensors {
// Render with Qt's font fallback at the icon's requested resolution.
QIcon emojiIcon(const QString &emoji);
}
