// SPDX-License-Identifier: GPL-3.0-only
#include "settings/numberformat.h"
#include <QSettings>

namespace qsensors {

NumberFormatSettings &NumberFormatSettings::instance()
{
    static NumberFormatSettings settings;
    return settings;
}

NumberFormatSettings::NumberFormatSettings()
{
    const QString value = QSettings().value(QStringLiteral("Display/decimalSeparator")).toString();
    if (value == QStringLiteral("dot")) m_separator = DecimalSeparator::Dot;
    else if (value == QStringLiteral("comma")) m_separator = DecimalSeparator::Comma;
}

void NumberFormatSettings::setSeparator(DecimalSeparator separator)
{
    if (separator != DecimalSeparator::System && separator != DecimalSeparator::Dot
        && separator != DecimalSeparator::Comma) return;
    if (separator == m_separator) return;
    QSettings settings;
    if (separator == DecimalSeparator::System) settings.remove(QStringLiteral("Display/decimalSeparator"));
    else settings.setValue(QStringLiteral("Display/decimalSeparator"), separator == DecimalSeparator::Dot
        ? QStringLiteral("dot") : QStringLiteral("comma"));
    m_separator = separator;
    emit changed();
}

QLocale numberLocale()
{
    const auto separator = NumberFormatSettings::instance().separator();
    if (separator == DecimalSeparator::System) return QLocale();
    QLocale locale = separator == DecimalSeparator::Dot ? QLocale(QLocale::English, QLocale::UnitedStates)
                                                      : QLocale(QLocale::German, QLocale::Germany);
    // Avoid ambiguous grouping, e.g. a dot as both a group and decimal separator.
    locale.setNumberOptions(QLocale::OmitGroupSeparator | QLocale::RejectGroupSeparator);
    return locale;
}

} // namespace qsensors
