// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QAbstractTableModel>
#include <QList>
#include <QHash>

namespace qsensors {

class AlarmController;
class SensorPreferencesStore;

// Sensor view model, independent of provider ownership/polling.
class SensorTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum { SortRole = Qt::UserRole };
    explicit SensorTableModel(SensorPreferencesStore *preferences, QObject *parent = nullptr);
    void setAlarmController(AlarmController *alarms);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    void setDescriptors(const QList<SensorDescriptor> &descriptors);
    void applySamples(const QList<SensorSample> &samples, bool incremental = false);

private:
    AlarmController *m_alarms = nullptr;
    SensorPreferencesStore *m_preferences;
    QList<SensorDescriptor> m_descriptors;
    QList<SensorSample> m_samples;
    QHash<QString, int> m_rows;
};

} // namespace qsensors
