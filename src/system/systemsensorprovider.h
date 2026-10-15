// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensorprovider.h"
#include "system/systemmetrics.h"

#include <QByteArray>
#include <QHash>
#include <QElapsedTimer>
#include <QPair>
#include <QSet>
#include <array>

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
    QHash<QString, QString> m_batteries; // Stable sensor ID -> power-supply directory.
    std::optional<systemmetrics::CpuCounters> m_previousCpu;
    // Stable system-wide counters: retain these across topology/recovery rescans.
    QElapsedTimer m_pressureClock;
    std::array<systemmetrics::PressureTracker, 3> m_pressure;
};

} // namespace qsensors
