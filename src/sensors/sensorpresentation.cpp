// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/sensorpresentation.h"
#include "settings/sensorpreferences.h"

#include <QSet>
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
        for (auto it = m_samples.begin(); it != m_samples.end();) {
            if (!m_kinds.contains(it.key())) it = m_samples.erase(it);
            else ++it;
        }
        emit catalogChanged(m_catalog);
    }
    for (const auto &sample : snapshot.samples) {
        m_samples.insert(sample.id, sample);
        if (!snapshot.rescanned) m_displaySamples.insert(sample.id, convert(sample));
    }
    if (snapshot.rescanned) rebuildDisplayCache();
    m_error = snapshot.error;
    publish(snapshot, snapshot.rescanned);
}

void SensorPresentation::refresh()
{
    rebuildDisplayCache();
    publish(SensorSnapshot{}, true);
}

void SensorPresentation::rebuildDisplayCache()
{
    m_displaySamples.clear();
    m_displaySamples.reserve(m_samples.size());
    for (const auto &sample : m_samples) m_displaySamples.insert(sample.id, convert(sample));
}

void SensorPresentation::publish(const SensorSnapshot &snapshot, bool changedCatalog)
{
    SensorSnapshot display;
    display.rescanned = changedCatalog;
    display.incremental = snapshot.incremental && !changedCatalog;
    display.dashboardRefreshIds = snapshot.dashboardRefreshIds;
    display.trayRefreshIds = snapshot.trayRefreshIds;
    display.error = m_error;
    if (changedCatalog) {
        display.descriptors.reserve(m_catalog.size());
        for (auto descriptor : m_catalog) {
            const auto p = m_preferences->value(descriptor.id);
            if (!p.label.isEmpty())
                descriptor.label = p.label;
            if (descriptor.kind == SensorKind::Temperature)
                descriptor.unit = temperatureUnitText(m_preferences->temperatureUnit());
            if (descriptor.kind == SensorKind::Memory && p.memoryAsPercent) {
                descriptor.kind = SensorKind::Percentage;
                descriptor.unit = QStringLiteral("%");
            }
            display.descriptors.append(descriptor);
        }
    }
    if (changedCatalog || !snapshot.incremental) {
        display.samples.reserve(m_displaySamples.size());
        for (const auto &sample : m_displaySamples) display.samples.append(sample);
    } else {
        // New acquisitions feed previews; due presentations may reuse an older
        // converted value without recalibrating or publishing unrelated channels.
        QSet<QString> ids;
        ids.reserve(snapshot.samples.size() + snapshot.dashboardRefreshIds.size() + snapshot.trayRefreshIds.size());
        for (const auto &sample : snapshot.samples) ids.insert(sample.id);
        for (const auto &id : snapshot.dashboardRefreshIds) ids.insert(id);
        for (const auto &id : snapshot.trayRefreshIds) ids.insert(id);
        display.samples.reserve(ids.size());
        for (const auto &id : ids) {
            const auto it = m_displaySamples.constFind(id);
            if (it != m_displaySamples.cend()) display.samples.append(it.value());
        }
    }
    emit snapshotReady(display);
}

SensorSample SensorPresentation::convert(SensorSample sample) const
{
    if (sample.available) {
        const auto p = m_preferences->value(sample.id);
        sample.value = sample.value * p.multiplier + p.offset;
        if (std::isfinite(sample.maximum)) sample.maximum = sample.maximum * p.multiplier + p.offset;
        if (m_kinds.contains(sample.id) && m_kinds.value(sample.id) == SensorKind::Temperature)
            sample.value = temperatureToDisplay(sample.value, m_preferences->temperatureUnit());
        if (m_kinds.value(sample.id) == SensorKind::Memory && p.memoryAsPercent) {
            if (std::isfinite(sample.reference) && sample.reference > 0)
                sample.value = sample.value / sample.reference * 100;
            else sample.value = std::numeric_limits<double>::quiet_NaN();
        }
        if (!std::isfinite(sample.value) || !sample.timestamp.isValid()) {
            sample.available = false;
            sample.value = std::numeric_limits<double>::quiet_NaN();
            sample.error = tr("Calibration or conversion produced an invalid reading.");
        }
    }
    return sample;
}

} // namespace qsensors
