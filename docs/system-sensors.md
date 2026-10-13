# Optional system readings — phase 7

Phase 6 alarms are deferred. All five new numeric readings default to disabled;
select them in Settings for the dashboard or numeric tray. Their stable IDs start
with `system/`, independently of labels and CPU model. CPU model metadata is
attached to the two CPU readings and shown in their source heading, tile details
at larger sizes and tooltips. No separate text-only tile or per-core expansion is
introduced.

| Reading | Source and meaning |
| --- | --- |
| CPU activity | Aggregate `/proc/stat` counter deltas. `100 * (total - idle - iowait) / total`; total includes user, nice, system, idle, iowait, irq, softirq and steal. Guest counters are validated but not added again. Steal is counted as non-idle time, so this is activity rather than purely time executing work in a VM. |
| CPU frequency (reported mean) | Arithmetic mean of the readable `cpu MHz` entries in one `/proc/cpuinfo` snapshot, in MHz. This is a kernel-reported sampled estimate across listed logical CPUs, not a base frequency, cpufreq policy target, or measured effective work rate. If the architecture does not expose these entries, it is unavailable. |
| RAM usage | `(MemTotal - MemAvailable) / MemTotal * 100`. Older kernels fall back to free + buffers + cached + reclaimable slab − shared memory, bounded to total memory. The fallback is explicitly described in tooltips. |
| Swap usage | `(SwapTotal - SwapFree) / SwapTotal * 100`. Zero total gives a valid zero percentage with an explicit “No swap configured” state in Settings and tooltips. This does not measure zram compression or swap I/O. |
| Uptime | First `/proc/uptime` field, displayed as seconds. Parsing and formatting avoid 32-bit integer conversions. The second field is validated but not used as uptime. Duration tiles omit the gauge. |

CPU activity starts unavailable until two valid samples establish an interval.
Counter decreases (including iowait), a zero interval, malformed input or overflow
never yield a fabricated percentage; the next valid counters establish the new
baseline. Rescans reset the baseline. A batch without CPU activity demand also
clears it. RAM, swap and frequency are current snapshots rather than averages
over the polling interval. CPU model is read during discovery, not refreshed as a
label every tick.

## Work and failure boundaries

The existing serialized worker owns both providers. Requests are partitioned by
provider ID; hardware failure does not suppress system readings. All provider
results own their copied data. Each needed proc file is read once per batch,
shared by all consumers; RAM and swap share one meminfo read. File reads are
bounded to 4 MiB and reject truncation. Unselected system readings cause no
periodic proc reads; discovery reads CPU metadata once. Settings preview requests
the catalog temporarily, as it already does for hardware sensors.

No extra threads, polling timers, external commands, privileged helpers, per-core
controls or monitoring-history buffers are added. CPU and memory accounting are
host kernel figures; they are not container/cgroup quotas. Existing user
calibration remains available and is applied after these canonical readings.

## Parser fixtures and manual checks

The opt-in fixtures depend only on Qt Core and cover 64-bit counters, guest
accounting, counter decreases, delta overflow, unchanged intervals, missing
fields, malformed data/units, RAM fallback, no swap, frequency estimates and
large uptime values. They have not been compiled or executed for this patch.

To build and run them locally:

```sh
cmake -S . -B build -DQSENSORS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Manual checks:

- Open Settings and find CPU, Memory and System sources. All new selections should
  be off unless previously saved. The initial CPU activity reading needs a second
  interval; other supported readings should arrive immediately.
- Select the readings for tiles/table/tray; compare RAM with `free`'s available
  column and uptime with `/proc/uptime`. CPU activity may differ from tools using
  a different sampling interval; frequency is explicitly a reported mean.
- On a system without swap, inspect the explicit state in Settings and tooltips.
- Hide the dashboard with numeric system tray selections; values should continue.
  Deselect all system readings and close Settings; periodic system reads stop.
- Rename, restart and Rescan; IDs/selections remain stable and CPU activity safely
  establishes a fresh baseline. Check 48/128/512 sizes and system tray formatting.

Definitions were checked against the Linux kernel's proc documentation:
https://www.kernel.org/doc/html/latest/filesystems/proc.html and
https://www.kernel.org/doc/html/latest/admin-guide/cpu-load.html .
