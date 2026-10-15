// SPDX-License-Identifier: GPL-3.0-only
#include "settings/alarmeditor.h"
#include "settings/numberformat.h"
#include "ui/decimalspinbox.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace qsensors {
AlarmEditor::AlarmEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Limits apply to calibrated readings. Alarms keep monitoring hidden sensors at the global interval. Notifications and commands run once per alarm episode."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *limits = new QFormLayout;
    layout->addLayout(limits);
    const auto flag = [this](const QString &text, bool AlarmSettings::*field, QWidget *parent) {
        auto *control = new QCheckBox(text, parent);
        m_flags.append({control, field});
        connect(control, &QCheckBox::toggled, this, [this, field](bool checked) {
            if (m_loading) return;
            m_value.*field = checked;
            updateControls();
            emit edited(m_value);
        });
        return control;
    };
    const auto number = [this](double AlarmSettings::*field, bool difference) {
        auto *control = new DecimalSpinBox(this);
        control->setSingleStep(.1);
        const auto storeValue = [this, field, difference](double value) {
            if (m_loading) return;
            m_value.*field = !m_temperature ? value : difference
                ? temperatureDifferenceFromDisplay(value, m_unit) : temperatureFromDisplay(value, m_unit);
            emit edited(m_value);
        };
        connect(control, &DecimalSpinBox::valueEdited, this, storeValue);
        return control;
    };
    m_lowerEnabled = flag(tr("Below:"), &AlarmSettings::lowerEnabled, this);
    m_upperEnabled = flag(tr("Above:"), &AlarmSettings::upperEnabled, this);
    m_lower = number(&AlarmSettings::lower, false);
    m_upper = number(&AlarmSettings::upper, false);
    m_hysteresis = number(&AlarmSettings::hysteresis, true);
    m_hysteresis->setToolTip(tr("Recovery margin inside the limit. With two limits, it cannot exceed half their separation. Zero clears the alarm at the limit."));
    limits->addRow(m_lowerEnabled, m_lower);
    limits->addRow(m_upperEnabled, m_upper);
    limits->addRow(tr("Hysteresis:"), m_hysteresis);
    const auto color = [this](QColor AlarmSettings::*field) {
        auto *button = new QPushButton(this);
        m_colors.append({button, field});
        connect(button, &QPushButton::clicked, this, [this, field] {
            const int generation = m_generation;
            const QColor chosen = QColorDialog::getColor(m_value.*field, this, tr("Alarm color"));
            if (!chosen.isValid() || generation != m_generation) return;
            m_value.*field = chosen;
            updateControls();
            emit edited(m_value);
        });
        return button;
    };
    auto *actions = new QFormLayout;
    layout->addLayout(actions);
    actions->addRow(flag(tr("Change tile background"), &AlarmSettings::dashboard, this), color(&AlarmSettings::dashboardColor));
    actions->addRow(flag(tr("Change table text color"), &AlarmSettings::table, this), color(&AlarmSettings::tableColor));
    actions->addRow(flag(tr("Show a notification"), &AlarmSettings::notify, this));
    actions->addRow(flag(tr("Run a command"), &AlarmSettings::command, this));
    m_program = new QLineEdit(this);
    m_program->setPlaceholderText(tr("Executable, e.g. paplay"));
    m_arguments = new QLineEdit(this);
    m_arguments->setPlaceholderText(tr("Arguments, e.g. \"/path/to/sound file.wav\""));
    m_arguments->setToolTip(tr("Separate arguments with spaces; use double quotes for spaces within an argument (three quotes for a literal quote). No shell expansion. Child environment: SENSOR_ID, SENSOR_NAME, SENSOR_VALUE, SENSOR_UNIT, SENSOR_TEST (1 for tests). An unavailable test value is empty. Only one command per sensor may run at a time."));
    actions->addRow(tr("Executable:"), m_program);
    actions->addRow(tr("Arguments:"), m_arguments);
    connect(m_program, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_loading) { m_value.program = text; emit edited(m_value); }
    });
    connect(m_arguments, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_loading) { m_value.arguments = text; emit edited(m_value); }
    });
    m_tray = new QGroupBox(tr("System tray alarm"), this);
    auto *tray = new QFormLayout(m_tray);
    tray->addRow(flag(tr("Show horizontal lines above and below the value"), &AlarmSettings::trayMarkers, m_tray));
    tray->addRow(flag(tr("Blink: 1 second visible, 0.75 seconds hidden"), &AlarmSettings::trayBlink, m_tray));
    tray->addRow(flag(tr("Change tray text color"), &AlarmSettings::trayColor, m_tray), color(&AlarmSettings::trayTextColor));
    auto *trayHint = new QLabel(tr("Tray actions affect the sensor's existing tray icon. Enable it in the System Tray tab."), m_tray);
    trayHint->setWordWrap(true);
    tray->addRow(trayHint);
    layout->addWidget(m_tray);
    m_test = new QPushButton(tr("Test Alarm"), this);
    m_test->setToolTip(tr("Execute all checked actions using this draft, without saving. Visual effects last at least three seconds, then the live alarm state is restored."));
    connect(m_test, &QPushButton::clicked, this, &AlarmEditor::testRequested);
    layout->addWidget(m_test);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 3000);
    layout->addWidget(m_progress);
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setTextFormat(Qt::PlainText);
    layout->addWidget(m_error);
    layout->addStretch();
    setTestRemaining(0);
}

void AlarmEditor::load(const AlarmSettings &alarm, bool temperature, TemperatureUnit unit,
                       const QString &unitText, bool traySupported)
{
    m_loading = true;
    ++m_generation;
    m_value = alarm;
    m_temperature = temperature;
    m_unit = unit;
    for (const auto &flag : m_flags) flag.control->setChecked(alarm.*flag.field);
    const auto display = [temperature, unit](double value, bool difference) {
        return !temperature ? value : difference ? temperatureDifferenceToDisplay(value, unit) : temperatureToDisplay(value, unit);
    };
    for (auto *control : {m_lower, m_upper, m_hysteresis}) {
        const bool difference = control == m_hysteresis;
        control->setRange(difference ? 0 : display(-1e12, false), display(1e12, difference));
        control->setSuffix(QLatin1Char(' ') + unitText);
    }
    m_lower->loadValue(display(alarm.lower, false));
    m_upper->loadValue(display(alarm.upper, false));
    m_hysteresis->loadValue(display(alarm.hysteresis, true));
    m_program->setText(alarm.program);
    m_arguments->setText(alarm.arguments);
    m_tray->setVisible(traySupported);
    m_error->clear();
    m_loading = false;
    updateControls();
}

void AlarmEditor::updateControls()
{
    m_lower->setEnabled(m_value.lowerEnabled);
    m_upper->setEnabled(m_value.upperEnabled);
    m_program->setEnabled(m_value.command);
    m_arguments->setEnabled(m_value.command);
    m_test->setEnabled(m_value.hasActions() && m_testRemaining == 0);
    for (const auto &color : m_colors) {
        const QColor value = m_value.*color.field;
        color.control->setText(value.name());
        color.control->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
            .arg(value.name(), value.lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white")));
    }
}

void AlarmEditor::setTestRemaining(int milliseconds)
{
    m_testRemaining = milliseconds;
    m_progress->setVisible(milliseconds > 0);
    m_progress->setValue(milliseconds);
    m_progress->setFormat(tr("Test expires in %1 s").arg(numberLocale().toString(milliseconds / 1000.0, 'f', 1)));
    m_test->setEnabled(m_value.hasActions() && milliseconds == 0);
}

void AlarmEditor::setError(const QString &message) { m_error->setText(message); }
} // namespace qsensors
