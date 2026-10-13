// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sensors/sensortypes.h"

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace qsensors {

class TraySettingsDialog final : public QDialog {
    Q_OBJECT
public:
    TraySettingsDialog(const QList<SensorDescriptor> &descriptors,
                       const QStringList &selected, QWidget *parent);
    QStringList selectedIds() const;
    void updateCatalog(const QList<SensorDescriptor> &descriptors);

private:
    void populate(const QList<SensorDescriptor> &descriptors, const QStringList &selected);
    QListWidget *m_list;
};

} // namespace qsensors
