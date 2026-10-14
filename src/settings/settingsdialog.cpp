// SPDX-License-Identifier: GPL-3.0-only
#include "settings/settingsdialog.h"
#include "rendering/valueformat.h"
#include "settings/autostart.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QRadioButton>
#include <QSpinBox>
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
#include <functional>
#include <utility>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace qsensors {
namespace {
// QDoubleSpinBox retains three editable decimal places. Compact text is presentation
// only: loading these controls never writes their rounded value back to the draft.
class ConfigurationNumber final : public QDoubleSpinBox {
public:
    explicit ConfigurationNumber(QWidget *parent) : QDoubleSpinBox(parent)
    {
        setDecimals(3);
        setKeyboardTracking(false);
        setSingleStep(.01);
        connect(lineEdit(), &QLineEdit::textEdited, this, [this] { m_textEdited = true; });
        connect(this, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            if (m_editHandler) m_editHandler(value);
        });
        connect(this, &QDoubleSpinBox::editingFinished, this, [this] {
            if (!std::exchange(m_textEdited, false)) return;
            // An explicit edit matching the rounded display still replaces the
            // higher-precision draft. Mere focus/acceptance never does.
            if (m_editHandler) m_editHandler(value());
        });
    }
    void loadValue(double value)
    {
        m_textEdited = false;
        setValue(value);
    }
    void setEditHandler(std::function<void(double)> handler) { m_editHandler = std::move(handler); }
protected:
    QString textFromValue(double value) const override
    {
        const double thousandths = std::round(value * 1000);
        const int digits = std::fmod(std::abs(thousandths), 10.0) < .5 ? 2 : 3;
        return locale().toString(value, 'f', digits);
    }
private:
    bool m_textEdited = false;
    std::function<void(double)> m_editHandler;
};
void loadNumber(QDoubleSpinBox *control, double value)
{
    static_cast<ConfigurationNumber *>(control)->loadValue(value);
    control->setToolTip(QObject::tr("Stored value (unchanged unless edited): %1").arg(control->locale().toString(value, 'g', 17)));
}
void onNumberEdited(QDoubleSpinBox *control, std::function<void(double)> handler)
{
    static_cast<ConfigurationNumber *>(control)->setEditHandler(std::move(handler));
}
}

SettingsDialog::SettingsDialog(SensorPreferencesStore *store, const QList<SensorDescriptor> &catalog,
                               QWidget *parent)
    : QDialog(parent), m_store(store), m_draft(store->values()), m_unit(store->temperatureUnit()),
      m_initialAutostart(Autostart::enabled())
{
    m_loading = true;
    setWindowTitle(tr("QSensors Settings"));
    setAttribute(Qt::WA_DeleteOnClose);
    const QSize available = screen()->availableGeometry().size();
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
    disableAll->setToolTip(tr("Clear dashboard and tray selections, including sensors hidden by search. Changes take effect on Apply or OK."));
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
    auto *editorLayout = new QVBoxLayout(m_editor);
    m_identity = new QLabel(m_editor);
    m_identity->setWordWrap(true);
    m_identity->setTextFormat(Qt::PlainText);
    m_identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editorLayout->addWidget(m_identity);
    m_sensorTabs = new QTabWidget(m_editor);
    editorLayout->addWidget(m_sensorTabs);
    auto *properties = new QWidget(m_sensorTabs);
    auto *form = new QFormLayout(properties);
    m_label = new QLineEdit(properties);
    form->addRow(tr("Display name:"), m_label);
    auto *resetName = new QPushButton(tr("Use original"), properties);
    form->addRow(resetName);
    m_multiplier = new ConfigurationNumber(properties);
    m_multiplier->setRange(-1e6, 1e6);
    m_offset = new ConfigurationNumber(properties);
    form->addRow(tr("Multiplier:"), m_multiplier);
    form->addRow(tr("Offset:"), m_offset);
    auto *identityCalibration = new QPushButton(tr("Reset calibration"), properties);
    form->addRow(identityCalibration);
    auto *defaults = new QPushButton(tr("Reset sensor preferences"), properties);
    form->addRow(defaults);
    m_sensorTabs->addTab(properties, tr("General"));
    m_dashboardControls = createPresentationPage(false);
    m_sensorTabs->addTab(m_dashboardControls.page, tr("Dashboard"));
    m_trayControls = createPresentationPage(true);
    m_sensorTabs->addTab(m_trayControls.page, tr("Systray"));
    scroll->setWidget(m_editor);
    sensors->setStretchFactor(0, 1);
    sensors->setStretchFactor(1, 1);
    tabs->addTab(sensors, tr("Sensors"));
    tabs->setCurrentWidget(sensors);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { applyChanges(); });
    connect(m_list, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
        loadSensor(item ? item->data(0, Qt::UserRole).toString() : QString());
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty() || (column != 1 && column != 2) || (column == 2 && !sensorSupportsTray(id))) return;
        auto &p = m_draft[id];
        if (column == 1) p.dashboard = item->checkState(1) == Qt::Checked;
        else p.tray = item->checkState(2) == Qt::Checked;
        if (id == m_currentId) loadSensor(id);
    });
    connect(m_temperatureUnit, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_unit = static_cast<TemperatureUnit>(index);
        loadSensor(m_currentId);
    });
    connect(m_label, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_loading && !m_currentId.isEmpty()) {
            m_draft[m_currentId].label = text;
            syncCurrentItem();
        }
    });
    connect(resetName, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) { m_draft[m_currentId].label.clear(); loadSensor(m_currentId); }
    });
    connect(identityCalibration, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) {
            m_draft[m_currentId].multiplier = 1;
            m_draft[m_currentId].offset = 0;
            loadSensor(m_currentId);
        }
    });
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) {
            m_draft[m_currentId] = defaultSensorPreferences(m_currentId);
            loadSensor(m_currentId);
        }
    });
    onNumberEdited(m_multiplier, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].multiplier = value;
    });
    onNumberEdited(m_offset, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].offset = fromDisplay(value, true);
    });
    connect(m_customRange, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].customRange = checked;
        updatePresentationControls();
    });
    onNumberEdited(m_lower, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].rangeLower = fromDisplay(value, false);
    });
    onNumberEdited(m_upper, [this](double value) {
        if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].rangeUpper = fromDisplay(value, false);
    });
    updateCatalog(catalog);
    resize(sizeHint().boundedTo(available * .9));
}

SettingsDialog::PresentationControls SettingsDialog::createPresentationPage(bool tray)
{
    PresentationControls c{};
    c.page = new QWidget(m_sensorTabs);
    auto *layout = new QVBoxLayout(c.page);
    c.enabled = new QCheckBox(tr("Enabled"), c.page);
    layout->addWidget(c.enabled);
    c.options = new QWidget(c.page);
    auto *options = new QVBoxLayout(c.options);
    options->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(c.options);
    auto *timing = new QGroupBox(tr("Update interval"), c.options);
    auto *timingLayout = new QVBoxLayout(timing);
    c.global = new QRadioButton(tr("Use global"), timing);
    c.custom = new QRadioButton(tr("Every"), timing);
    c.interval = new ConfigurationNumber(timing);
    c.interval->setRange(minimumRefreshSeconds, maximumRefreshSeconds);
    c.interval->setSuffix(tr(" seconds"));
    timingLayout->addWidget(c.global);
    auto *customRow = new QHBoxLayout;
    customRow->addWidget(c.custom);
    customRow->addWidget(c.interval, 1);
    timingLayout->addLayout(customRow);
    options->addWidget(timing);
    auto *appearance = new QGroupBox(tr("Appearance"), c.options);
    auto *form = new QFormLayout(appearance);
    if (tray) {
        m_trayFont = new QFontComboBox(appearance);
        m_trayFontSize = new QSpinBox(appearance);
        m_trayFontSize->setRange(1, 72);
        m_trayFontSize->setSuffix(tr(" pt"));
        m_trayFontSize->setToolTip(tr("Reference size at a 24-pixel tray icon; scaled for the host and reduced as needed to fit the complete value. Reset restores automatic sizing."));
        auto *reset = new QPushButton(tr("Reset"), appearance);
        auto *fontRow = new QHBoxLayout;
        fontRow->addWidget(m_trayFontSize, 1);
        fontRow->addWidget(reset);
        form->addRow(tr("Font:"), m_trayFont);
        form->addRow(tr("Font size:"), fontRow);
        connect(m_trayFont, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
            if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].trayFontFamily = font.family();
        });
        connect(m_trayFontSize, &QSpinBox::valueChanged, this, [this](int size) {
            if (!m_loading && !m_currentId.isEmpty()) {
                m_draft[m_currentId].trayFontSize = size;
                m_draft[m_currentId].trayFontAutoFit = false;
            }
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            if (!m_currentId.isEmpty()) {
                m_draft[m_currentId].trayFontFamily.clear();
                m_draft[m_currentId].trayFontSize = 10;
                m_draft[m_currentId].trayFontAutoFit = true;
                loadSensor(m_currentId);
            }
        });
    }
    c.foreground = new QPushButton(appearance);
    auto *resetColor = new QPushButton(tr("Reset"), appearance);
    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(c.foreground, 1);
    colorRow->addWidget(resetColor);
    form->addRow(tr("Color:"), colorRow);
    c.shadow = new QCheckBox(tr("Cast shadow"), appearance);
    form->addRow(c.shadow);
    auto *background = new QGroupBox(tr("Background"), appearance);
    auto *backgroundLayout = new QVBoxLayout(background);
    c.solid = new QRadioButton(tr("Color"), background);
    c.transparent = new QRadioButton(tr("Transparent"), background);
    c.background = new QPushButton(background);
    auto *resetBackground = new QPushButton(tr("Reset"), background);
    auto *solidRow = new QHBoxLayout;
    solidRow->addWidget(c.solid);
    solidRow->addWidget(c.background, 1);
    solidRow->addWidget(resetBackground);
    backgroundLayout->addLayout(solidRow);
    backgroundLayout->addWidget(c.transparent);
    form->addRow(background);
    options->addWidget(appearance);
    if (!tray) {
        m_memoryDisplay = new QComboBox(c.options);
        m_memoryDisplay->addItem(tr("MiB (automatic MiB/GiB/TiB/PiB)"), false);
        m_memoryDisplay->addItem(tr("Percentage of total RAM"), true);
        m_memoryDisplay->setToolTip(tr("Available RAM value format, also used by the tray and sensor table."));
        options->addWidget(m_memoryDisplay);
        connect(m_memoryDisplay, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].memoryAsPercent = index == 1;
        });
        auto *textStyle = new QGroupBox(tr("Text style"), c.options);
        auto *styles = new QVBoxLayout(textStyle);
        m_normalText = new QRadioButton(tr("Normal"), textStyle);
        m_digitalText = new QRadioButton(tr("Digital"), textStyle);
        styles->addWidget(m_normalText);
        styles->addWidget(m_digitalText);
        options->addWidget(textStyle);
        connect(m_digitalText, &QRadioButton::toggled, this, [this](bool checked) {
            if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].dashboardTextStyle =
                checked ? DashboardTextStyle::Digital : DashboardTextStyle::Normal;
        });
        m_gauge = new QCheckBox(tr("Show gauge"), c.options);
        options->addWidget(m_gauge);
        connect(m_gauge, &QCheckBox::toggled, this, [this](bool checked) {
            if (!m_loading && !m_currentId.isEmpty()) m_draft[m_currentId].dashboardGauge = checked;
            updatePresentationControls();
        });
        m_gaugeRange = new QGroupBox(tr("Gauge range"), c.options);
        auto *range = new QFormLayout(m_gaugeRange);
        m_customRange = new QCheckBox(tr("Use custom gauge range"), m_gaugeRange);
        m_customRange->setToolTip(tr("Display bounds only; these do not trigger alarms."));
        m_lower = new ConfigurationNumber(m_gaugeRange);
        m_upper = new ConfigurationNumber(m_gaugeRange);
        range->addRow(m_customRange);
        range->addRow(tr("Gauge minimum:"), m_lower);
        range->addRow(tr("Gauge maximum:"), m_upper);
        options->addWidget(m_gaugeRange);
    }
    options->addStretch();
    connect(c.enabled, &QCheckBox::toggled, this, [this, tray](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.tray : p.dashboard) = checked;
            syncCurrentItem();
        }
        updatePresentationControls();
    });
    connect(c.global, &QRadioButton::toggled, this, [this, tray](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayUseGlobal : p.dashboardUseGlobal) = checked;
        }
        updatePresentationControls();
    });
    onNumberEdited(c.interval, [this, tray](double value) {
        if (!m_loading && !m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayInterval : p.dashboardInterval) = value;
        }
    });
    connect(c.foreground, &QPushButton::clicked, this, [this, tray] { chooseColor(false, tray); });
    connect(c.background, &QPushButton::clicked, this, [this, tray] { chooseColor(true, tray); });
    connect(resetColor, &QPushButton::clicked, this, [this, tray] {
        if (!m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayForeground : p.foreground) = QColor();
            updateColorButtons();
        }
    });
    connect(resetBackground, &QPushButton::clicked, this, [this, tray] {
        if (!m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayBackground : p.background) = QColor();
            updateColorButtons();
        }
    });
    connect(c.shadow, &QCheckBox::toggled, this, [this, tray](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayShadow : p.dashboardShadow) = checked;
        }
    });
    connect(c.transparent, &QRadioButton::toggled, this, [this, tray](bool checked) {
        if (!m_loading && !m_currentId.isEmpty()) {
            auto &p = m_draft[m_currentId];
            (tray ? p.trayTransparent : p.dashboardTransparent) = checked;
        }
        updatePresentationControls();
    });
    return c;
}

void SettingsDialog::loadPresentation(const PresentationControls &c, bool tray)
{
    const auto p = m_draft.value(m_currentId);
    c.enabled->setChecked(tray ? p.tray && sensorSupportsTray(m_currentId) : p.dashboard);
    const bool global = tray ? p.trayUseGlobal : p.dashboardUseGlobal;
    c.global->setChecked(global);
    c.custom->setChecked(!global);
    loadNumber(c.interval, tray ? p.trayInterval : p.dashboardInterval);
    c.shadow->setChecked(tray ? p.trayShadow : p.dashboardShadow);
    const bool transparent = tray ? p.trayTransparent : p.dashboardTransparent;
    c.transparent->setChecked(transparent);
    c.solid->setChecked(!transparent);
}

void SettingsDialog::updatePresentationControls()
{
    // Page construction connects signals before both pages are fully installed.
    if (m_loading) return;
    for (const auto &c : {m_dashboardControls, m_trayControls}) {
        c.options->setEnabled(c.enabled->isChecked());
        c.interval->setEnabled(c.custom->isChecked());
        c.background->setEnabled(c.solid->isChecked());
    }
    const bool gaugeCapable = sensorSupportsGauge(m_currentId, m_catalog.value(m_currentId).kind);
    m_gauge->setVisible(gaugeCapable);
    m_gaugeRange->setVisible(gaugeCapable && m_gauge->isChecked());
    m_lower->setEnabled(m_customRange->isChecked());
    m_upper->setEnabled(m_customRange->isChecked());
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

void SettingsDialog::selectSensor(const QString &id)
{
    if (auto *tabs = findChild<QTabWidget *>()) tabs->setCurrentIndex(1);
    m_search->clear();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *group = m_list->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *item = group->child(j);
            if (item->data(0, Qt::UserRole).toString() == id) {
                group->setExpanded(true);
                m_list->setCurrentItem(item);
                m_list->scrollToItem(item);
                return;
            }
        }
    }
}
void SettingsDialog::disableDashboardSensor(const QString &id)
{
    // Preserve unrelated unapplied edits, but do not resurrect an externally deleted tile on OK.
    if (m_draft.contains(id)) m_draft[id].dashboard = false;
    populate();
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
    // This is a shared cache snapshot; a failed acquisition replaces its channel.
    // Other channels retain the sample from their own acquisition cadence.
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
                auto descriptor = m_catalog.value(id);
                if (descriptor.kind == SensorKind::Temperature) descriptor.unit = temperatureUnitText(m_store->temperatureUnit());
                if (descriptor.kind == SensorKind::Memory && m_store->value(id).memoryAsPercent) {
                    descriptor.kind = SensorKind::Percentage;
                    descriptor.unit = QStringLiteral("%");
                }
                const auto formatted = formatSensorValue(descriptor, sample.value);
                text = formatted.number;
                if (!formatted.unit.isEmpty()) text += QLatin1Char(' ') + formatted.unit;
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
        if (sensorSupportsTray(id)) item->setCheckState(2, p.tray ? Qt::Checked : Qt::Unchecked);
        else { item->setText(2, tr("—")); item->setToolTip(2, tr("Dashboard only")); }
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
    if (sensorSupportsTray(m_currentId)) item->setCheckState(2, p.tray ? Qt::Checked : Qt::Unchecked);
    else item->setData(2, Qt::CheckStateRole, QVariant());
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
    m_memoryDisplay->setCurrentIndex(p.memoryAsPercent ? 1 : 0);
    const bool memory = id == QStringLiteral("system/memory/available");
    m_memoryDisplay->setVisible(memory);
    const int trayIndex = m_sensorTabs->indexOf(m_trayControls.page);
    if (!sensorSupportsTray(id) && trayIndex >= 0) m_sensorTabs->removeTab(trayIndex);
    else if (sensorSupportsTray(id) && trayIndex < 0) m_sensorTabs->addTab(m_trayControls.page, tr("Systray"));
    loadPresentation(m_dashboardControls, false);
    loadPresentation(m_trayControls, true);
    m_gauge->setChecked(p.dashboardGauge && sensorSupportsGauge(id, descriptor.kind));
    m_normalText->setChecked(p.dashboardTextStyle == DashboardTextStyle::Normal);
    m_digitalText->setChecked(p.dashboardTextStyle == DashboardTextStyle::Digital);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!p.trayFontFamily.isEmpty()) font.setFamily(p.trayFontFamily);
    m_trayFont->setCurrentFont(font);
    m_trayFontSize->setValue(p.trayFontSize);
    loadNumber(m_multiplier, p.multiplier);
    const QString unit = currentIsTemperature() ? temperatureUnitText(m_unit) : descriptor.unit;
    m_offset->setSuffix(unit.isEmpty() ? QString() : QLatin1Char(' ') + unit);
    m_lower->setSuffix(m_offset->suffix());
    m_upper->setSuffix(m_offset->suffix());
    m_offset->setRange(toDisplay(-1e12, true), toDisplay(1e12, true));
    m_lower->setRange(toDisplay(-1e12, false), toDisplay(1e12, false));
    m_upper->setRange(toDisplay(-1e12, false), toDisplay(1e12, false));
    loadNumber(m_offset, toDisplay(p.offset, true));
    m_customRange->setChecked(p.customRange);
    loadNumber(m_lower, toDisplay(p.rangeLower, false));
    loadNumber(m_upper, toDisplay(p.rangeUpper, false));
    m_lower->setEnabled(p.customRange);
    m_upper->setEnabled(p.customRange);
    updateColorButtons();
    syncCurrentItem();
    m_loading = false;
    updatePresentationControls();
}

void SettingsDialog::chooseColor(bool background, bool tray)
{
    if (m_currentId.isEmpty())
        return;
    const QString id = m_currentId;
    const auto preferences = m_draft.value(id);
    const QColor initial = tray ? (background ? preferences.trayBackground : preferences.trayForeground)
        : (background ? preferences.background : preferences.foreground);
    auto *dialog = new QColorDialog(initial, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QColorDialog::colorSelected, this, [this, id, background, tray](const QColor &color) {
        if (!color.isValid() || !m_draft.contains(id))
            return;
        auto &p = m_draft[id];
        if (tray) (background ? p.trayBackground : p.trayForeground) = color;
        else (background ? p.background : p.foreground) = color;
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
    update(m_dashboardControls.foreground, p.foreground);
    if (m_currentId == QStringLiteral("system/uptime") && !p.foreground.isValid()) {
        m_dashboardControls.foreground->setText(tr("Default (red)"));
        m_dashboardControls.foreground->setStyleSheet(QStringLiteral("background-color: #ff0000; color: white;"));
    }
    update(m_dashboardControls.background, p.background);
    update(m_trayControls.foreground, p.trayForeground);
    update(m_trayControls.background, p.trayBackground);
}

bool SettingsDialog::applyChanges()
{
    for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it) {
        if (it->customRange && it->rangeLower >= it->rangeUpper) {
            QMessageBox::warning(this, tr("Invalid gauge range"), tr("The minimum must be below the maximum for %1.").arg(it.key()));
            return false;
        }
    }
    QString error;
    const bool autostartChanged = m_autostart->isChecked() != m_initialAutostart;
    if (autostartChanged && !Autostart::setEnabled(m_autostart->isChecked(), error)) {
        QMessageBox::warning(this, tr("Autostart"), error);
        return false;
    }
    if (!m_store->apply(m_draft, m_unit, error)) {
        QString rollbackError;
        if (autostartChanged && !Autostart::setEnabled(m_initialAutostart, rollbackError)) {
            error += QLatin1Char('\n') + rollbackError;
            m_initialAutostart = Autostart::enabled();
        }
        QMessageBox::warning(this, tr("Settings"), error);
        return false;
    }
    m_initialAutostart = m_autostart->isChecked();
    // Rebase the draft after Apply, including positions assigned by the dashboard.
    m_draft = m_store->values();
    populate();
    return true;
}

void SettingsDialog::accept()
{
    if (applyChanges()) QDialog::accept();
}

} // namespace qsensors
