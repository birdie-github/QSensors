# QSensors — Lightweight Linux Hardware Monitor

QSensors monitors **CPU temperature, fan speed, voltage, current, power, CPU usage
and frequency, RAM, swap, and uptime** on your Linux desktop. Choose the readings
you care about, arrange them on a customizable dashboard, or keep them visible as
individual numbers in the system tray.

Inspired by **KSensors**, QSensors brings its desktop sensor panels to modern
**Qt 6**, with **no KDE library dependencies**. It supports X11 and Wayland through
Qt, with a compatible desktop tray host for system tray features.

If you use [HWiNFO64](https://www.hwinfo.com/) or
[HWMonitor](https://www.cpuid.com/softwares/hwmonitor.html) on Windows, QSensors
provides a Linux home for everyday hardware sensor monitoring: live readings,
configurable alerts, and an at-a-glance view of your system's health.

## Why QSensors?

- **A dashboard that fits your desktop.** Arrange sensor tiles by dragging them,
  switch to a sortable table, or use a compact, always-on-top dashboard without
  menus and toolbars. Tile sizes range from 64 to 2048 pixels.
- **A separate tray readout for each chosen sensor.** Keep temperatures, CPU
  clocks, memory usage, or other readings visible while the dashboard is hidden.
  Choose fonts, colors, shadows, transparency, optional units, and independent
  update intervals. Remove an icon without losing its settings.
- **Alarms with independent actions.** Combine tile and table colors, desktop
  notifications, commands or sound playback, and tray lines, blinking, and text
  colors. Set lower/upper limits and hysteresis, pause alarms without clearing
  their configuration, and try them with a three-second Test Alarm countdown.
- **Your choice of appearance.** Rounded rectangles, square tiles, or circles;
  normal or digital readouts; optional gauges; separate sensor-name and data
  fonts; System, Classic KSensors, Graphite, and custom colors; solid, dotted,
  or grid backgrounds.
- **Useful sensor controls.** Search and rename sensors, adjust calibration and
  gauge ranges, choose Celsius/Fahrenheit/Kelvin, and use dot, comma, or system
  decimal separators. Settings stay attached to the sensor rather than its name.
- **Lean by design.** Native Qt Widgets, a small dependency footprint, shared
  sensor reads, and updates driven by the readings you actually use. Hidden
  dashboard views do not keep repainting; tray readings and enabled alarms can
  continue monitoring in the background.

## What can it monitor?

| Category | Readings |
| --- | --- |
| Hardware sensors | Temperature, fan speed, voltage, current, power, energy, and humidity exposed through Linux drivers and libsensors. |
| CPU | Overall usage, per-physical-core frequency, and average/maximum frequency across physical cores. |
| Memory | RAM used and available, plus swap usage, with amount and percentage views. |
| System | Uptime on the dashboard. |

Hardware coverage depends on your kernel drivers and libsensors. GPU and disk
sensors appear when those interfaces expose them; coverage varies by device.
QSensors reads sensor values; fan-speed control and privileged SMART access are
outside its scope.

## Getting started

QSensors runs on Linux with **Qt 6.8 or newer** and **libsensors**. It uses Qt's
Core, Gui, Widgets, and Network components and your desktop's X11 or Wayland
platform plugin. There is no KDE runtime, browser engine, or separate monitoring
daemon to install for QSensors itself.

For source installation, follow the [build and installation guide](docs/building.md).
Then launch `qsensors` from your application menu or terminal.

1. Open **QSensors → Configure Sensors**.
2. Check **Dashboard** and/or **Tray** for the sensors you want. New sensors start
   unselected, so the initial dashboard is empty until you choose them.
3. Select a sensor to customize its label, dashboard, tray readout, or alarms.
   **Apply** saves your edits; **OK** saves and closes; **Cancel** discards edits
   since the last Apply.
4. To set an alarm, check **Alarm**, choose the limits in the Alarms tab, and
   select its actions. Enabling an alarm with no actions selects notifications.
   Automatic alarms require at least one limit and action.

Use **View** for dashboard/table mode and appearance. **Settings** controls
refresh timing, temperature units, and Start at Login. Dashboard and tray updates
can have different intervals; the global interval accepts 0.10–999.99 seconds.

## Everyday controls

| Action | Control |
| --- | --- |
| Select dashboard tiles | Arrow keys; selection wraps through occupied tiles. |
| Configure the selected tile | Enter. |
| Move or swap a tile | Drag it, or use Ctrl + an arrow key in normal mode. |
| Toggle Compact Mode | Ctrl+Shift+M; double-click empty dashboard space to enter, or anywhere in compact view to leave. |
| Rescan hardware sensors | QSensors → Rescan Sensors, or F5. |
| Configure a tray sensor | Right-click its icon and select `[ Sensor name ]`. |
| Remove a tray icon | Right-click its icon → Remove Sensor. Its settings are retained. |

Closing the dashboard hides it when a system tray host is available. Use
**QSensors → Quit** or the tray menu to exit. Without a tray host, closing exits
and the dashboard remains the way to access the application. Native Wayland
movement, focus, and always-on-top behavior follow your compositor's policy.

## Why use QSensors instead of KSensors 0.7.3?

QSensors keeps the appeal of KSensors—small sensor panels and persistent tray
readings—while replacing its legacy desktop stack and expanding customization.

| Area | [KSensors5](https://sourceforge.net/projects/ksensors5/) SourceForge | QSensors |
| --- | --- | --- |
| Sensor backend | Legacy libsensors interfaces. | Current libsensors interfaces plus Linux CPU, memory, swap, and uptime readings. |
| Dashboard | Original draggable sensor panels. | Scalable tiles, three tile shapes, custom palettes/fonts, table view, and compact mode. |
| Alarm actions | A choice between sound playback and a command. | Independently combinable visual, notification, command, and tray actions, with hysteresis, pause, reset, and draft testing. |
| Charts | Present | Currently missing |

The **Classic KSensors** palette and digital readouts retain the original app's
visual character, with modern display sizes and desktop integration.

## More information

- [Sensor configuration and formatting](docs/sensor-settings.md)
- [Alarms, notifications, and commands](docs/alarms.md)
- [Dashboard controls and appearance](docs/vector-dashboard.md)
- [System readings and their meaning](docs/system-sensors.md)
- [Tray behavior and desktop compatibility](docs/tray-lifecycle.md)
- [Build and installation](docs/building.md)
- [Architecture and dependency details](docs/architecture.md)

## License and acknowledgments

QSensors is free software under the [GNU GPL version 3](LICENSE).
It is inspired by Miguel Novas's KSensors 0.7.3. The
[reference audit](docs/reference-audit.md) records the original application's
attribution and the implementation references.
