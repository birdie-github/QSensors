// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensorprovider.h"
#include "system/systemmetrics.h"
#include <QElapsedTimer>
#include <QHash>
#include <array>
#include <optional>

namespace qsensors {

// Serialized worker only. One stat read per requested disk, independent baselines
// for its three channels so a faster channel cannot consume another's interval.
class DiskSensorProvider final : public SensorProvider {
public:
    SensorDiscovery discover() override;
    QList<SensorSample> readBatch(const QStringList &ids) override;
private:
    struct Baseline { systemmetrics::DiskCounters counters; qint64 nanoseconds; };
    struct Disk {
        QString path;
        QByteArray sequence;
        std::array<std::optional<Baseline>, 3> previous;
    };
    struct Channel { QString diskId; int metric; }; // Read bytes, written bytes, busy %.
    QHash<QString, Disk> m_disks;
    QHash<QString, Channel> m_channels;
    QElapsedTimer m_clock;
};

} // namespace qsensors
