// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensorprovider.h"
#include "system/systemmetrics.h"

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QSet>

namespace qsensors {

// Confined to the same serialized worker as libsensors.
class SystemSensorProvider final : public SensorProvider {
public:
    SensorDiscovery discover() override;
    QList<SensorSample> readBatch(const QStringList &ids) override;
private:
    struct Core { int package; int core; QString id; QList<int> threads; };
    void clearFrequencyMetadata();
    std::optional<double> frequencyMaximum(int cpu, QSet<QString> &failedPolicies);
    QList<Core> m_cores;
    QByteArray m_onlineMask;
    QList<QPair<int, int>> m_onlineRanges;
    QHash<int, QString> m_cpuPolicyPaths;
    QHash<QString, double> m_policyMaxima;
    std::optional<systemmetrics::CpuCounters> m_previousCpu;
};

} // namespace qsensors
