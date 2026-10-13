// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemmetrics.h"
#include "rendering/valueformat.h"
#include <cmath>
#include <QLocale>
#include <iostream>
#include <stdexcept>

using namespace qsensors::systemmetrics;
namespace {
void require(bool condition, const char *name)
{
    if (!condition) throw std::runtime_error(name);
}
void near(const std::optional<double> &value, double expected, const char *name)
{
    require(value && std::abs(*value - expected) < 1e-9, name);
}
}
int main()
{
    QLocale::setDefault(QLocale::c());
    try {
        const auto before = cpuCounters("cpu 4294967300 0 0 4294967300 0 0 0 0 100 50\ncpu0 1 2 3 4\n");
        const auto after = cpuCounters("cpu 4294967320 0 0 4294967380 0 0 0 0 120 60\n");
        require(before && after, "64-bit aggregate counters");
        near(cpuActivity(*before, *after), 20, "guest not counted twice");
        require(!cpuActivity(*after, *before), "counter reset");
        require(!cpuActivity(*before, *before), "zero interval");
        const auto waited = cpuCounters("cpu 20 0 0 100 50 0 0 0\n");
        const auto waitReset = cpuCounters("cpu 30 0 0 110 49 0 0 0\n");
        require(waited && waitReset && !cpuActivity(*waited, *waitReset), "decreasing iowait");
        CpuCounters zero, overflow;
        overflow.fields[0] = 18446744073709551615ULL;
        overflow.fields[1] = 1;
        require(!cpuActivity(zero, overflow), "delta sum overflow");
        require(cpuCounters("cpu 1 2 3 4\n").has_value(), "optional modern fields");
        require(!cpuCounters("cpu 1 bad 3 4\n"), "malformed counter");
        require(!cpuCounters("cpu 18446744073709551616 0 0 0\n"), "counter overflow");
        require(!cpuCounters("cpu0 1 2 3 4\n"), "aggregate missing");
        const auto frequencies = cpuFrequencies("processor : 0\ncpu MHz : 900\n\nprocessor : 1\ncpu MHz : 1200\n");
        require(frequencies.value(0) == 900 && frequencies.value(1) == 1200, "logical frequency association");
        near(frequencyLimitMHz("5200000\n"), 5200, "sysfs kHz to MHz");
        require(!frequencyLimitMHz("0\n") && !frequencyLimitMHz("bad\n") && !frequencyLimitMHz("-1\n"), "invalid hardware limits");
        const auto limits = frequencyLimits({5200, 3600, 2800});
        require(limits && limits->maximum == 5200 && std::abs(limits->average - 11600.0 / 3) < 1e-9, "heterogeneous core normalization");
        require(!frequencyLimits({}) && !frequencyLimits({5200, std::numeric_limits<double>::quiet_NaN()}), "incomplete limits not guessed");
        auto uptime = qsensors::formatUptime(99 * 3600 + 59 * 60 + 59);
        require(uptime.time == "99:59" && uptime.days.isEmpty(), "uptime before day layout");
        uptime = qsensors::formatUptime(100 * 3600);
        require(uptime.time == "04:00" && uptime.days == "4 days", "uptime day layout");
        uptime = qsensors::formatUptime(4294967297.0);
        require(!uptime.days.isEmpty() && uptime.time.size() == 5, "large uptime layout");
        require(qsensors::formatUptime(-1).time == QStringLiteral("—"), "invalid uptime layout");
        require(!qsensors::sensorSupportsTray(QStringLiteral("system/uptime")), "uptime dashboard only");
        require(qsensors::sensorSupportsTray(QStringLiteral("system/cpu/activity")), "ordinary sensor tray eligibility");
        qsensors::SensorDescriptor descriptor;
        descriptor.kind = qsensors::SensorKind::Frequency;
        auto formatted = qsensors::formatSensorValue(descriptor, 900);
        require(formatted.number == "900" && formatted.unit == "MHz", "MHz format");
        formatted = qsensors::formatSensorValue(descriptor, 1200, true);
        require(formatted.number == "1.2" && formatted.unit == "G", "GHz tray format");
        descriptor.kind = qsensors::SensorKind::Memory;
        formatted = qsensors::formatSensorValue(descriptor, 2560);
        require(formatted.number == "2.5" && formatted.unit == "GiB", "GiB format");
        formatted = qsensors::formatSensorValue(descriptor, 0.5, true);
        require(formatted.number == "0" && formatted.unit == "M", "sub-MiB format");
        formatted = qsensors::formatSensorValue(descriptor, 100);
        require(formatted.number == "100" && formatted.unit == "MiB", "three-character MiB");
        for (double value : {0.0, 0.5, 9.94, 9.95, 99.9, 999.0, 1000.0, 1024.0, 2560.0, 1e9}) {
            require(qsensors::formatSensorValue(descriptor, value).number.size() <= 3, "memory numeric width");
        }
        const auto available = memoryUsage("MemTotal: 1000 kB\nMemAvailable: 750 kB\n", false);
        require(available && available->percent == 25, "MemAvailable RAM");
        require(available->availableMiB == 750.0 / 1024 && available->totalMiB == 1000.0 / 1024, "available RAM canonical MiB");
        const auto fallback = memoryUsage("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 100 kB\nCached: 300 kB\nSReclaimable: 100 kB\nShmem: 50 kB\n", false);
        require(fallback && fallback->percent == 45 && !fallback->detail.isEmpty(), "RAM fallback");
        require(!memoryUsage("MemTotal: 1000 kB\n", false), "missing availability");
        require(!memoryUsage("MemTotal: 0 kB\nMemAvailable: 0 kB\n", false), "zero RAM");
        require(!memoryUsage("MemTotal: 1000 kB\nMemAvailable: 1001 kB\n", false), "invalid available range");
        require(!memoryUsage("MemTotal: 1000 bytes\nMemAvailable: 20 kB\n", false), "malformed memory units");
        require(!memoryUsage("MemTotal: 1000 kB\nMemAvailable: bad kB\n", false), "malformed availability");
        const auto none = memoryUsage("SwapTotal: 0 kB\nSwapFree: 0 kB\n", true);
        require(none && none->percent == 0 && none->detail == "No swap configured", "no swap");
        const auto swap = memoryUsage("SwapTotal: 100 kB\nSwapFree: 25 kB\n", true);
        require(swap && swap->percent == 75, "swap usage");
        require(!memoryUsage("SwapTotal: 100 kB\n", true), "missing swap free");
        require(cpuFrequencies("processor : 0\ncpu MHz : nan\n").isEmpty(), "nonfinite frequency");
        require(cpuFrequencies("model name : CPU\n").isEmpty(), "unsupported frequency");
        require(cpuModel("model name : Example CPU\n") == "Example CPU", "static model");
        near(uptimeSeconds("4294967297.25 100.0\n"), 4294967297.25, "large uptime");
        require(!uptimeSeconds("-1 0\n") && !uptimeSeconds("nan 0\n") && !uptimeSeconds("10 bad\n"), "malformed uptime");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
