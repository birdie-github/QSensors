// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensorprovider.h"

#include <QHash>
#include <sensors/sensors.h>

namespace qsensors {

// Only the sensor worker may create, use or destroy this object.
class LmSensorsProvider final : public SensorProvider {
public:
    ~LmSensorsProvider() override;
    SensorDiscovery discover() override;
    QList<SensorSample> readBatch(const QStringList &ids) override;

private:
    struct Channel {
        const sensors_chip_name *chip;
        int subfeature;
    };
    void cleanup();
    bool m_initialized = false;
    QHash<QString, Channel> m_channels;
};

} // namespace qsensors
