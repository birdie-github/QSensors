# Tray and application lifecycle

## Usage

Configure Sensors (from QSensors, the toolbar, or a tray icon) selects dashboard
and tray sensors and configures their labels, calibration, appearance, and alarms.
Global temperature units, refresh interval, and Start at Login are available in
Settings. Apply saves edits; OK saves and closes; Cancel discards edits since the
last Apply. Preferences use stable sensor IDs in `[Sensor-NNN]` sections.
An open configuration dialog preserves unapplied edits across a rescan.
Temporarily missing selected sensors remain checked until explicitly removed.

One QSystemTrayIcon represents each selected sensor. With none selected, an
ordinary application icon remains. Each sensor menu contains an enabled
`[ Sensor name ]` entry using the configured display name, or the original name.
Selecting it opens that sensor's System Tray tab, reusing an open dialog and
retaining unapplied edits. Remove Sensor removes only the tray selection and
keeps its settings. Unavailable sensors remain configurable.

Tooltips show devices, full readings/units, and availability. Compact icon values
use the shared [formatting rules](sensor-settings.md); unavailable channels show
an em dash. Optional units are supported for non-frequency sensors. Frequency
has its own rules, described in [system sensors](system-sensors.md).

Frequency sensors offer Normal and Digital styles. Digital uses filled
seven-segment digits with no unit suffix. Readings from 1 through 9999 MHz are
rounded to whole MHz; values outside that inclusive range show ERR. Unavailable
readings retain the dash. The font selector is disabled in Digital mode, but its
choice is retained for Normal mode. Color, background, and the optional
one-pixel offset shadow apply to both styles. Tooltips retain grouped raw Hz.

Icons are painted at a bounded set of physical resolutions, independent of tile
size. Image and font-fit caches avoid unnecessary rendering. Icon images change
when visible text or style changes; full-value tooltips can update independently.
Alarms can add horizontal lines, blink the icon, and change its text color.
Blinking swaps in a transparent image without unregistering the tray slot.
When the alarm changes the text color, its shadow uses a darker version of that
color. See [alarms](alarms.md).

Every tray menu has one dashboard action: Hide Dashboard while visible, or
Show Dashboard while hidden or minimized. Configuration, Rescan, About, and Quit
remain accessible. Single activation toggles the dashboard where the host
delivers it; explicit Show/Hide actions remain in the context menu.

## Visibility and recovery

Closing with a tray host hides the dashboard. Quit always exits, including from
an open configuration dialog. Tray selections and armed alarms keep requesting
readings in the background. Settings previews temporarily request the catalog.
With no consumers, periodic acquisition stops. Showing restores the active view's
demand: intersecting selected tiles, or the dashboard catalog in table mode.
All consumers share one serialized worker.

If there is no tray host, the dashboard explains it and closing exits. Hide is
disabled. A shared, coarse three-second watchdog detects host loss and restores
a hidden/minimized dashboard. It does not repaint or regenerate icons at each
check. QSystemTrayIcon handles registration when a host appears or restarts;
existing objects are reused.

On X11 Qt may use an XEmbed tray or a StatusNotifierItem host; native Wayland
requires a compatible StatusNotifierItem host and suitable Qt platform support.
Host features vary, and compositor focus policy may prevent activation from
forcing focus. No direct X11 or KDE APIs are used. Verify tray availability,
activation, scaling, host restart/loss, and multiple icons on the target desktop.

## Single instance

A QLockFile lifetime lock and QLocalServer/QLocalSocket activation handshake
allow one instance per user and graphical-session environment. The key hashes
XDG_SESSION_ID, WAYLAND_DISPLAY and DISPLAY; changing those variables may yield
an independent session key. IPC files live in the user-private RuntimeLocation,
with a restricted application-data fallback when unavailable.

Only the lifetime-lock owner removes an old socket and starts a listener.
QLockFile handles dead owners; timestamp expiry is disabled so a live process
cannot lose ownership merely because it has been running for a long time.
Concurrent launches retry for roughly three seconds and ask the primary to
show the dashboard. An unresponsive live primary produces an error rather than
a second tray instance. `--help`/`--version` exit before instance acquisition.
IPC permits only a small activation message, at most eight active connections,
and a 1.5-second connection lifetime. There is no general command interface.

Shutdown deletes tray icons before their shared menu, waits for the serialized
sensor worker to finish/clean up, then releases the IPC endpoint and lock.
A synchronous stuck driver read can still delay orderly shutdown as described
in hardware-backend.md; no thread termination is used.
