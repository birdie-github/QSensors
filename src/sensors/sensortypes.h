// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDateTime>
#include <QString>
#include <QList>
#include <QMetaType>

#include <limits>

namespace qsensors {

enum class SensorKind { Temperature, Fan, Voltage, Current, Power, Energy, Humidity };

// Copied metadata; canonical units are C, RPM, V, A, W, J and % respectively.
// IDs belong to the provider and must be unique and independent of labels.
struct SensorDescriptor {
    QString id;
    QString label;
    SensorKind kind = SensorKind::Temperature;
    QString unit;
    QString device;
};

// A failed reading never represents zero or a previously valid value.
struct SensorSample {
    QString id;
    double value = std::numeric_limits<double>::quiet_NaN();
    QDateTime timestamp;
    bool available = false;
    QString error;
};

struct SensorDiscovery {
    QList<SensorDescriptor> descriptors;
    QString error;
};

struct SensorSnapshot {
    bool rescanned = false;
    QList<SensorDescriptor> descriptors;
    QList<SensorSample> samples;
    QString error;
};

} // namespace qsensors

Q_DECLARE_METATYPE(qsensors::SensorSnapshot)
