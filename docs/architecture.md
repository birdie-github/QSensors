# QSensors architecture and dependencies

This page describes implementation details. The [README](../README.md) covers
features and everyday use.

## Dependency footprint

The application targets Linux and links Qt 6 Core, Gui, Widgets, and Network,
plus libsensors. Qt 6.8, CMake 3.21, and C++17 are the declared build minimums.
Qt Network supplies local single-instance IPC. There are no KDE libraries,
embedded browser, QML runtime, or application-specific monitoring daemon.
Qt's platform plugins and the desktop tray host provide native integration.

See [building.md](building.md) for installation prerequisites and commands.

## Source layout

| Directory | Responsibility |
| --- | --- |
| `src/app` | Application assembly, metadata, command line, single-instance IPC. |
| `src/sensors` | libsensors provider, demand scheduling, worker, raw-to-display presentation. |
| `src/system` | CPU, memory, swap, and uptime providers and parsers. |
| `src/settings` | Stable-ID preferences, sensor drafts, numeric-format policy, editors, and autostart. |
| `src/alarms` | Threshold/hysteresis state, persistence fields, actions, tests, and visual timing. |
| `src/tray` | Per-sensor numeric icons, shared menus, font fitting, caches, and host recovery. |
| `src/ui` | Dashboard, shared decimal editor, tile grid, and sortable sensor table model. |
| `src/rendering` | Vector geometry, digital glyphs, palettes, ranges, and value formatting. |
| `resources` | Embedded application icons and desktop entry. |
| `cmake` | libsensors discovery without requiring a vendor CMake package file. |

## Acquisition and presentation

One serialized worker owns the hardware provider and its library state.
Initialization, discovery, reads, rescan, and cleanup run there. Only owned value
snapshots cross into the GUI thread; backend pointers never become UI data.

A single-shot scheduler allows at most one acquisition request in flight. It
combines demand from the active dashboard presentation, selected tray sensors,
armed alarms, and settings previews. Each sensor is acquired at the fastest
interval required by its current consumers. Coincident requests are deduplicated
and batched. Missed ticks do not create a catch-up queue.

Dashboard and tray presentation deadlines are independent. Cached raw readings
are calibrated and converted for display without modifying the canonical sample.
The hidden dashboard presentation is not maintained at full display speed.
An off-screen tile adds no viewport demand; selected tray sensors and armed
alarms continue to contribute background demand. With no consumer, periodic
acquisition stops. Relevant proc files are shared within a requested batch.

Sensor preferences are keyed by provider identity rather than display labels.
Rename, calibration, and unit changes therefore preserve selections. A failed,
nonfinite, or invalid-timestamp reading is unavailable, never a fabricated zero.
Gauge display ranges are independent of alarm thresholds.

See [hardware-backend.md](hardware-backend.md),
[system-sensors.md](system-sensors.md), and
[sensor-settings.md](sensor-settings.md) for the detailed contracts.

## Rendering and resource use

Dashboard tiles are painted in one viewport using QPainter vector primitives,
shared paths, prepared readings, and cached styles. There is no widget or polling
timer per tile. Tile sizes are independent of the small embedded PNG application
icons. Rendering uses Qt Widgets; no particular GPU or Vulkan backend is required.

Tray text is rendered at the requested physical resolution. Font-fit and icon
image caches are bounded, and icon images are reused when the visible reading
and style have not changed. Alarm blinking retains the visible icon and swaps
in a transparent image without unregistering its tray slot. Alarm tests and
blinking use a timer only while needed; host availability has its own coarse
watchdog. These are design mechanisms for low overhead, not performance benchmarks.

## Alarms and application lifetime

Alarm evaluation consumes fresh calibrated readings separately from display
refreshes. A normal alarm episode runs notifications/commands on entry, retains
visual effects while active, and recovers using hysteresis. Invalid readings
suppress live visuals without fabricating a recovery. A per-sensor master flag
pauses alarms while retaining their complete configuration. Test Alarm uses an
expiring draft overlay and leaves the real episode latch intact.

Commands run asynchronously with one process per sensor at a time. Sensor
metadata is placed in each child environment rather than the process-global
environment. Machine-readable values retain their precision and decimal format;
the global decimal-separator preference controls UI formatting and editor parsing.
See [alarms.md](alarms.md).

A lock and local activation socket provide a single instance per user/session
environment. Qt owns tray registration, including host restarts. Shutdown removes
tray icons and waits for the serialized worker to finish and release libsensors.
Synchronous kernel/driver calls can delay that cleanup; threads are not forcibly
terminated. See [tray-lifecycle.md](tray-lifecycle.md).
