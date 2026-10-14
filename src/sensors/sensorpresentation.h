// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"
#include <QObject>
#include <QHash>

namespace qsensors {
class SensorPreferencesStore;

// Retains raw canonical data and publishes copies for display. Reapplying
// settings never calibrates/converts an already-converted GUI value.
class SensorPresentation final : public QObject {
    Q_OBJECT
public:
    explicit SensorPresentation(SensorPreferencesStore *preferences, QObject *parent = nullptr);
    void applySnapshot(const SensorSnapshot &snapshot);
    void refresh();

signals:
    void snapshotReady(const qsensors::SensorSnapshot &snapshot);
    void catalogChanged(const QList<qsensors::SensorDescriptor> &descriptors);

private:
    SensorSample convert(SensorSample sample) const;
    void rebuildDisplayCache();
    void publish(const SensorSnapshot &snapshot, bool catalogChanged);
    SensorPreferencesStore *m_preferences;
    QList<SensorDescriptor> m_catalog;
    QHash<QString, SensorKind> m_kinds;
    QHash<QString, SensorSample> m_samples;
    QHash<QString, SensorSample> m_displaySamples;
    QString m_error;
};

} // namespace qsensors
