# Settings and formatting — phase 5

## General and sensor settings

Settings now has two resizable pages. General chooses Celsius/Fahrenheit/Kelvin
for all temperature displays and optionally starts QSensors at desktop login.
Polling interval and tile size remain in the dashboard toolbar.

New sensors default to disabled for both dashboard and tray. A fresh configuration
therefore has an empty dashboard. Existing saved selections remain unchanged;
Disable all sensors clears both selections across the catalog (including search
results that are hidden), and takes effect on OK. Reset sensor preferences also
returns that sensor to disabled.

The dialog opens on Sensors at 1120 × 760 logical pixels, bounded to 90% of the
available screen. The resizable browser groups sensors under collapsible source
headings, naturally sorts source and display names (Core 2 before Core 18), and
keeps unavailable sensors in a separate group. Search matches display names,
original names, sources and stable IDs. Dashboard and tray checkboxes are directly
available in each sensor row. Display name contains editable text; Use original
name clears the override. Renames update the row immediately; the next catalog
refresh or dialog opening orders it by the new name.

Sensors lists the complete discovered catalog before any monitoring selection,
plus saved missing devices. Choose a sensor to edit independent dashboard/tray
selection, custom label, multiplier/offset, optional custom gauge bounds and
text/tray/tile-background colors. An empty label uses the libsensors label.
Reset color uses the system palette; a custom background with no custom text
color chooses contrasting black/white text. Explicit color combinations are
respected. Reset calibration restores multiplier 1 and offset 0; Reset sensor
preferences restores that sensor's defaults, including selections and colors.

OK applies the draft; Cancel discards it. Rescan preserves unapplied drafts and
updates the catalog; saved missing-device preferences are never removed merely
because hardware is absent. Duplicate labels are independent because IDs are
used for persistence. The empty tile view explains how to select sensors.
The table and tiles use the same dashboard selection. Tray selection stays
independent and works when the dashboard is hidden.

Alarm limits/actions, hysteresis and reference-based alarm presets are added
with the working phase-6 alarm engine. This page's gauge bounds are display-only
and do not produce alarms.

## Canonical values and display conversion

Hardware readings/configuration stay owned by libsensors on the existing
serialized worker. A small GUI presentation layer retains copied raw canonical
data and applies `value * multiplier + offset` once to fresh copies. It then
converts temperature values for display. The raw cached readings are never
rewritten using a displayed/converted value, so repeated preference edits or
unit changes cannot accumulate conversions.

Temperature calibration operates in Celsius. Absolute conversions are
`F = C * 1.8 + 32` and `K = C + 273.15`. Offsets are differences: a 10 °C offset
is 18 °F or 10 K, without adding 32 or 273.15. Custom gauge bounds are absolute
canonical values, converted for editing/rendering. Changing the General unit
reloads the controls without rewriting their canonical draft values. Only
actual numeric edits write those draft fields.

A missing device's type may be unknown after restart; its calibration and
bounds are shown in canonical units until rediscovery, as explained in the
editor. No feature-name/type guessing is introduced.

The editable multiplier range is ±1e6; offsets and gauge bounds are ±1e12 in
canonical units. Temperature editors adjust these bounds for the chosen unit.
These are editing/restore validation bounds, not hardware reading limits.
Malformed/non-finite restored values fall back safely. Invalid custom ranges
are disabled on restore and rejected on editing. Calibration/display overflow
produces Unavailable rather than a misleading number. Missing readings remain
missing even when an offset/multiplier is configured.

Tiles, table readings and tray tooltips share calibrated display values/units.
Automatic temperature gauge defaults are first computed in canonical Celsius,
then converted with their endpoints. User gauge bounds remain distinct from
alarm limits. Default custom colors are absent, so light/dark theme changes
continue to use Qt's palette; no application theme engine is added.

## Persistence and demand

QSettings stores per-sensor preferences under `Sensors/<escaped stable ID>` with
the exact ID also retained as a value. Labels never identify groups. Existing
`Tray/sensors` selections are preserved; the legacy selection list is kept in
sync for continuity. Global unit is `Display/temperatureUnit` (0=C, 1=F, 2=K).
Tile size/order/gauges/view/geometry and polling settings keep their existing
keys. Preferences are written on OK, not on measurements.

Only selected dashboard sensors are exposed by either dashboard view. Tiles
request those intersecting the viewport, table mode requests the selected
catalog, and tray selections add background demand. No-selection/no-tray mode
stops reading while keeping settings accessible. No new polling worker or
continuous repaint timer is introduced.

## Autostart

Autostart is opt-in and uses the standard user configuration autostart folder,
with `io.github.birdie_github.QSensors.desktop`. Exec contains the quoted current
executable path; the checkbox tooltip shows it. Spaces/backslashes/quotes,
dollar signs/backticks and literal percent characters follow desktop-entry
escaping rules; no shell is invoked. Paths with newlines or equals signs cannot
be represented and are rejected. If the executable is later moved, disable and
re-enable autostart to update its path.

Disabling writes a user `Hidden=true` entry, masking a same-name system entry.
There is no KDE-specific flag or system-wide installation. Files are replaced
atomically with QSaveFile; failures are reported and leave the dialog open.
Active sensor preferences update only after their settings sync succeeds. If
sensor settings cannot be saved after changing autostart, the dialog attempts
to restore the previous autostart state and reports any rollback failure.
Desktop-session launch behavior still needs real environment testing.

## Verification status

Static patch application, source/declaration/reference checks and manual review
of conversion, identity, invalid-data, selection, persistence and autostart
escaping paths were performed. No configuration, compilation, application
launch or runtime unit tests were executed. Test sensor selection, duplicate
labels, C/F/K changes with offsets/custom ranges, Cancel/OK/reset, light/dark
colors, restart/rescan/missing devices and autostart on actual hardware.
