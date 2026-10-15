# Building and installing QSensors

## Requirements

- Linux, CMake 3.21 or newer, and a C++17 compiler.
- Qt 6.8 or newer development files: Core, Gui, Widgets, and Network.
- libsensors development files: `sensors/sensors.h` and `libsensors`.
- Qt's xcb or Wayland platform plugin for your desktop session.

QSensors does not require KDE libraries. Qt and libsensors may pull in their
normal platform dependencies through your distribution's packages.

Typical Fedora package names are `cmake`, `gcc-c++`, `qt6-qtbase-devel`, and
`lm_sensors-devel`, with `qt6-qtwayland` for Wayland. Debian-family package names
include `cmake`, `g++`, `qt6-base-dev`, `libsensors-dev`, and `qt6-wayland`.
Check that the available Qt version meets the 6.8 minimum; package names alone
do not guarantee it. A separate Qt installation can be used where necessary.

## Build and run

From the repository directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/qsensors
```

For a separate Qt installation, add `-DCMAKE_PREFIX_PATH=/path/to/Qt/prefix` to
the first command. QSensors supports `--help` and `--version`.

Qt normally selects the platform plugin for the session. To select one
explicitly, use `QT_QPA_PLATFORM=xcb` or `QT_QPA_PLATFORM=wayland` when that
plugin and the corresponding desktop session are available. System tray support
also requires a compatible host; see [tray behavior](tray-lifecycle.md).

## Install

```sh
cmake --install build --prefix /chosen/prefix
```

Use a prefix you can write to, or arrange the necessary installation permissions
for a system prefix. Installation includes the executable, desktop entry,
application icons, and license using CMake's GNUInstallDirs conventions.

With a custom prefix, make its `bin` and application-data directories discoverable
through `PATH` and `XDG_DATA_DIRS` as appropriate for your desktop. Launching from
the terminal can work even when the application menu has not discovered the
custom prefix.

`build/install_manifest.txt` lists installed files. There is no custom uninstaller.
Start at Login is configured from the application Settings menu and records the
current executable path; toggle it again if the executable is moved.

## Hardware access

QSensors reads hardware channels through libsensors and system readings through
Linux proc/sysfs interfaces. It uses the current user's permissions. Missing
kernel-driver support or unreadable attributes can limit the available sensors.
It does not install a privileged helper or configure kernel modules. See
[hardware backend](hardware-backend.md) for coverage and error handling.

## Project metadata

`project.json` is the source for the application identity, version, release date,
description, repository, SPDX license identifier, and platform requirements.
CMake reads it and generates `projectmetadata.h` and the installed desktop entry.
Editing it causes CMake to reconfigure on the next build. There is no runtime JSON
loading or dependency on an external metadata file; an informational copy is
installed under `share/QSensors`.

Update `release.version` and `release.date` when preparing a release. The current
version remains 0.1.0. Keep `identity.name`, `identity.organization`,
`identity.organization_domain`, and `identity.bundle_namespace` stable: they
identify existing settings, desktop integration, and autostart entries.

## AppImage CI

`.github/workflows/ci.yml` adapts QLinuxDeviceManager's Linux AppImage workflow.
Pushes, pull requests, and manual runs build a Release executable on Ubuntu 24.04
with Qt 6.8.3, stage it through the normal CMake install rules, and package it
using linuxdeploy and its Qt plugin. The package includes libsensors and Qt's
X11 and Wayland platform plugins; hardware access and tray-host requirements
remain the same as for a native installation.

Download the artifact from the workflow run's **Artifacts** section. It contains
`QSensors-<version>-Linux-x86_64.AppImage`, with the version taken from
`project.json`. Artifacts are retained for 14 days; the workflow does not publish
GitHub releases. This is an x86_64 build with an Ubuntu 24.04 userspace baseline,
not a promise of compatibility with older distributions.

Mark the AppImage executable before running it. Start at Login records the outer
AppImage path rather than its temporary mount; toggle it again if you move or
rename the AppImage. The package still reads the host's libsensors configuration
and sensors exposed by the host kernel.
