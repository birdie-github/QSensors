# Optional system readings

All new readings default to disabled. CPU source headings carry the static model;
rows under that heading are Usage, Average frequency, Maximum frequency and
Core 0 frequency, Core 1 frequency, etc. Existing custom labels are preserved.
Core numbers are presentation indices in sorted package/core order; persistent
IDs contain package and kernel core IDs, independently of display names.

| Reading | Meaning |
| --- | --- |
| Usage | Aggregate `/proc/stat` activity across all logical CPUs, **including SMT siblings**. `100 * (total - idle - iowait) / total`; guest counters are not double counted. Steal is included as non-idle time. |
| Average frequency | Arithmetic mean of one current MHz reading per online physical core. |
| Maximum frequency | Highest current MHz reading among those physical cores, not a rated maximum or boost specification. |
| Core X frequency | Current reported MHz from the lowest-numbered online logical thread of that physical core. |
| RAM usage (%) | `(MemTotal - MemAvailable) / MemTotal * 100`, with the fallback below. |
| RAM usage (MiB) | `MemTotal - MemAvailable` (same fallback), converted from kernel KiB to MiB. |
| Swap usage (MiB) | `SwapTotal - SwapFree`, converted to MiB; zero when no swap is configured. |
| Available RAM | MemAvailable in canonical MiB, or as a percentage of MemTotal after optional calibration. |
| Swap usage (%) | `(SwapTotal - SwapFree) / SwapTotal * 100`; no swap gives zero plus an explicit state in Settings/tooltips. |
| Uptime | Dashboard-only clock from the first `/proc/uptime` field, without 32-bit integer conversions. |
| Charge percentage | Kernel-reported remaining charge, from 0 to 100%, for each supported system battery. |

## System Pressure

CPU Pressure, Memory Pressure, and I/O Pressure appear under **System Pressure**.
They use system-wide `some total` counters in `/proc/pressure/cpu`, `memory`, and
`io`, respectively: the time when at least one task was waiting for CPU execution,
stalled on memory, or stalled on I/O. Values are percentages from 0 to 100:
`100 * delta(stalled microseconds) / elapsed microseconds`.

Each requested sensor keeps its own counter and monotonic timestamp. The first
read is unavailable; later readings cover the actual interval between successful
acquisitions, including subsecond intervals. Kernel avg10/avg60/avg300 values are
not used. Changing one sensor's cadence does not alter the others' baselines.
As with existing sensors, acquisition follows the fastest active dashboard, tray,
preview, or alarm demand for that sensor.

Missing/unreadable/malformed PSI becomes unavailable, retaining the last successful
baseline. Recovery then covers the full elapsed interval. Counter decreases and
nonpositive elapsed intervals re-prime without reporting a percentage. Discovery
rescans preserve pressure baselines; no counter history is stored between runs.
Unsupported kernels show unavailable readings, never fabricated zero pressure.

The sensors start disabled and reuse percentage gauges, digital/tray formatting,
calibration, alarm rules, and the existing worker scheduler. Discovery does not
read pressure files; only requested pressure sensors are acquired. No separate
polling timer or trigger registration is used.

See the [kernel PSI interface documentation](https://www.kernel.org/doc/html/latest/accounting/psi.html).

## Battery charge

Each system battery exposing `/sys/class/power_supply/<name>/capacity` adds a
Charge percentage reading under Battery — `<name>` (for example, BAT0). Supplies
must report `type=Battery`; `scope=Device` peripherals, explicitly absent batteries,
and batteries without a capacity attribute are omitted. A missing scope attribute
is accepted for drivers that do not expose it. No charge/energy-based estimate or
combined multi-battery percentage is calculated.

IDs use `system/battery/<escaped supply name>/charge`, independently of display
labels and discovery order. Kernel supply renaming changes the ID. The reading
starts disabled and supports the existing dashboard, table, tray, calibration,
optional % suffix, refresh interval, and alarm settings. Gauges use the standard
0–100 percentage range. No additional libraries, services, or timers are required.

Only requested battery readings are polled, through the serialized system worker.
When exposed, `present` must be 1; otherwise the reading is unavailable. Missing,
unreadable, malformed, or out-of-range capacity also becomes unavailable, never a
fabricated zero or a stale reading. Zero itself is a valid empty-battery reading.
Newly attached/reinserted batteries may require Rescan (F5), as other new devices do.

The [Linux power-supply documentation](https://www.kernel.org/doc/html/latest/power/power_supply_class.html)
defines capacity as a percentage, distinct from charge and energy quantities.

## Whole-drive I/O

Each supported drive adds Read per update, Written per update, and Busy under
Disk — `<kernel name>` (with the model when available). Read/write values are
**bytes transferred between consecutive acquisitions**, not B/s: with a ten-second
acquisition interval they cover those ten seconds. Labels and units use that
meaning consistently. Busy is elapsed I/O-active time divided by elapsed wall
time, bounded to 0–100%; it is not a drive performance-capacity measurement.

Discovery uses whole devices in `/sys/block` backed by a kernel device outside
`/sys/devices/virtual`. Partitions and software layers such as LVM/device mapper,
MD, loop, RAM, and zram devices are excluded. USB, SATA, NVMe, SCSI, and iSCSI
drives can qualify through the same interface. Hardware RAID volumes and virtual
machine disks may look like ordinary drives to the guest/kernel and cannot be
resolved to their underlying hardware here. Native NVMe multipath virtual heads
are excluded by this rule; hidden paths cannot be monitored through this interface.

The existing serialized worker reads one `stat` snapshot per requested drive.
Each channel has its own baseline. No unselected drive is polled, no new timer
or dependency is added, and all new selections default to off. The first sample,
rescan, counter reset, or recovery needs a new baseline before a value is shown.
Removal/read failure yields unavailable. Where exposed, `diskseq` detects device
replacement, including replacement by counters that happen to be larger.

As with other sensors, acquisition follows the fastest active dashboard, tray,
alarm, or configuration-preview demand. A ten-second dashboard with a one-second
tray for the **same channel** therefore displays one-second transfer samples;
keep those demands aligned when you want a particular transfer window. Busy
updates do not change the transfer channels' windows. After a pause without
demand, the next valid delta covers the elapsed gap since the previous sample.

Canonical read/write values and calibration, ranges, and alarm limits use bytes.
Dashboard/table values scale through B, KiB, MiB, GiB, TiB, PiB, and EiB, with at
most two decimals. Tray/digital readouts use compact B/K/M/G/T/P/E suffixes;
the tray unit can be hidden while retaining scaling. Decimal separators use the
global policy. Read/write gauges start off; choose explicit bounds to enable a
meaningful gauge. Busy uses the usual percentage gauge and alarm controls.

IDs hash the canonical sysfs path and append the channel name. Display-name or
model changes retain preferences; port, driver, or kernel-device-name changes
can change the ID. New drives may need Rescan (F5). On kernels without `diskseq`,
replacement at exactly the same path cannot always be distinguished from the
original device; Rescan resets its baselines.

Counter interpretation follows the [kernel I/O statistics documentation](https://cdn.kernel.org/doc/html/latest/admin-guide/iostats.html).
Busy is an approximate kernel accounting statistic, especially with concurrent I/O.

## Physical-core frequencies

Frequency presentation/calculations exclude HT/SMT siblings. Usage does not.
Discovery groups logical CPUs by sysfs `physical_package_id` and `core_id`.
Only physical cores with an online thread are exposed. Each core contributes
once to the average/maximum, regardless of its sibling count. Online status is
checked when frequency readings are requested; a representative going offline
can fall back to another online sibling of the same core. Newly enabled physical
cores require Rescan to enter the catalog. Usage remains independent of topology.

Frequency uses the representative's `cpu MHz` entry from a single `/proc/cpuinfo`
snapshot. It is a kernel-reported estimate, not an effective work rate. If a
representative has no valid reading, average/maximum are unavailable rather than
silently averaging an incomplete set. If topology is unknown, frequencies are
unavailable rather than guessing which logical CPUs are physical cores. A fully
offline core is excluded from summaries and its saved row becomes unavailable.

## Automatic frequency gauge limits

Frequency gauges use `/sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq`,
converting the driver's hardware maximum from kHz to canonical MHz. The per-core
upper bound is that representative core's limit. Maximum frequency uses the
largest limit across online physical cores; Average frequency uses the arithmetic
mean of those limits, with one contribution per physical core. For a hybrid CPU,
slower cores therefore reduce the average upper bound appropriately.

Shared cpufreq policies are read once per requested batch. Limits are refreshed
alongside frequency samples, without a second timer or GUI file reads. This uses
the hardware capability reported by the driver, not the current policy cap
`scaling_max_freq`. Missing, zero or malformed limits leave the current reading
usable but omit its automatic gauge. Aggregate bounds require limits for every
online physical core; no 5 GHz fallback or estimate from current readings is
used. Explicit custom bounds take precedence. Optional calibration applies to
both the current reading and provider maximum; nonpositive/invalid maxima omit
the automatic gauge. The instantaneous Maximum frequency reading retains its
existing meaning; this change fixes normalization, not the measured value.

Frequency batches read the aggregate sysfs `online` list once and reuse its
parsed ranges while unchanged. CPU-to-policy paths and successful
`cpuinfo_max_freq` limits persist across batches, shared by policy. Rescan or an
online-set change clears this metadata; a missing current reading drops its
CPU's mapping and policy limit, and failed limit reads are retried rather than
cached. Missing/malformed aggregate lists fall back to the existing per-CPU
online checks. Current MHz values still come from a fresh `/proc/cpuinfo` batch.
Use Rescan after replacing a cpufreq driver or changing hardware-limit metadata
without an online-set change; no periodic metadata-maintenance timer is added.

## Dashboard text styles

Normal retains the existing font/compact-unit readouts. Digital reuses the
Uptime vector segment engine, with glyphs for decimal separators, unit letters,
percentages and a small square degree marker. Readouts fit both the width and
height available, reserving room for strokes and shadows. CPU frequencies use
filled, chamfered seven-segment digits, following KSensors 0.7.3's CpuPanel
QLCDNumber::Filled appearance. No per-tile widgets, bitmaps or timers are added.
The source footer is removed for every tile; the sensor label remains.

| Digital reading | Format |
| --- | --- |
| Memory | Rounded whole M/G/T/P value, no space, e.g. `1520M`. Units are binary (1024 per step) and promote when rounding would exceed 9999 of the current unit. Values below one MiB show `0M`. |
| Voltage | At most two decimals, no space, e.g. `5.09V` or `5V`. |
| Temperature | One decimal, nearest-tenth rounding, then a square degree mark for C/F. Kelvin uses K, e.g. `298.1K`. The displayed symbol is vector geometry, not a square Unicode placeholder. |
| Fan | Rounded whole value plus ` RPM`, e.g. `2000 RPM`. |
| CPU frequency | Rounded whole MHz in filled watch digits, with a small MHz unit underneath. |
| Percentage / humidity | At most one decimal plus %, e.g. `37.2%`. |
| Current / power / energy | At most two decimals followed by the canonical unit. |

Normal temperature readings also use one decimal, including Kelvin. Formatting
never changes samples, calibration, gauge calculations, table sort keys or tray
settings. Normal/Digital and Show gauge are independent, so all four combinations
work for gauge-capable sensors. Uptime supports both styles and remains gauge-free.

RAM and Swap each have independent percentage and MiB sensors. Existing IDs and
preferences for percentage readings are retained; the new MiB IDs are
`system/memory/ram-used` and `system/memory/swap-used`, initially disabled. MiB
readings have their own labels, calibration, gauge bounds, dashboard/tray
selections and intervals. This avoids changing the units of an existing
calibration or duplicating the underlying meminfo file read: all RAM readings
share one parsed RAM result, all Swap readings share one parsed Swap result,
and those use the same meminfo snapshot. Automatic MiB gauge upper bounds use
total RAM/swap capacity from that snapshot. No-swap MiB readings show `0M` in
Digital mode with the existing explanatory tooltip, and omit the zero-span gauge.

## Normal, table and tray compact display

Normal dashboard, configuration, and table readings use MHz below 1000 MHz and
GHz at or above 1000 MHz. Normal tray frequency follows a separate four-character
scheme: K below 1 MHz, whole MHz through 9999 MHz, and rounded G from 10 GHz.
Values below 1 kHz or above 999 GHz show ERR. Digital tray frequency supports
1–9999 MHz and shows ERR outside that range. Tooltips retain the full Hz reading.

Memory uses binary units MiB, GiB, TiB, PiB (1024 per step), with M/G/T/P suffixes
in the tray when Show the unit is enabled. Nonnegative amounts below one MiB
show zero. Percentage readings use % without the sub-MiB floor.

Non-frequency tray readings with hidden units use up to four characters,
including the decimal separator and sign, and up to two decimal places. Thus
14.5 GiB can appear as `14.5`. Showing a unit reserves room for its suffix.
The shared [sensor formatting rules](sensor-settings.md) describe precision,
rounding, unit selection, and the distinct display contexts. Formatting never
changes samples or calculations. The global decimal-separator preference selects
dot, comma, or the system locale for UI values.

Calibration and custom bounds for Available RAM remain MiB even in percentage
mode. Percentage conversion uses the total from the same meminfo snapshot.
Changing display mode never compounds calibration or rewrites canonical values.

## Dashboard-only uptime

Uptime cannot be selected for a tray icon. The centralized eligibility rule is
applied in Settings, preference loading/saving, and the tray controller itself;
manual `tray=true` for uptime is ignored. No uptime background polling is caused
by tray selection. Settings preview still requests it temporarily, as usual.

Below 100 hours, the tile has a small digital HH:MM legend, a large digital time,
and a small digital UPTIME footer. From 100 hours onward it omits the top legend,
shows X days and the remaining HH:MM underneath, and retains the UPTIME footer.
The cutoff interprets 99:59 as hours:minutes; exactly 100 hours is 4 days / 04:00.
Digital defaults to the UPTIME footer; a custom sensor name replaces it using
normal text so arbitrary names remain legible. Normal displays the same time
and days with regular fonts and the sensor label. Settings and the table also display the formatted time.

The readout defaults to red with an offset dark-red shadow. Its existing sensor
foreground setting becomes Readout color; the shadow is derived by darkening
that color. Reset restores red. Small legends are black on light tiles and white
on dark tiles for contrast. The renderer uses independently implemented vector
segment glyphs rather than a bundled font, enlarged bitmap or a per-tile widget.
All drawing remains in the grid paint event; paths are shared across sizes, with
no new timer. Changes in seconds do not repaint an otherwise unchanged tile.
Days are formatted without a narrowing integer cast, so large uptimes do not
wrap at 32 bits; unavailable/invalid data shows a dash.

## Work, parsing and verification

One serialized worker owns both providers. Hardware failure does not suppress
system readings. Each needed proc file is read once per batch; RAM and Available
RAM share one parsed result, while swap shares that meminfo read. File reads are
bounded to 4 MiB. Static model/topology labels are discovered rather than rebuilt
on every measurement. No system demand means no periodic proc/topology work;
Settings preview temporarily requests the catalog as before.

CPU usage needs two valid counter snapshots. Counter decreases, malformed input,
overflow and zero intervals invalidate a reading without presenting the old value
as current. Rescan resets its baseline; partial batches for other sensors retain it. RAM uses
MemAvailable when present; otherwise estimated availability is free + buffers +
cache + reclaimable slab − shared memory, bounded to total, with a tooltip note.
These are host figures, not cgroup limits or zram compression measurements.

Opt-in parser/formatter fixtures cover guest accounting, large counters, resets,
malformed data, RAM fallback, no swap, processor/frequency association, MiB
references, hardware-limit unit conversion and heterogeneous-core normalization, unit
thresholds, sub-MiB values and the three-character numeric
budget. They have not been compiled or executed for this patch.

```sh
cmake -S . -B build -DQSENSORS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Manual checks:

- For each type, try Normal/Digital with gauges on/off; Uptime must have no gauge.
- Check readout fit, labels, decimal points, square degree markers, shadows and
  transparency at 64/128/512/1024 pixels, including negative and unavailable readings.
- Test memory transitions around 9999.5 M/G/T and voltage trailing-zero removal.
- Compare RAM/Swap MiB and percentage readings against the same meminfo snapshot;
  enable both and verify independent calibration/settings survive restart.
- Check that absent new gauge/text keys use their defaults; obsolete
  presentation/global-gauge keys must not affect the new controls.

- Rescan and compare frequency-row count with physical cores from `lscpu`.
  On SMT systems there must be one row per physical core, not one per thread.
- Compare Average/Maximum frequency with the displayed physical-core readings;
  Usage should still agree approximately with tools sampling all logical CPUs.
- Compare per-core gauge endpoints with cpuinfo_max_freq divided by 1000. The
  Maximum endpoint must be the largest physical-core limit; Average must be their
  mean. Test custom range precedence and missing limits if your environment allows.
- Test 900 MHz / 1.2 GHz and MiB/GiB transitions on dashboard, table and tray.
- Toggle Available RAM between MiB and percentage, including with calibration
  and custom ranges; Apply, restart and confirm persistence.
- Compare Available RAM with `/proc/meminfo`; sub-MiB values display zero.
- Check uptime at 64/128/512/1024 logical pixels, reset/change readout color,
  and ensure no uptime tray checkbox/icon exists, even with `tray=true` in the INI.
  Parser/format fixtures cover 99:59 and 100-hour boundaries; exercising them
  visually still needs a local test environment.
- Deselect all system readings and close Settings; periodic system reads stop.

Reference definitions:
https://www.kernel.org/doc/html/latest/filesystems/proc.html
https://www.kernel.org/doc/html/latest/admin-guide/cputopology.html
https://docs.kernel.org/admin-guide/pm/cpufreq.html
