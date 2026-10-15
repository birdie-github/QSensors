// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <QHash>
#include <QList>
#include <array>
#include <optional>

namespace qsensors::systemmetrics {

// PSI totals count microseconds of system-wide partial (some) pressure.
std::optional<quint64> pressureSomeTotal(const QByteArray &contents);
class PressureTracker {
public:
    std::optional<double> update(std::optional<quint64> totalMicroseconds, qint64 nanoseconds);
private:
    struct Baseline { quint64 totalMicroseconds; qint64 nanoseconds; };
    std::optional<Baseline> m_previous;
};

struct CpuCounters {
    std::array<quint64, 8> fields{}; // user..steal; guest fields are already included.
};
struct Usage {
    double percent = 0;
    QString detail;
    double availableMiB = 0;
    double totalMiB = 0;
};
std::optional<CpuCounters> cpuCounters(const QByteArray &contents);
QHash<int, double> cpuFrequencies(const QByteArray &contents);
struct FrequencyLimits { double average; double maximum; };
std::optional<double> frequencyLimitMHz(const QByteArray &contents);
std::optional<FrequencyLimits> frequencyLimits(const QList<double> &coreMaxima);
std::optional<double> cpuActivity(const CpuCounters &previous, const CpuCounters &current);
std::optional<Usage> memoryUsage(const QByteArray &contents, bool swap);
QString cpuModel(const QByteArray &contents);
std::optional<double> uptimeSeconds(const QByteArray &contents);
std::optional<double> batteryChargePercent(const QByteArray &contents);
struct DiskCounters { quint64 sectorsRead; quint64 sectorsWritten; quint64 busyMilliseconds; };
struct DiskInterval { double readBytes; double writtenBytes; double busyPercent; };
std::optional<DiskCounters> diskCounters(const QByteArray &contents);
std::optional<DiskInterval> diskInterval(const DiskCounters &before, const DiskCounters &after, double elapsedMilliseconds);

} // namespace qsensors::systemmetrics
