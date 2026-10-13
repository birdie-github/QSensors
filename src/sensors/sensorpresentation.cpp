// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/sensorpresentation.h"
#include "settings/sensorpreferences.h"

#include <QHash>
#include <cmath>

namespace qsensors {

SensorPresentation::SensorPresentation(SensorPreferencesStore *preferences, QObject *parent)
    : QObject(parent), m_preferences(preferences)
{
    connect(preferences, &SensorPreferencesStore::changed, this, &SensorPresentation::refresh);
}

void SensorPresentation::applySnapshot(const SensorSnapshot &snapshot)
{
    if (snapshot.rescanned) {
        m_catalog = snapshot.descriptors;
        m_kinds.clear();
        m_kinds.reserve(m_catalog.size());
        for (const auto &descriptor : m_catalog)
            m_kinds.insert(descriptor.id, descriptor.kind);
        emit catalogChanged(m_catalog);
    }
    m_samples = snapshot.samples;
    m_error = snapshot.error;
    publish(snapshot.rescanned);
}

void SensorPresentation::refresh()
{
    publish(true);
}

void SensorPresentation::publish(bool changedCatalog)
{
    SensorSnapshot display;
    display.rescanned = changedCatalog;
    display.error = m_error;
    if (changedCatalog) {
        display.descriptors.reserve(m_catalog.size());
        for (auto descriptor : m_catalog) {
            const auto p = m_preferences->value(descriptor.id);
            if (!p.label.isEmpty())
                descriptor.label = p.label;
            if (descriptor.kind == SensorKind::Temperature)
                descriptor.unit = temperatureUnitText(m_preferences->temperatureUnit());
            display.descriptors.append(descriptor);
        }
    }
    for (auto sample : m_samples) {
        if (sample.available) {
            const auto p = m_preferences->value(sample.id);
            sample.value = sample.value * p.multiplier + p.offset;
            if (m_kinds.contains(sample.id) && m_kinds.value(sample.id) == SensorKind::Temperature)
                sample.value = temperatureToDisplay(sample.value, m_preferences->temperatureUnit());
            if (!std::isfinite(sample.value) || !sample.timestamp.isValid()) {
                sample.available = false;
                sample.value = std::numeric_limits<double>::quiet_NaN();
                sample.error = tr("Calibration or conversion produced an invalid reading.");
            }
        }
        display.samples.append(sample);
    }
    emit snapshotReady(display);
}

} // namespace qsensors
