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
#include <QTreeWidget>
#include <QHeaderView>
#include <QCollator>
#include <QLocale>
#include <QScreen>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QHideEvent>

#include <cmath>
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
    const QSize available = screen()->availableGeometry().size();
    resize(QSize(1120, 760).boundedTo(available * 0.9));
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
    auto *browser = new QWidget;
    auto *browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(browser);
    m_search->setPlaceholderText(tr("Search sources or sensors…"));
    m_search->setClearButtonEnabled(true);
    browserLayout->addWidget(m_search);
    m_list = new QTreeWidget(browser);
    m_list->setHeaderLabels({tr("Source / sensor"), tr("Dashboard"), tr("Tray"), tr("Value")});
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setMinimumSectionSize(48);
    m_list->setColumnWidth(0, 240);
    m_list->setColumnWidth(1, 105);
    m_list->setColumnWidth(2, 60);
    m_list->setColumnWidth(3, 130);
    browserLayout->addWidget(m_list);
    auto *disableAll = new QPushButton(tr("Disable all sensors"), browser);
    disableAll->setToolTip(tr("Clear dashboard and tray selections, including sensors hidden by search. Changes take effect on OK."));
    browserLayout->addWidget(disableAll);
    sensors->addWidget(browser);
    browser->setMinimumWidth(340);
    connect(m_search, &QLineEdit::textChanged, this, &SettingsDialog::filterSensors);
    connect(disableAll, &QPushButton::clicked, this, [this] {
        for (auto it = m_draft.begin(); it != m_draft.end(); ++it) {
            it->dashboard = false;
            it->tray = false;
        }
        populate();
    });
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
    form->addRow(tr("Display name:"), m_label);
    m_label->setToolTip(tr("Rename this sensor on the dashboard, in the table and in tray tooltips. Clear to restore the original name."));
    auto *resetName = new QPushButton(tr("Use original name"), m_editor);
    form->addRow(resetName);
    connect(resetName, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) {
            m_draft[m_currentId].label.clear();
            loadSensor(m_currentId);
        }
    });
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
    sensors->setStretchFactor(0, 1);
    sensors->setStretchFactor(1, 1);
    sensors->setSizes({600, 460});
    tabs->addTab(sensors, tr("Sensors"));
    tabs->setCurrentWidget(sensors);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        loadSensor(item ? item->data(0, Qt::UserRole).toString() : QString());
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty() || (column != 1 && column != 2))
            return;
        auto &p = m_draft[id];
        if (column == 1) p.dashboard = item->checkState(1) == Qt::Checked;
        else p.tray = item->checkState(2) == Qt::Checked;
        if (id == m_currentId)
            loadSensor(id);
    });
    connect(m_temperatureUnit, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_unit = static_cast<TemperatureUnit>(index);
        loadSensor(m_currentId); // Reload converted controls without rewriting canonical draft values.
    });
    connect(m_label, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_loading && !m_currentId.isEmpty()) {
            m_draft[m_currentId].label = text;
            syncCurrentItem();
        }
    });
    connect(m_dashboard, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            m_draft[m_currentId].dashboard = checked;
            syncCurrentItem();
        }
    });
    connect(m_tray, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            m_draft[m_currentId].tray = checked;
            syncCurrentItem();
        }
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
    m_samples.clear();
    populate();
    if (isVisible())
        requestPreview();
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_samples.clear();
    m_readError.clear();
    updateValues();
    requestPreview();
}

void SettingsDialog::hideEvent(QHideEvent *event)
{
    emit previewIdsChanged({});
    QDialog::hideEvent(event);
}

void SettingsDialog::requestPreview()
{
    emit previewIdsChanged(m_catalog.keys());
}

void SettingsDialog::applySnapshot(const SensorSnapshot &snapshot)
{
    if (!isVisible())
        return;
    // Values are already calibrated and converted by SensorPresentation.
    // Replacing the batch prevents a missing/failed reading from looking current.
    m_samples.clear();
    for (const auto &sample : snapshot.samples)
        m_samples.insert(sample.id, sample);
    m_readError = snapshot.error;
    updateValues();
}

void SettingsDialog::updateValues()
{
    const QSignalBlocker blocker(m_list);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const auto *group = m_list->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            const QString id = item->data(0, Qt::UserRole).toString();
            const auto sample = m_samples.value(id);
            QString text;
            QString tooltip;
            if (!m_catalog.contains(id)) {
                text = tr("Unavailable");
                tooltip = tr("This sensor is not currently detected.");
            } else if (!m_samples.contains(id) && m_readError.isEmpty()) {
                text = tr("Waiting…");
            } else if (!sample.available || !std::isfinite(sample.value) || !sample.timestamp.isValid()) {
                text = tr("Unavailable");
                tooltip = sample.error.isEmpty() ? m_readError : sample.error;
                if (tooltip.isEmpty()) tooltip = tr("No valid reading returned.");
            } else {
                const auto descriptor = m_catalog.value(id);
                const QString unit = descriptor.kind == SensorKind::Temperature
                    ? temperatureUnitText(m_store->temperatureUnit()) : descriptor.unit;
                text = QLocale().toString(sample.value, 'g', 8);
                if (!unit.isEmpty()) text += QLatin1Char(' ') + unit;
                tooltip = tr("Applied calibration and display units. Read at %1.")
                    .arg(QLocale().toString(sample.timestamp.toLocalTime(), QLocale::ShortFormat));
                if (!sample.detail.isEmpty()) tooltip += QLatin1Char('\n') + sample.detail;
                if (sample.detail == QStringLiteral("No swap configured"))
                    text = tr("No swap configured");
            }
            item->setText(3, text);
            item->setToolTip(3, tooltip);
            item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

void SettingsDialog::populate()
{
    const QString previous = m_currentId;
    QHash<QString, bool> expanded;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const auto *group = m_list->topLevelItem(i);
        expanded.insert(group->data(0, Qt::UserRole + 1).toString(), group->isExpanded());
    }
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    QStringList ids = m_catalog.keys();
    for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it) {
        if (!m_catalog.contains(it.key()))
            ids.append(it.key());
    }
    // Numeric collation also works when the process uses the C locale.
    QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const auto name = [this](const QString &id) {
        const QString custom = m_draft.value(id).label;
        return custom.isEmpty() ? m_catalog.value(id).label : custom;
    };
    std::sort(ids.begin(), ids.end(), [&](const QString &a, const QString &b) {
        const bool presentA = m_catalog.contains(a), presentB = m_catalog.contains(b);
        if (presentA != presentB) return presentA;
        const QString sourceA = m_catalog.value(a).device, sourceB = m_catalog.value(b).device;
        const int source = collator.compare(sourceA, sourceB);
        if (source != 0) return source < 0;
        if (sourceA != sourceB) return sourceA < sourceB;
        const int label = collator.compare(name(a), name(b));
        return label != 0 ? label < 0 : a < b;
    });
    QHash<QString, QTreeWidgetItem *> groups;
    QTreeWidgetItem *selected = nullptr;
    QTreeWidgetItem *first = nullptr;
    for (const auto &id : ids) {
        if (!m_draft.contains(id))
            m_draft.insert(id, m_store->value(id));
        const bool present = m_catalog.contains(id);
        const auto descriptor = m_catalog.value(id);
        const QString source = present ? descriptor.device : QString();
        auto *group = groups.value(source);
        if (!group) {
            group = new QTreeWidgetItem(m_list, {present ? source : tr("Unavailable sensors")});
            group->setData(0, Qt::UserRole + 1, source);
            group->setFlags(Qt::ItemIsEnabled);
            QFont font = group->font(0);
            font.setBold(true);
            group->setFont(0, font);
            group->setExpanded(expanded.value(source, true));
            groups.insert(source, group);
        }
        const auto p = m_draft.value(id);
        const QString label = name(id);
        auto *item = new QTreeWidgetItem(group, {label.isEmpty() ? id : label});
        item->setData(0, Qt::UserRole, id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(1, p.dashboard ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(2, p.tray ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, tr("Original name: %1\nSource: %2\n%3").arg(descriptor.label, source, id));
        if (!first) first = item;
        if (id == previous) selected = item;
    }
    m_list->setCurrentItem(selected ? selected : first);
    const auto *current = m_list->currentItem();
    loadSensor(current ? current->data(0, Qt::UserRole).toString() : QString());
    filterSensors();
    updateValues();
}

void SettingsDialog::filterSensors()
{
    const QString query = m_search->text().trimmed();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *group = m_list->topLevelItem(i);
        bool any = false;
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            const bool match = query.isEmpty() || item->text(0).contains(query, Qt::CaseInsensitive)
                || item->toolTip(0).contains(query, Qt::CaseInsensitive);
            item->setHidden(!match);
            any |= match;
        }
        group->setHidden(!any);
        if (!query.isEmpty() && any) group->setExpanded(true);
    }
}

void SettingsDialog::syncCurrentItem()
{
    auto *item = m_list->currentItem();
    if (!item || m_currentId.isEmpty() || item->data(0, Qt::UserRole).toString() != m_currentId)
        return;
    const QSignalBlocker blocker(m_list);
    const auto p = m_draft.value(m_currentId);
    const QString original = m_catalog.value(m_currentId).label;
    const QString name = p.label.isEmpty() ? original : p.label;
    item->setText(0, name.isEmpty() ? m_currentId : name);
    item->setCheckState(1, p.dashboard ? Qt::Checked : Qt::Unchecked);
    item->setCheckState(2, p.tray ? Qt::Checked : Qt::Unchecked);
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
    m_identity->setText(m_catalog.contains(id) ? tr("Source: %1\nOriginal name: %2").arg(descriptor.device, descriptor.label)
        : tr("Temporarily unavailable. Preferences are retained. Calibration and gauge bounds use canonical units until rediscovery."));
    m_identity->setToolTip(id);
    m_label->setText(p.label.isEmpty() ? descriptor.label : p.label);
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
    syncCurrentItem();
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
