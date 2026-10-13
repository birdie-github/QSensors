// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensorprovider.h"
#include "system/systemmetrics.h"

namespace qsensors {

// Confined to the same serialized worker as libsensors.
class SystemSensorProvider final : public SensorProvider {
public:
    SensorDiscovery discover() override;
    QList<SensorSample> readBatch(const QStringList &ids) override;
private:
    struct Core { int package; int core; QString id; QList<int> threads; };
    QList<Core> m_cores;
    std::optional<systemmetrics::CpuCounters> m_previousCpu;
};

} // namespace qsensors
