# KSensors reference audit — phase 1

Reference: attached `ksensors-0.7.3.tar.gz`, authored by Miguel Novas
(`michaell@teleline.es`). Source headers specify GPL-2.0-or-later.
QSensors preserves its existing GPLv3 LICENSE unchanged. Selecting GPLv3 would
be compatible with reuse under those original headers, but no original source,
drawing algorithm, sound, translation or artwork is copied in this phase.
The application icon is newly drawn geometric artwork. Future reuse must
retain the relevant original copyright/license notices and update this record.

## Inspected modules

| Modules | Reference behavior / implementation decision |
| --- | --- |
| `main.cpp`, `lmsensorsdock.cpp` | KDE single instance, tray/menu lifecycle and dashboard toggle; replace with Qt lifecycle in phase 3. Drop direct X11 and desktop restoration. |
| `lmsensors.cpp`, `lmsensorschip.cpp`, `lmsensor.cpp` | Obsolete libsensors enumeration, prefix-only IDs and guessed feature types; implement a fresh modern backend in phase 2. |
| `sensor.cpp/.h`, `sensorslist.cpp` | Five-second group polling, correction, thresholds and units; separate descriptors, samples and preferences. |
| `lmsensorswidget.cpp`, `lmsensorpanel.cpp` | Small draggable grid tiles, persisted positions and alarm blinking; fresh scalable dashboard in phase 4. |
| `lmsensordockpanel.cpp` | Independently selected numeric icons; Qt tray controller in phase 3. |
| `lmsensorsalarms.cpp` | Alarm entry commands, process-global environment and repeating sound; independent state/action handling in phase 6. |
| `lmsensorscfg.cpp`, `generalcfg.cpp`, `systemcfg.cpp` | Global/per-sensor configuration; fresh Qt settings UI in phase 5. |
| `qdialarc.cpp`, `qlcdraw.cpp`, `qlcdstring.cpp` | Arc/needle trigonometry and ASCII LCD segments inspected; no geometry reused yet. |
| `procinfo.cpp`, `infopanels.cpp`, CPU/RAM/uptime panels | CPU/RAM/swap/uptime; replace unsafe buffers and old counters in phase 7. |
| `hdsensorslist.cpp`, `i8ksensorslist.cpp` | hddtemp, /proc/ide and hard-coded laptop/ACPI paths; do not port. |

## Phase-1 boundary

The provider interface exposes discovery and reads for requested IDs, returning
owned Qt value types. Samples carry explicit availability, timestamp and error.
The dashboard remains empty until the real hardware backend is implemented;
this foundation does not create polling timers or background work.
libsensors is discovered and listed as a target dependency now; actual calls
begin in phase 2. A linker using `--as-needed` may omit it from the phase-1
binary's dynamic dependencies because no API calls are made yet.

Qt 6.8 is the declared minimum target, not a tested compatibility claim.
No configure/build or runtime validation was performed for this patch.

## Uptime presentation reference

Inspected Miguel Novas's GPL-2.0-or-later `uptimepanel.cpp`, `qlcdstring.cpp` and
`qlcdraw.cpp` in KSensors 0.7.3 for the HH:MM layout, red foreground, darkRed
shadow and one-pixel shadow offset. QSensors follows that presentation behavior;
its normalized vector segment geometry and formatter are new implementations.
No source listings or glyph bitmap tables were copied. The days layout is a new
requested behavior beyond the original uptime panel.
