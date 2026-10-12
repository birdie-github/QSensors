# QSensors

A small Linux hardware-sensor monitor inspired by KSensors 0.7.3, rebuilt
with Qt 6 Widgets and modern libsensors. No KDE dependencies.

## Current status

Phase 2 discovers real libsensors channels and displays readings in a table,
with device identity and explicit unavailable states. Initialization, discovery,
reads and cleanup run on one serialized worker. No fake provider is included.

Use **Sensors → Rescan** (F5) after hardware changes. The toolbar controls the
update interval (1–3600 seconds, default 5), saved between launches. Polling
pauses when hidden or minimized. Failures trigger bounded recovery scans.
Closing the window currently exits; tray and single-instance support follow
in phase 3. Scalable dashboard tiles, sensor settings and alarms follow later.

The minimum target is **Qt 6.8**. Phase 1 was built and launched by the user;
its exact Qt version was not recorded. Phase 2 has only been statically reviewed,
without configuring, compiling or running it. Native X11/Wayland validation
remains pending. See [hardware-backend.md](docs/hardware-backend.md) for supported
channels, identity/recovery details and limitations.

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
- `src/sensors`: libsensors backend, copied descriptors/samples and serialized
  worker/demand scheduler.
- `src/ui`: small window and temporary table model. The model rejects missing,
  non-finite or invalid-timestamp readings and does not present stale values.
- `resources`: embedded PNG icon resolutions and desktop integration.
- `cmake`: libsensors discovery without requiring a vendor package file.

Provider calls have one owner/execution context. Hardware initialization,
discovery, batched reads and cleanup use one serialized worker; only copied
snapshots cross to the UI. A single completion-driven timer prevents overlap.
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
