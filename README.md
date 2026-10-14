# QSensors

A small Linux hardware-sensor monitor inspired by KSensors 0.7.3, rebuilt
with Qt 6 Widgets and modern libsensors. No KDE dependencies.

## Current status

Phase 7 adds optional system readings: aggregate CPU usage (including SMT), physical-core average/maximum/per-core
frequencies with CPU model metadata, RAM usage, available RAM, swap usage and
uptime. RAM/Swap usage have separate percentage and MiB readings. Available RAM supports MiB or percent display; frequency and memory
readings choose compact units automatically. Hardware and
system sensors use the same Settings, dashboard and numeric tray selections.
New sensors default to disabled. Settings groups sensors by source, supports
renaming and search, and previews live values in resizable columns. Calibration,
colors, custom gauge ranges, C/F/K units and opt-in desktop autostart remain
available. Each selected sensor has General, Dashboard and (when supported)
Systray tabs, with independent refresh timing and appearance. Apply saves the
current draft; Cancel discards changes since the last Apply. Numeric editors
show two or three decimals without reducing untouched stored precision.
Dashboard sensors independently select Normal/Digital text and gauge visibility.
Digital readouts use scalable vector segments; CPU frequencies use filled watch
segments, while temperatures retain one decimal and a square degree mark. Tile
source footers are omitted; sources remain in Settings, table rows and tooltips.
Phase 6 (alarms) is deferred.

Vector tiles default to 128 logical pixels; the toolbar accepts 16–2048, with
presets under View. Both Tile Size selectors offer Custom… for an integer-entry
dialog with OK/Cancel; Cancel leaves the current size unchanged. **View → Table View** retains the sensor table. Use Settings
to choose dashboard/tray sensors and **Sensors → Rescan** (F5) after hardware
changes. The global interval is 1–3600 seconds (default 5); presentation overrides
accept 0.05–3600 seconds. A single timer schedules acquisition at each sensor's
fastest active interval, with separate presentation deadlines using its cache.
Tile mode requests visible selected tiles plus tray selections; table mode
requests selected dashboard sensors. Tile positions are fixed across window resizing; dragging can grow the
rectangular field. New sensors fill its first empty cell. Double-click empty
space or press Ctrl+Shift+M to show/hide menu, toolbar and status bar.
Closing hides when a tray host exists; **Quit** exits. Without a tray
host the dashboard stays recoverable and closing exits.

The minimum target is **Qt 6.8**. Phase 7 has only been statically reviewed,
without configuring, compiling or running it. See
[system-sensors.md](docs/system-sensors.md),
[sensor-settings.md](docs/sensor-settings.md),
[vector-dashboard.md](docs/vector-dashboard.md),
[hardware-backend.md](docs/hardware-backend.md) and
[tray-lifecycle.md](docs/tray-lifecycle.md) for controls, architecture and limitations.

## Prerequisites

- Linux, CMake 3.21 or later and a C++17 compiler.
- Qt 6.8 or later development files for Core, Gui, Widgets and Network.
- libsensors development files (`sensors/sensors.h` and `libsensors`).
- Qt's xcb or Wayland platform plugin for your desktop.

Typical Fedora packages: `cmake gcc-c++ qt6-qtbase-devel lm_sensors-devel`
and `qt6-qtwayland` for Wayland. Debian-family package names include
`cmake g++ qt6-base-dev libsensors-dev qt6-wayland`; ensure the distribution
actually supplies Qt 6.8 or later, or use a separate Qt installation.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/qsensors
```

For a separate Qt installation, pass `-DCMAKE_PREFIX_PATH=/path/to/Qt/prefix`.
`--help` and `--version` are supported. Qt selects the platform plugin normally;
explicit native checks can use `QT_QPA_PLATFORM=xcb` or
`QT_QPA_PLATFORM=wayland` when those plugins and desktop sessions are available.
This does not claim either environment has been tested.

```sh
cmake --install build --prefix /chosen/prefix
```

Installation uses GNUInstallDirs for the executable, desktop entry, icon and
license. A custom prefix's `bin` and data directories must be discoverable by
your desktop (`PATH` and `XDG_DATA_DIRS`). CMake's `build/install_manifest.txt`
lists installed files; no custom uninstaller or privileged helper is provided.

## Architecture

- `src/app`: metadata, command line, application assembly and single-instance IPC.
- `src/sensors`: libsensors backend, serialized worker/demand scheduler and raw-to-display presentation.
- `src/system`: bounded proc reads and independent CPU/memory/uptime parsers.
- `src/settings`: stable-ID preferences, settings dialog and desktop-neutral autostart.
- `src/tray`: shared actions, numeric icons, host recovery and selection dialog.
- `src/ui`: persistent dashboard, virtual tile grid and table model. The model rejects missing,
  non-finite or invalid-timestamp readings and does not present stale values.
- `src/rendering`: vector tile geometry/fonts, type-aware display ranges and formatting.
- `resources`: embedded PNG icon resolutions and desktop integration.
- `cmake`: libsensors discovery without requiring a vendor package file.

Provider calls have one owner/execution context. Hardware initialization,
discovery, batched reads and cleanup use one serialized worker; only copied
snapshots cross to the UI. A single completion-driven timer prevents overlap.
Add rendering, tray, settings and alarm modules when their phases need them;
there are no placeholder service layers or unused module trees.

Widgets use QPainter vector primitives for dashboard tiles; the small PNG
application icons are unrelated to tile rendering. Vulkan acceleration is not
promised. Tray support will require a compatible desktop host; libsensors/hwmon
disk-temperature coverage will not include every SATA or USB disk.

## License and reference

QSensors uses the repository's GPL version 3 license; see [LICENSE](LICENSE).
See [docs/reference-audit.md](docs/reference-audit.md) for KSensors authorship,
reference inventory and the record of material reused in this phase.
