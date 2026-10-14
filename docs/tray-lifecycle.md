# Tray and lifecycle — phase 3

## Usage

The dashboard opens on startup. Settings (from File, the toolbar or any tray
icon) selects dashboard/tray sensors and configures labels, calibration,
units, ranges and colors. Alarm controls follow in phase 6. Changes apply on OK, Cancel leaves
selections unchanged, and selections are stored with readable stable IDs in `[Sensor-NNN]` sections.
An open Settings window preserves unapplied checkboxes across a rescan.
Temporarily missing selected sensors remain checked until explicitly removed.

One QSystemTrayIcon represents each selected sensor. No selection produces one
ordinary application icon. Each sensor icon's menu has a direct, bold, enabled reading
entry using only its configured display name (or original name). It is marked
as the default menu action and requests a bold font; actual emphasis depends
on the menu style and tray host. Selecting it opens that sensor's Systray settings, reusing an open dialog
and retaining unapplied edits. Unavailable sensors remain configurable.
Tooltips show devices, full readings/units and availability. Temperature/humidity use integers,
fans use thousands of RPM with one decimal (the tooltip explicitly uses RPM),
and voltage uses one decimal. Other types use compact significant digits;
large values use scientific notation. Unavailable channels show an em dash.

Frequency sensors offer Normal and Digital styles in the Systray tab. Digital
uses the dashboard's filled seven-segment digits, scaled to the icon, with no
unit suffix. Readings from 1 through 9999 MHz are rounded to whole MHz;
values outside that inclusive range show a digital ERR (seven-segment Err).
Unavailable readings retain the dash. The font selector is disabled in Digital
mode, but its choice is retained for Normal mode. Color, background and the
one-pixel offset shadow apply to both styles. Tooltips retain grouped raw Hz.

Transparent numeric icons are painted directly at 16, 22, 24, 32, 48, 64 and
128 physical pixels. Text has a contrasting outline for legibility on different
panel backgrounds. This is a bounded resolution set independent of dashboard
size. Icon images change only when compact text, colors or font parameters
change; full-value tooltip/menu text can update independently. Settings selects per-sensor text/tray colors; invalid colors use the application
palette. Alarm colors/actions follow in phase 6.

Every icon shares Show Dashboard, Hide Dashboard, Settings, Rescan, About and
Quit actions with the application menus. Single activation toggles the dashboard
where the host delivers it; there is no dependency on double-click or wheel
support. Explicit Show/Hide actions remain available through the context menu.

## Visibility and recovery

Closing with a tray host hides the persistent dashboard. File/Tray Quit always
exits, including from an open Settings window. Selected tray channels keep
updating in the background; with none selected, hiding/minimizing stops sensor
polling. Showing restores the chosen view's demand and refreshes stale values:
intersecting selected tiles, or the selected dashboard catalog in table mode.
Tray and dashboard share one hardware worker; there are no competing readers.
Settings currently opens with the dashboard visible and lists cached catalog
metadata; it does not require extra preview readings. Alarms will add background demand in phase 6.

If there is no tray host, the dashboard explains it and closing exits. Hide is
disabled. A small shared, coarse three-second host watchdog detects host loss
and restores a hidden/minimized dashboard. It does not repaint or regenerate
icons at each check. This is separate from the sensor polling interval.
QSystemTrayIcon stays logically visible and handles registration when a host
appears/restarts; existing objects are reused rather than duplicated.

On X11 Qt may use an XEmbed tray or a StatusNotifierItem host; native Wayland
requires a compatible StatusNotifierItem host and suitable Qt platform support.
Host feature support varies, and compositor focus policy may prevent an
activation request from forcing focus. No direct X11 or KDE APIs are used.

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

## Validation status

The user confirmed phase 3 works. Phase 4 is statically reviewed and checked
for clean patch application, without compilation or application execution. Real X11 and native Wayland tray tests,
fractional/high DPI, multiple numeric icons, host restart/loss, activation,
concurrent launches and stale-endpoint recovery remain pending.
