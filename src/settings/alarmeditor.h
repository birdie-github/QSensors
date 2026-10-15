// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "settings/sensorpreferences.h"
#include <QWidget>
#include <QList>
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;

namespace qsensors {
class DecimalSpinBox;
class AlarmEditor final : public QWidget {
    Q_OBJECT
public:
    explicit AlarmEditor(QWidget *parent = nullptr);
    void load(const AlarmSettings &alarm, bool temperature, TemperatureUnit unit,
              const QString &unitText, bool traySupported);
    void setTestRemaining(int milliseconds);
    void setError(const QString &message);
signals:
    void edited(const qsensors::AlarmSettings &settings);
    void testRequested();
    void resetRequested();
private:
    void updateControls();
    bool m_loading = false;
    bool m_temperature = false;
    TemperatureUnit m_unit = TemperatureUnit::Celsius;
    AlarmSettings m_value;
    struct Flag { QCheckBox *control; bool AlarmSettings::*field; };
    struct Color { QPushButton *control; QColor AlarmSettings::*field; };
    QList<Flag> m_flags;
    QList<Color> m_colors;
    QCheckBox *m_lowerEnabled;
    QCheckBox *m_upperEnabled;
    DecimalSpinBox *m_lower;
    DecimalSpinBox *m_upper;
    DecimalSpinBox *m_hysteresis;
    QLineEdit *m_program;
    QLineEdit *m_arguments;
    QWidget *m_tray;
    QPushButton *m_test;
    QProgressBar *m_progress;
    QLabel *m_error;
    int m_generation = 0;
    int m_testRemaining = 0;
};
} // namespace qsensors
