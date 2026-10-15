// SPDX-License-Identifier: GPL-3.0-only
#include "system/systemmetrics.h"
#include "rendering/valueformat.h"
#include <cmath>
#include <QLocale>
#include <iostream>
#include <limits>
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
        require(pressureSomeTotal("some avg10=99.00 avg60=88.00 avg300=77.00 total=123456\nfull total=999999\n")
            == std::optional<quint64>(123456), "PSI uses some total, not averages or full");
        require(pressureSomeTotal("full total=42\n  some\ttotal=0 future=ignored\n")
            == std::optional<quint64>(0), "zero pressure and whitespace/field-order tolerance");
        require(pressureSomeTotal("some total=18446744073709551615\n")
            == std::optional<quint64>(std::numeric_limits<quint64>::max()), "64-bit pressure total");
        for (const QByteArray &invalid : {QByteArray(), QByteArray("full total=10\n"),
                 QByteArray("some avg10=1.00\n"), QByteArray("some total=\n"),
                 QByteArray("some total=-1\n"), QByteArray("some total=+1\n"),
                 QByteArray("some total=1.5\n"), QByteArray("some total=NaN\n"),
                 QByteArray("some total=18446744073709551616\n"),
                 QByteArray("some total=10 total=20\n"), QByteArray("some total=10\nsome total=20\n")})
            require(!pressureSomeTotal(invalid), "missing/malformed/ambiguous PSI is unavailable");
        PressureTracker cpuPressure, memoryPressure, ioPressure;
        require(!cpuPressure.update(1000000, 0), "pressure first read needs a baseline");
        require(!memoryPressure.update(2000000, 0) && !ioPressure.update(0, 0), "independent initial baselines");
        near(cpuPressure.update(1750000, 3000000000LL), 25, "pressure over actual three-second interval");
        near(memoryPressure.update(2500000, 1000000000LL), 50, "memory unaffected by CPU acquisition");
        near(ioPressure.update(10000, 100000000LL), 10, "subsecond pressure interval");
        near(cpuPressure.update(1800000, 3500000000LL), 10, "CPU cadence change uses actual elapsed time");
        near(memoryPressure.update(3000000, 5000000000LL), 12.5, "CPU cadence change leaves memory baseline intact");
        require(!cpuPressure.update(std::nullopt, 4000000000LL), "failed pressure reading unavailable");
        near(cpuPressure.update(2300000, 5500000000LL), 25, "recovery spans last successful read, not failure");
        require(!cpuPressure.update(10, 6500000000LL), "counter reset re-primes without spike");
        near(cpuPressure.update(250010, 7500000000LL), 25, "pressure recovers after reset");
        near(cpuPressure.update(250010, 8500000000LL), 0, "idle pressure is valid zero");
        near(cpuPressure.update(2250010, 9500000000LL), 100, "pressure is bounded at 100 percent");
        PressureTracker largePressure;
        require(!largePressure.update(9007199254740992ULL, 0), "large pressure baseline");
        near(largePressure.update(9007199254740993ULL, 100000), 1, "integer subtraction preserves small pressure delta");
        PressureTracker invalidClock;
        require(!invalidClock.update(100, -1), "negative monotonic timestamp rejected");
        require(!invalidClock.update(100, 0), "invalid timestamp did not create baseline");
        require(!invalidClock.update(100, 0), "zero elapsed time is unavailable");
        near(invalidClock.update(100, 100000000), 0, "zero elapsed read causes no spike");
        require(!invalidClock.update(100, 50000000), "backward clock re-primes");
        near(invalidClock.update(50100, 150000000), 50, "backward clock recovers safely");
        const auto diskBefore = diskCounters("10 0 100 5 20 0 200 8 0 1000 12\n");
        const auto diskAfter = diskCounters("11 0 108 6 21 0 216 9 0 3500 14 0 0 0 0 0 0\n");
        require(diskBefore && diskAfter, "old and extended disk stat fields");
        const auto disk = diskInterval(*diskBefore, *diskAfter, 10000);
        require(disk && disk->readBytes == 4096 && disk->writtenBytes == 8192
            && disk->busyPercent == 25, "disk bytes per ten-second update and busy percentage");
        const auto diskOneSecond = diskInterval(*diskBefore, *diskAfter, 1000);
        require(diskOneSecond && diskOneSecond->readBytes == 4096 && diskOneSecond->busyPercent == 100,
            "transfer counts are not normalized per second; busy is bounded");
        const auto idleDisk = diskInterval(*diskAfter, *diskAfter, 1000);
        require(idleDisk && idleDisk->readBytes == 0 && idleDisk->writtenBytes == 0 && idleDisk->busyPercent == 0,
            "idle disk is valid zero");
        require(!diskInterval(*diskAfter, *diskBefore, 1000), "disk counter reset");
        require(!diskInterval(*diskBefore, *diskAfter, 0)
            && !diskInterval(*diskBefore, *diskAfter, -1), "invalid disk sample interval");
        const DiskCounters largeBefore{9007199254740992ULL, 0, 0};
        const DiskCounters largeAfter{9007199254740993ULL, 0, 0};
        const auto largeDisk = diskInterval(largeBefore, largeAfter, 1000);
        require(largeDisk && largeDisk->readBytes == 512, "subtract 64-bit counters before converting to double");
        require(!diskCounters("1 2 3 4") && !diskCounters("1 0 bad 0 0 0 0 0 0 0 0")
            && !diskCounters("1 0 -1 0 0 0 0 0 0 0 0")
            && !diskCounters("1 0 18446744073709551616 0 0 0 0 0 0 0 0"), "invalid disk counters");
        near(batteryPowerWatts(5.205, "Discharging\n"), -5.205, "battery discharge power is negative");
        near(batteryPowerWatts(-5.205, "Discharging"), -5.205, "signed driver discharge is not inverted twice");
        near(batteryPowerWatts(-30, "Charging"), 30, "battery charging power is positive");
        near(batteryPowerWatts(0, "Discharging"), 0, "zero battery power remains zero");
        near(batteryPowerWatts(0, "Full"), 0, "full battery zero power");
        near(batteryPowerWatts(1, "Not charging"), 1, "idle status preserves reported power magnitude");
        require(!batteryPowerWatts(8, "Unknown") && !batteryPowerWatts(8, ""), "unknown battery direction unavailable");
        require(!batteryPowerWatts(std::numeric_limits<double>::infinity(), "Charging"), "invalid battery power unavailable");
        near(batteryChargePercent("57\n"), 57, "battery charge percentage");
        near(batteryChargePercent("0\n"), 0, "empty battery is a valid reading");
        near(batteryChargePercent("100\n"), 100, "full battery");
        for (const QByteArray &invalid : {QByteArray(), QByteArray("-1"), QByteArray("101"),
                 QByteArray("57.5"), QByteArray("NaN"), QByteArray("50\n60"),
                 QByteArray("18446744073709551616")})
            require(!batteryChargePercent(invalid), "invalid battery charge is unavailable");
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
        descriptor.kind = qsensors::SensorKind::Bytes;
        auto byteReading = qsensors::formatSensorValue(descriptor, 512);
        require(byteReading.number == "512" && byteReading.unit == "B", "bytes below KiB remain visible");
        byteReading = qsensors::formatSensorValue(descriptor, 1536);
        require(byteReading.number == "1.5" && byteReading.unit == "KiB", "binary byte scaling");
        byteReading = qsensors::formatSensorValue(descriptor, 1073741824);
        require(byteReading.number == "1" && byteReading.unit == "GiB", "GiB byte scaling");
        byteReading = qsensors::formatSensorValue(descriptor, 0, true);
        require(byteReading.number == "0" && byteReading.unit == "B", "idle bytes in tray");
        require(qsensors::formatTrayByteNumber(1536) == "1.5", "unit-free byte scaling");
        require(qsensors::formatDigitalSensorValue(descriptor, 1536) == "1.5K", "digital byte suffix");
        for (double bytes : {0.0, 512.0, 999.4, 1023.0, 1536.0, 1048576.0, 1e12}) {
            const auto trayBytes = qsensors::formatSensorValue(descriptor, bytes, true);
            require((trayBytes.number + trayBytes.unit).size() <= 4 && trayBytes.number != "ERR",
                "byte tray values fit without clipping");
        }
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        require(qsensors::formatSensorValue(descriptor, 1536).number == "1,5"
            && qsensors::formatTrayByteNumber(1536) == "1,5", "localized byte decimals");
        QLocale::setDefault(QLocale::c());
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
