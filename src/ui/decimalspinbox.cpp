// SPDX-License-Identifier: GPL-3.0-only
#include "ui/decimalspinbox.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <utility>

namespace qsensors {

DecimalSpinBox::DecimalSpinBox(QWidget *parent, Precision precision) : QDoubleSpinBox(parent)
{
    setDecimals(precision == Precision::Fine ? fineDecimals : defaultDecimals);
    setKeyboardTracking(false);
    setSingleStep(.01);
    connect(lineEdit(), &QLineEdit::textEdited, this, [this] { m_textEdited = true; });
    connect(this, &QDoubleSpinBox::valueChanged, this, &DecimalSpinBox::valueEdited);
    connect(this, &QDoubleSpinBox::editingFinished, this, [this] {
        // An intentional edit matching the rounded display must still replace
        // a more precise draft. Merely focusing/accepting the control must not.
        if (std::exchange(m_textEdited, false)) emit valueEdited(value());
    });
}

QString DecimalSpinBox::textFromValue(double value) const
{
    int digits = defaultDecimals;
    if (decimals() == fineDecimals) {
        const double thousandths = std::round(value * 1000);
        if (std::fmod(std::abs(thousandths), 10.0) >= .5) digits = fineDecimals;
    }
    return locale().toString(value == 0 ? 0.0 : value, 'f', digits);
}

void DecimalSpinBox::loadValue(double value)
{
    const QSignalBlocker blocker(this);
    m_textEdited = false;
    setValue(value);
    if (!m_helpCaptured) {
        m_help = toolTip();
        m_helpCaptured = true;
    }
    const QString precise = tr("Stored value (unchanged unless edited): %1").arg(locale().toString(value, 'g', 17));
    setToolTip(m_help.isEmpty() ? precise : m_help + QLatin1Char('\n') + precise);
}

std::optional<double> DecimalSpinBox::getValue(QWidget *parent, const QString &title, const QString &label,
                                              double initial, double minimum, double maximum, double step)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(label, &dialog);
    description->setTextFormat(Qt::PlainText);
    layout->addWidget(description);
    auto *number = new DecimalSpinBox(&dialog);
    number->setRange(minimum, maximum);
    number->setSingleStep(step);
    number->loadValue(initial);
    description->setBuddy(number);
    layout->addWidget(number);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    number->setFocus();
    number->selectAll();
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    number->interpretText();
    return number->value();
}

} // namespace qsensors
