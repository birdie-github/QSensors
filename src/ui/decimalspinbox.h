// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDoubleSpinBox>
#include <optional>

namespace qsensors {

// Shared policy for editable decimals. Sensor readout formatting is separate.
class DecimalSpinBox final : public QDoubleSpinBox {
    Q_OBJECT
public:
    enum class Precision { Standard, Fine };
    explicit DecimalSpinBox(QWidget *parent = nullptr, Precision precision = Precision::Standard);
    // Loading never emits edit signals or rounds the caller's canonical draft.
    void loadValue(double value);
    static std::optional<double> getValue(QWidget *parent, const QString &title, const QString &label,
                                         double initial, double minimum, double maximum, double step);

signals:
    void valueEdited(double value);

protected:
    QString textFromValue(double value) const override;

private:
    // Precision changes belong to the shared policy, not individual pages.
    using QDoubleSpinBox::setDecimals;
    static constexpr int defaultDecimals = 2;
    static constexpr int fineDecimals = 3;
    void updatePreciseToolTip();
    std::optional<double> m_loadedValue;
    bool m_textEdited = false;
    bool m_helpCaptured = false;
    QString m_help;
};

} // namespace qsensors
