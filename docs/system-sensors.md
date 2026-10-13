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
| RAM usage | `(MemTotal - MemAvailable) / MemTotal * 100`, with the fallback below. |
| Available RAM | MemAvailable in canonical MiB, or as a percentage of MemTotal after optional calibration. |
| Swap usage | `(SwapTotal - SwapFree) / SwapTotal * 100`; no swap gives zero plus an explicit state in Settings/tooltips. |
| Uptime | First `/proc/uptime` field, in seconds, without 32-bit integer conversions. |

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

## Compact display

Frequency uses MHz below 1000 MHz and GHz at/above 1000 MHz. Tiles, Settings and
the table show full units; numeric tray icons append M or G. Examples: 900 MHz /
900M; 1200 MHz becomes 1.2 GHz / 1.2G.

Memory uses binary units MiB, GiB, TiB, PiB (1024 per step). Tiles, Settings and
the table show full units; the tray appends M/G/T/P. Examples: 100 MiB / 100M;
2560 MiB becomes 2.5 GiB / 2.5G. Nonnegative values below one MiB show 0 MiB / 0M.
The percentage choice instead uses % and does not apply the sub-MiB floor.

The numeric part has at most three characters, counting the decimal separator
and sign. Positive values below 9.95 use one decimal; larger values are rounded
to whole numbers. Memory promotes at 999.5 of the current unit to avoid a
four-digit rounded result. Frequency retains the requested 1000-MHz unit boundary
and caps the last rounded MHz value at 999. Values beyond the largest supported
unit's three-digit capacity saturate at 999 (negative capacity is two digits plus
sign). Measurement/calculation values retain full precision; formatting never
feeds back into calculations. Locale decimal separators are respected.

Calibration and custom bounds for Available RAM remain MiB even in percentage
mode. Percentage conversion uses the total from the same meminfo snapshot.
Changing display mode never compounds calibration or rewrites canonical values.

## Work, parsing and verification

One serialized worker owns both providers. Hardware failure does not suppress
system readings. Each needed proc file is read once per batch; RAM and Available
RAM share one parsed result, while swap shares that meminfo read. File reads are
bounded to 4 MiB. Static model/topology labels are discovered rather than rebuilt
on every measurement. No system demand means no periodic proc/topology work;
Settings preview temporarily requests the catalog as before.

CPU usage needs two valid counter snapshots. Counter decreases, malformed input,
overflow and zero intervals invalidate a reading without presenting the old value
as current. Rescan or a batch without Usage demand resets its baseline. RAM uses
MemAvailable when present; otherwise estimated availability is free + buffers +
cache + reclaimable slab − shared memory, bounded to total, with a tooltip note.
These are host figures, not cgroup limits or zram compression measurements.

Opt-in parser/formatter fixtures cover guest accounting, large counters, resets,
malformed data, RAM fallback, no swap, processor/frequency association, MiB
references, unit thresholds, sub-MiB values and the three-character numeric
budget. They have not been compiled or executed for this patch.

```sh
cmake -S . -B build -DQSENSORS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Manual checks:

- Rescan and compare frequency-row count with physical cores from `lscpu`.
  On SMT systems there must be one row per physical core, not one per thread.
- Compare Average/Maximum frequency with the displayed physical-core readings;
  Usage should still agree approximately with tools sampling all logical CPUs.
- Test 900 MHz / 1.2 GHz and MiB/GiB transitions on dashboard, table and tray.
- Toggle Available RAM between MiB and percentage, including with calibration
  and custom ranges; Apply, restart and confirm persistence.
- Compare Available RAM with `/proc/meminfo`; sub-MiB values display zero.
- Deselect all system readings and close Settings; periodic system reads stop.

Reference definitions:
https://www.kernel.org/doc/html/latest/filesystems/proc.html
https://www.kernel.org/doc/html/latest/admin-guide/cputopology.html
