// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <optional>

namespace qsensors::systemmetrics {

struct CpuCounters {
    std::array<quint64, 8> fields{}; // user..steal; guest fields are already included.
};
struct Usage {
    double percent = 0;
    QString detail;
};
std::optional<CpuCounters> cpuCounters(const QByteArray &contents);
std::optional<double> cpuActivity(const CpuCounters &previous, const CpuCounters &current);
std::optional<Usage> memoryUsage(const QByteArray &contents, bool swap);
QString cpuModel(const QByteArray &contents);
std::optional<double> meanCpuMHz(const QByteArray &contents);
std::optional<double> uptimeSeconds(const QByteArray &contents);

} // namespace qsensors::systemmetrics
