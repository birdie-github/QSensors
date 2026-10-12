// SPDX-License-Identifier: GPL-3.0-only
#include "ui/sensortablemodel.h"

#include <QHash>
#include <QLocale>

#include <cmath>

namespace qsensors {

SensorTableModel::SensorTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int SensorTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_descriptors.size());
}

int SensorTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 4;
}

QVariant SensorTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= columnCount())
        return {};
    const auto &descriptor = m_descriptors[index.row()];
    const auto &sample = m_samples[index.row()];
    if (role == Qt::ToolTipRole) {
        if (index.column() <= 1)
            return descriptor.id;
        if (!sample.available)
            return sample.error;
        return {};
    }
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case 0: return descriptor.label;
    case 1: return descriptor.device;
    case 2:
        if (!sample.available)
            return tr("Unavailable");
        return QLocale().toString(sample.value, 'g', 8);
    case 3: return descriptor.unit;
    }
    return {};
}

QVariant SensorTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case 0: return tr("Sensor");
    case 1: return tr("Device");
    case 2: return tr("Value");
    case 3: return tr("Unit");
    }
    return {};
}

void SensorTableModel::setDescriptors(const QList<SensorDescriptor> &descriptors)
{
    beginResetModel();
    m_descriptors = descriptors;
    m_samples.clear();
    m_samples.reserve(descriptors.size());
    for (const auto &descriptor : descriptors) {
        SensorSample sample;
        sample.id = descriptor.id;
        sample.error = tr("Not read yet");
        m_samples.append(sample);
    }
    endResetModel();
}

void SensorTableModel::applySamples(const QList<SensorSample> &samples)
{
    QHash<QString, SensorSample> batch;
    batch.reserve(samples.size());
    for (const auto &sample : samples)
        batch.insert(sample.id, sample);
    for (int row = 0; row < rowCount(); ++row) {
        const QString &id = m_descriptors[row].id;
        auto sample = batch.value(id);
        sample.id = id;
        if (sample.available && (!std::isfinite(sample.value) || !sample.timestamp.isValid())) {
            sample.available = false;
            sample.error = tr("Invalid reading");
        }
        if (!sample.available) {
            sample.value = std::numeric_limits<double>::quiet_NaN();
            if (sample.error.isEmpty())
                sample.error = tr("No reading returned");
        }
        const auto &previous = m_samples[row];
        const bool changed = sample.available != previous.available
            || sample.error != previous.error
            || (sample.available && sample.value != previous.value);
        m_samples[row] = sample;
        if (changed)
            emit dataChanged(index(row, 2), index(row, 3), {Qt::DisplayRole, Qt::ToolTipRole});
    }
}

} // namespace qsensors
