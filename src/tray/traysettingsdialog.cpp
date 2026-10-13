// SPDX-License-Identifier: GPL-3.0-only
#include "tray/traysettingsdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QVBoxLayout>

namespace qsensors {

TraySettingsDialog::TraySettingsDialog(const QList<SensorDescriptor> &descriptors,
                                     const QStringList &selected, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("QSensors Settings"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 480);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("Select sensors to show as numeric tray icons. Selected sensors keep updating while the dashboard is hidden."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    m_list = new QListWidget(this);
    layout->addWidget(m_list);
    populate(descriptors, selected);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList TraySettingsDialog::selectedIds() const
{
    QStringList ids;
    for (int i = 0; i < m_list->count(); ++i) {
        const auto *item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}

void TraySettingsDialog::updateCatalog(const QList<SensorDescriptor> &descriptors)
{
    populate(descriptors, selectedIds()); // Preserve unapplied edits during rescan.
}

void TraySettingsDialog::populate(const QList<SensorDescriptor> &descriptors, const QStringList &selected)
{
    m_list->clear();
    QSet<QString> present;
    for (const auto &descriptor : descriptors) {
        present.insert(descriptor.id);
        auto *item = new QListWidgetItem(tr("%1 — %2 (%3)").arg(descriptor.label, descriptor.device, descriptor.unit), m_list);
        item->setData(Qt::UserRole, descriptor.id);
        item->setToolTip(descriptor.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(descriptor.id) ? Qt::Checked : Qt::Unchecked);
    }
    for (const auto &id : selected) {
        if (present.contains(id))
            continue;
        auto *item = new QListWidgetItem(tr("Unavailable sensor — %1").arg(id), m_list);
        item->setData(Qt::UserRole, id);
        item->setToolTip(tr("The selection is preserved until you uncheck it."));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
}

} // namespace qsensors
