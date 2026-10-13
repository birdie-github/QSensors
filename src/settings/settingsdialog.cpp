// SPDX-License-Identifier: GPL-3.0-only
#include "settings/settingsdialog.h"
#include "settings/autostart.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace qsensors {

SettingsDialog::SettingsDialog(SensorPreferencesStore *store, const QList<SensorDescriptor> &catalog,
                               QWidget *parent)
    : QDialog(parent), m_store(store), m_draft(store->values()), m_unit(store->temperatureUnit()),
      m_initialAutostart(Autostart::enabled())
{
    setWindowTitle(tr("QSensors Settings"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(850, 610);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    auto *general = new QWidget(tabs);
    auto *generalForm = new QFormLayout(general);
    m_temperatureUnit = new QComboBox(general);
    m_temperatureUnit->addItem(tr("Celsius (°C)"), 0);
    m_temperatureUnit->addItem(tr("Fahrenheit (°F)"), 1);
    m_temperatureUnit->addItem(tr("Kelvin (K)"), 2);
    m_temperatureUnit->setCurrentIndex(static_cast<int>(m_unit));
    generalForm->addRow(tr("Temperature unit:"), m_temperatureUnit);
    m_autostart = new QCheckBox(tr("Start QSensors when I log in"), general);
    m_autostart->setChecked(m_initialAutostart);
    m_autostart->setToolTip(tr("Starts this executable: %1").arg(QCoreApplication::applicationFilePath()));
    generalForm->addRow(m_autostart);
    auto *note = new QLabel(tr("Update interval and tile size are available on the dashboard toolbar. Sensor corrections are applied after libsensors configuration rules. Temperature multipliers operate on Celsius readings; offsets are temperature differences."), general);
    note->setWordWrap(true);
    generalForm->addRow(note);
    tabs->addTab(general, tr("General"));

    auto *sensors = new QSplitter(tabs);
    m_list = new QListWidget;
    sensors->addWidget(m_list);
    m_list->setMinimumWidth(180);
    auto *scroll = new QScrollArea;
    sensors->addWidget(scroll);
    scroll->setWidgetResizable(true);
    m_editor = new QGroupBox(tr("Sensor"), scroll);
    auto *form = new QFormLayout(m_editor);
    m_identity = new QLabel(m_editor);
    m_identity->setWordWrap(true);
    m_identity->setTextFormat(Qt::PlainText);
    m_identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(m_identity);
    m_dashboard = new QCheckBox(tr("Show on dashboard"), m_editor);
    m_tray = new QCheckBox(tr("Show numeric tray icon"), m_editor);
    form->addRow(m_dashboard);
    form->addRow(m_tray);
    m_label = new QLineEdit(m_editor);
    form->addRow(tr("Label:"), m_label);
    const auto spin = [this](double limit, int decimals) {
        auto *spin = new QDoubleSpinBox(m_editor);
        spin->setRange(-limit, limit);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(false);
        return spin;
    };
    m_multiplier = spin(1e6, 12);
    m_offset = spin(1.8e12, 9); // Fahrenheit differences need 1.8 times the canonical limit.
    form->addRow(tr("Multiplier:"), m_multiplier);
    form->addRow(tr("Offset:"), m_offset);
    auto *identityCalibration = new QPushButton(tr("Reset calibration"), m_editor);
    form->addRow(identityCalibration);
    m_customRange = new QCheckBox(tr("Use custom gauge range"), m_editor);
    m_customRange->setToolTip(tr("Display bounds only; these do not trigger alarms."));
    form->addRow(m_customRange);
    m_lower = spin(1.8e12 + 300, 9);
    m_upper = spin(1.8e12 + 300, 9);
    form->addRow(tr("Gauge minimum:"), m_lower);
    form->addRow(tr("Gauge maximum:"), m_upper);
    m_foreground = new QPushButton(m_editor);
    m_background = new QPushButton(m_editor);
    auto *resetForeground = new QPushButton(tr("Reset"), m_editor);
    auto *resetBackground = new QPushButton(tr("Reset"), m_editor);
    auto *fgLayout = new QHBoxLayout;
    fgLayout->addWidget(m_foreground, 1);
    fgLayout->addWidget(resetForeground);
    auto *bgLayout = new QHBoxLayout;
    bgLayout->addWidget(m_background, 1);
    bgLayout->addWidget(resetBackground);
    form->addRow(tr("Text / tray color:"), fgLayout);
    form->addRow(tr("Tile background:"), bgLayout);
    auto *defaults = new QPushButton(tr("Reset sensor preferences"), m_editor);
    form->addRow(defaults);
    scroll->setWidget(m_editor);
    sensors->setStretchFactor(1, 1);
    tabs->addTab(sensors, tr("Sensors"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const auto *item = m_list->item(row);
        loadSensor(item ? item->data(Qt::UserRole).toString() : QString());
    });
    connect(m_temperatureUnit, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_unit = static_cast<TemperatureUnit>(index);
        loadSensor(m_currentId); // Reload converted controls without rewriting canonical draft values.
    });
    connect(m_label, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_loading && !m_currentId.isEmpty()) {
            m_draft[m_currentId].label = text;
            const auto descriptor = m_catalog.value(m_currentId);
            if (auto *item = m_list->currentItem())
                item->setText(m_catalog.contains(m_currentId)
                    ? tr("%1 — %2").arg(text.isEmpty() ? descriptor.label : text, descriptor.device)
                    : tr("Missing — %1").arg(text.isEmpty() ? m_currentId : text));
        }
    });
    connect(m_dashboard, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].dashboard = checked;
    });
    connect(m_tray, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].tray = checked;
    });
    connect(m_multiplier, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].multiplier = value;
    });
    connect(m_offset, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].offset = fromDisplay(value, true);
    });
    connect(m_customRange, &QCheckBox::toggled, this, [this](bool checked) {
        m_lower->setEnabled(checked);
        m_upper->setEnabled(checked);
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].customRange = checked;
    });
    connect(m_lower, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].rangeLower = fromDisplay(value, false);
    });
    connect(m_upper, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty())
            m_draft[m_currentId].rangeUpper = fromDisplay(value, false);
    });
    connect(m_foreground, &QPushButton::clicked, this, [this] { chooseColor(false); });
    connect(m_background, &QPushButton::clicked, this, [this] { chooseColor(true); });
    connect(resetForeground, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) { m_draft[m_currentId].foreground = QColor(); updateColorButtons(); }
    });
    connect(resetBackground, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) { m_draft[m_currentId].background = QColor(); updateColorButtons(); }
    });
    connect(identityCalibration, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) {
            m_draft[m_currentId].multiplier = 1;
            m_draft[m_currentId].offset = 0;
            loadSensor(m_currentId);
        }
    });
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) { m_draft[m_currentId] = {}; loadSensor(m_currentId); }
    });
    updateCatalog(catalog);
}

void SettingsDialog::updateCatalog(const QList<SensorDescriptor> &catalog)
{
    m_catalog.clear();
    for (const auto &descriptor : catalog)
        m_catalog.insert(descriptor.id, descriptor);
    populate();
}

void SettingsDialog::populate()
{
    const QString previous = m_currentId;
    m_list->clear();
    QStringList ids = m_catalog.keys();
    for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it) {
        if (!m_catalog.contains(it.key()))
            ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end());
    int selected = 0;
    for (const auto &id : ids) {
        if (!m_draft.contains(id))
            m_draft.insert(id, m_store->value(id));
        const auto descriptor = m_catalog.value(id);
        const auto p = m_draft.value(id);
        const QString label = p.label.isEmpty() ? descriptor.label : p.label;
        auto *item = new QListWidgetItem(m_catalog.contains(id) ? tr("%1 — %2").arg(label, descriptor.device)
            : tr("Missing — %1").arg(label.isEmpty() ? id : label), m_list);
        item->setData(Qt::UserRole, id);
        item->setToolTip(id);
        if (id == previous)
            selected = m_list->count() - 1;
    }
    m_list->setCurrentRow(ids.isEmpty() ? -1 : selected);
    if (ids.isEmpty())
        loadSensor({});
}

bool SettingsDialog::currentIsTemperature() const
{
    return m_catalog.contains(m_currentId) && m_catalog.value(m_currentId).kind == SensorKind::Temperature;
}

double SettingsDialog::toDisplay(double value, bool difference) const
{
    if (!currentIsTemperature())
        return value;
    return difference ? temperatureDifferenceToDisplay(value, m_unit) : temperatureToDisplay(value, m_unit);
}

double SettingsDialog::fromDisplay(double value, bool difference) const
{
    if (!currentIsTemperature())
        return value;
    return difference ? temperatureDifferenceFromDisplay(value, m_unit) : temperatureFromDisplay(value, m_unit);
}

void SettingsDialog::loadSensor(const QString &id)
{
    m_loading = true;
    m_currentId = id;
    m_editor->setEnabled(!id.isEmpty());
    const auto descriptor = m_catalog.value(id);
    const auto p = m_draft.value(id);
    m_identity->setText(m_catalog.contains(id) ? descriptor.device
        : tr("Temporarily unavailable. Preferences are retained. Calibration and gauge bounds use canonical units until rediscovery."));
    m_identity->setToolTip(id);
    m_label->setText(p.label);
    m_label->setPlaceholderText(descriptor.label);
    m_dashboard->setChecked(p.dashboard);
    m_tray->setChecked(p.tray);
    m_multiplier->setValue(p.multiplier);
    const QString unit = currentIsTemperature() ? temperatureUnitText(m_unit) : descriptor.unit;
    m_offset->setSuffix(unit.isEmpty() ? QString() : QLatin1Char(' ') + unit);
    m_lower->setSuffix(m_offset->suffix());
    m_upper->setSuffix(m_offset->suffix());
    m_offset->setRange(toDisplay(-1e12, true), toDisplay(1e12, true));
    m_lower->setRange(toDisplay(-1e12, false), toDisplay(1e12, false));
    m_upper->setRange(toDisplay(-1e12, false), toDisplay(1e12, false));
    m_offset->setValue(toDisplay(p.offset, true));
    m_customRange->setChecked(p.customRange);
    m_lower->setValue(toDisplay(p.rangeLower, false));
    m_upper->setValue(toDisplay(p.rangeUpper, false));
    m_lower->setEnabled(p.customRange);
    m_upper->setEnabled(p.customRange);
    updateColorButtons();
    m_loading = false;
}

void SettingsDialog::chooseColor(bool background)
{
    if (m_currentId.isEmpty())
        return;
    const QString id = m_currentId;
    const auto preferences = m_draft.value(id);
    auto *dialog = new QColorDialog(background ? preferences.background : preferences.foreground, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QColorDialog::colorSelected, this, [this, id, background](const QColor &color) {
        if (!color.isValid() || !m_draft.contains(id))
            return;
        if (background)
            m_draft[id].background = color;
        else
            m_draft[id].foreground = color;
        if (id == m_currentId)
            updateColorButtons();
    });
    dialog->open();
}

void SettingsDialog::updateColorButtons()
{
    const auto p = m_draft.value(m_currentId);
    const auto update = [this](QPushButton *button, const QColor &color) {
        button->setText(color.isValid() ? color.name() : tr("System default"));
        button->setStyleSheet(color.isValid() ? QStringLiteral("background-color: %1; color: %2;")
            .arg(color.name(), color.lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white")) : QString());
    };
    update(m_foreground, p.foreground);
    update(m_background, p.background);
}

void SettingsDialog::accept()
{
    for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it) {
        if (it->customRange && it->rangeLower >= it->rangeUpper) {
            QMessageBox::warning(this, tr("Invalid gauge range"), tr("The minimum must be below the maximum for %1.").arg(it.key()));
            return;
        }
    }
    QString error;
    const bool autostartChanged = m_autostart->isChecked() != m_initialAutostart;
    if (autostartChanged && !Autostart::setEnabled(m_autostart->isChecked(), error)) {
        QMessageBox::warning(this, tr("Autostart"), error);
        return;
    }
    if (!m_store->apply(m_draft, m_unit, error)) {
        QString rollbackError;
        if (autostartChanged && !Autostart::setEnabled(m_initialAutostart, rollbackError)) {
            error += QLatin1Char('\n') + rollbackError;
            m_initialAutostart = Autostart::enabled();
        }
        QMessageBox::warning(this, tr("Settings"), error);
        return;
    }
    m_initialAutostart = m_autostart->isChecked();
    QDialog::accept();
}

} // namespace qsensors
