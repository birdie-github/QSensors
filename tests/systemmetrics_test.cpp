// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemmetrics.h"
#include <cmath>
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
        const auto available = memoryUsage("MemTotal: 1000 kB\nMemAvailable: 750 kB\n", false);
        require(available && available->percent == 25, "MemAvailable RAM");
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
        near(meanCpuMHz("cpu MHz : 1000\ncpu MHz : 3000\n"), 2000, "frequency mean");
        require(!meanCpuMHz("cpu MHz : nan\n"), "nonfinite frequency");
        require(!meanCpuMHz("model name : CPU\n"), "unsupported frequency");
        require(cpuModel("model name : Example CPU\n") == "Example CPU", "static model");
        near(uptimeSeconds("4294967297.25 100.0\n"), 4294967297.25, "large uptime");
        require(!uptimeSeconds("-1 0\n") && !uptimeSeconds("nan 0\n") && !uptimeSeconds("10 bad\n"), "malformed uptime");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
