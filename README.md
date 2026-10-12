# QSensors

A small Linux hardware-sensor monitor inspired by KSensors 0.7.3, rebuilt
with Qt 6 Widgets and modern libsensors. No KDE dependencies.

## Current status

Phase 1 provides application metadata, an embedded icon, a usable empty window,
a minimal provider boundary and a table model ready for real sensor readings.
This foundation does not read sensors yet. Hardware discovery
and serialized worker polling follow in phase 2; system tray support and
single-instance behavior follow in phase 3. Closing the window currently exits.
Scalable QPainter dashboard tiles, settings and alarms follow in later phases.

The minimum target is **Qt 6.8**. This initial patch has only been statically
reviewed: no Qt version has yet been verified by configuring, compiling or
running it. X11/Wayland and tray-host validation remain pending.

## Prerequisites

- Linux, CMake 3.21 or later and a C++17 compiler.
- Qt 6.8 or later development files for Core, Gui and Widgets.
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

- `src/app`: metadata, command line and application assembly.
- `src/sensors`: copied descriptors/samples, discovery and batched-read interface.
- `src/ui`: small window and temporary table model. The model rejects missing,
  non-finite or invalid-timestamp readings and does not present stale values.
- `resources`: embedded PNG icon resolutions and desktop integration.
- `cmake`: libsensors discovery without requiring a vendor package file.

Provider calls have one owner/execution context. Phase 2 must place hardware
initialization, discovery, batched reads and cleanup on one serialized worker;
only copied data crosses to the UI. Development targets real hardware readings.
Add rendering, tray, settings and alarm modules when their phases need them;
there are no placeholder service layers or unused module trees.

Widgets will use QPainter vector primitives for dashboard tiles; the small PNG
application icons are unrelated to tile rendering. Vulkan acceleration is not
promised. Tray support will require a compatible desktop host; libsensors/hwmon
disk-temperature coverage will not include every SATA or USB disk.

## License and reference

QSensors uses the repository's GPL version 3 license; see [LICENSE](LICENSE).
See [docs/reference-audit.md](docs/reference-audit.md) for KSensors authorship,
reference inventory and the record of material reused in this phase.
