// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QList>
#include <QStringList>

namespace qsensors {

// Provider instances are confined to one execution context. A hardware provider
// must run on the serialized worker introduced in phase 2, never in the UI.
// Results own their data; library-owned pointers must not cross this boundary.
class SensorProvider {
public:
    virtual ~SensorProvider() = default;
    virtual SensorDiscovery discover() = 0;
    // One sample per requested ID, including explicit unavailable results.
    virtual QList<SensorSample> readBatch(const QStringList &ids) = 0;
};

} // namespace qsensors
