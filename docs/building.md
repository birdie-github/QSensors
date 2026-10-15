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
