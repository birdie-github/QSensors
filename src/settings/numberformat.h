// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QLocale>
#include <QObject>

namespace qsensors {

enum class DecimalSeparator { System, Dot, Comma };

// Presentation policy only. Never use this locale for settings or command data.
class NumberFormatSettings final : public QObject {
    Q_OBJECT
public:
    static NumberFormatSettings &instance();
    DecimalSeparator separator() const { return m_separator; }
    void setSeparator(DecimalSeparator separator);
signals:
    void changed();
private:
    NumberFormatSettings();
    DecimalSeparator m_separator = DecimalSeparator::System;
};

QLocale numberLocale();

} // namespace qsensors
