// SPDX-License-Identifier: GPL-3.0-only
#include "ui/dashboardcolorsdialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>

namespace qsensors {
std::optional<DashboardAppearance> editDashboardColors(const DashboardAppearance &initial, QWidget *parent)
{
    DashboardAppearance draft = initial;
    QDialog dialog(parent);
    dialog.setWindowTitle(QDialog::tr("Custom Dashboard Colors"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QDialog::tr("Start from the current palette. Per-sensor color overrides take precedence. Gauge bands are visual guides, not alarm thresholds."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto *fields = new QWidget(scroll);
    auto *form = new QFormLayout(fields);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(fields);
    layout->addWidget(scroll);
    const auto addColor = [&](const QString &label, const QColor &initialColor,
                              std::function<void(const QColor &)> update, bool alpha = false) {
        auto *button = new QPushButton(fields);
        const auto showColor = [button, alpha](const QColor &color) {
            button->setText(color.name(alpha ? QColor::HexArgb : QColor::HexRgb));
            button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                .arg(color.name(), color.lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white")));
        };
        showColor(initialColor);
        form->addRow(label, button);
        QObject::connect(button, &QPushButton::clicked, &dialog,
                         [&, showColor, update, alpha, color = initialColor]() mutable {
            QColorDialog::ColorDialogOptions options;
            if (alpha) options |= QColorDialog::ShowAlphaChannel;
            const QColor chosen = QColorDialog::getColor(color, &dialog, QDialog::tr("Choose Color"), options);
            if (!chosen.isValid()) return;
            color = chosen;
            showColor(chosen);
            update(chosen);
        });
    };
    const auto addRole = [&](const QString &label, QPalette::ColorRole role) {
        addColor(label, draft.colors.color(role), [&, role](const QColor &color) {
            // Editing normal text must not overwrite the separate unavailable color.
            if (role == QPalette::Text) {
                draft.colors.setColor(QPalette::Active, role, color);
                draft.colors.setColor(QPalette::Inactive, role, color);
            } else draft.colors.setColor(role, color);
        });
    };
    addRole(QDialog::tr("Canvas:"), QPalette::Window);
    addRole(QDialog::tr("Tile background:"), QPalette::Base);
    addRole(QDialog::tr("Text and labels:"), QPalette::Text);
    addColor(QDialog::tr("Unavailable text:"), draft.colors.color(QPalette::Disabled, QPalette::Text),
             [&](const QColor &color) { draft.colors.setColor(QPalette::Disabled, QPalette::Text, color); });
    addRole(QDialog::tr("Tile border:"), QPalette::Mid);
    addRole(QDialog::tr("Selected border:"), QPalette::Highlight);
    const auto addField = [&](const QString &label, QColor &field, bool alpha = false) {
        auto *target = &field;
        addColor(label, field, [target](const QColor &color) { *target = color; }, alpha);
    };
    addField(QDialog::tr("Background pattern:"), draft.pattern, true);
    addField(QDialog::tr("Gauge track:"), draft.gaugeTrack);
    addField(QDialog::tr("Gauge needle:"), draft.needle);
    addField(QDialog::tr("Uptime digits:"), draft.uptime);
    auto *bands = new QCheckBox(QDialog::tr("Colored gauge bands"), fields);
    bands->setChecked(draft.coloredGauges);
    form->addRow(bands);
    QObject::connect(bands, &QCheckBox::toggled, &dialog, [&](bool enabled) { draft.coloredGauges = enabled; });
    addField(QDialog::tr("Gauge main band:"), draft.gaugeNormal);
    addField(QDialog::tr("Gauge transition band:"), draft.gaugeTransition);
    addField(QDialog::tr("Gauge end band:"), draft.gaugeEnd);
    addField(QDialog::tr("Voltage main band:"), draft.voltageNormal);
    addField(QDialog::tr("Voltage transition band:"), draft.voltageTransition);
    addField(QDialog::tr("Voltage end bands:"), draft.voltageEnd);
    addField(QDialog::tr("Voltage needle:"), draft.voltageNeedle);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(QSize(480, 620).boundedTo(dialog.screen()->availableGeometry().size()));
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return draft;
}
} // namespace qsensors
