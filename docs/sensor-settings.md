# Sensor configuration and formatting

## Application settings and sensor configuration

The main menus are QSensors, View, Settings and Help. QSensors contains Configure
Sensors, Rescan Sensors, Hide Dashboard and Quit. View selects Dashboard/Table,
Compact Mode, Tile Size, Tile Style, Palette, Background and Decimal Separator. Compact Mode selects
Dashboard; selecting Table exits compact mode. Settings is also available from
all tray menus and shares the same temperature-unit, global-interval and Start
at Login actions. Menu choices apply immediately; interval dialogs save on OK.
The global interval remains available on the dashboard toolbar. Both controls
accept 0.10–999.99 seconds with two decimal places and 0.10-second arrow steps.
The stored value and signal/slot interfaces use floating-point seconds; deadlines
use rounded integer milliseconds, including previews (0.10 s → 100 ms,
1.25 s → 1250 ms, 999.99 s → 999990 ms). On load, finite saved values are clamped
and rounded to two decimals; malformed/non-finite values fall back to 5 seconds.
The existing `Monitoring/intervalSeconds` key also accepts old integer values.

Configure Sensors opens directly to the sensor browser/editor, with Properties,
Dashboard, (if supported) System Tray, and Alarms tabs. See [alarms.md](alarms.md)
for thresholds, actions, tests, and background monitoring. It has no outer General/Sensors
tabs. Its Apply/OK/Cancel behavior concerns sensor drafts. Global unit changes
refresh an open editor without replacing canonical calibration/range values or
other draft edits; Apply/OK always use the current global unit. Calibration
explanations are available on the Properties field labels.

New sensors default to disabled for both dashboard and tray. A fresh configuration
therefore has an empty dashboard. Existing saved selections remain unchanged;
Disable all sensors clears both selections across the catalog (including search
results that are hidden), and takes effect on Apply or OK. Reset sensor preferences also
returns that sensor to disabled.

The dialog opens directly to the browser/editor, sized for all five browser columns and the largest
fully populated sensor-editor tab, bounded to 90% of the available screen.
Splitter widths are assigned to those measured content widths, and both panes
remain accessible. Qt font/style metrics account for scaling. The editor
scrolls only when screen space is limited or the user deliberately narrows the
window/pane; long sensor lists still scroll vertically. The resizable browser groups sensors under collapsible source
headings, naturally sorts source and display names (Core 2 before Core 18), and
keeps unavailable sensors in a separate group. Search matches display names,
original names, sources and stable IDs. The columns are Source / sensor,
Dashboard, Tray, Alarm, and Value. Alarm is checked when at least one action is
selected and the sensor's alarm master switch is on; automatic triggering also
requires a configured limit. Unchecking Alarm pauses all actions without clearing
any alarm settings. Checking restores the retained configuration. Enabling when
no actions are selected automatically selects Show a notification, so the row
stays checked. Both checking and unchecking explicitly select/highlight that
sensor's row and open its Alarms tab, even when a different sensor was selected.
The Alarms tab's Enable alarms control uses the same enable operation and
reflects the same master/action state; paused choices remain visible but disabled.
Opening or resetting an empty configuration does not create an action. All edits follow
Apply/OK/Cancel. Test Alarm remains an explicit manual test even while paused.
Dashboard and tray checkboxes are directly available in each sensor row. The Value column shows live readings with applied
calibration and display units; unapplied edits do not change the preview. All
columns can be resized by dragging their header boundaries, including the last
column. Reading updates and rescans leave those widths unchanged. Double-click a
boundary to fit its contents. Display name contains editable text; Use original
clears the override. Renames update the row immediately; the next catalog
refresh or dialog opening orders it by the new name.

Sensors lists the complete discovered catalog plus saved missing devices.
Properties edits the display name, multiplier and offset. Use original changes only
the name; Reset calibration changes only multiplier/offset; Reset sensor
preferences restores all of this sensor's defaults, including both enabled states.

Dashboard and System Tray have independent enablement, timing, foreground/background
colors, transparency and shadows. Disabling either page deactivates its controls
without clearing any preferences. Dashboard text style is Normal or Digital,
independent of its Show gauge checkbox. Uptime supports both text styles but has
no gauge controls or Systray tab. Gauge bounds appear only when Show gauge is
checked; hiding them retains their saved values. Gauge defaults to on (except
Uptime); text defaults to Normal (Digital for Uptime). Only the new settings
keys are read; old presentation/global-gauge choices are ignored. There is no
compatibility or migration reader for them. The global gauge menu item is removed.
Available RAM's MiB/percentage choice remains shared with the tray and table.

System Tray → Appearance has a per-sensor **Show the unit** checkbox. It appends
`°` for Celsius/Fahrenheit temperatures (`K` for Kelvin), `%` for percentages or
humidity, `A` for current, and `V` for voltage. Other supported compact suffixes
are `W`, `J`, `s`, and `kRPM` (fan values already display thousands of RPM).
Memory amounts retain automatic M/G/T/P scaling (binary MiB/GiB/TiB/PiB); ordinary
RAM amounts therefore show `G`. Unchecking the option retains that scale and
uses the shared four-character numeric formatter, as do all other unit-free
non-frequency tray readings. It counts the decimal point and minus sign,
uses up to two decimal places that fit, and removes trailing zeros: examples
include `14.5`, `1.25`, `45.3`, and `1234`. Decimal separators follow `QLocale`,
so comma locales show `14,5`, `1,25`, and `45,3`. Separator localization occurs
before checking the character budget; grouping separators are not added.
Unit-bearing tray readings use the same separator helper. Font fitting measures
the resulting localized text, and locale-change events refresh tray entries. Rounding overflows retry with fewer
decimals. Very small/large values use compact scientific notation where it fits
(`1e-3`, `1e4`); otherwise the icon shows `ERR`, with the value in its tooltip.
Tiny nonzero numbers do not silently round to zero. Existing memory values
below one MiB still display zero. Fan readings remain in thousands of RPM.
Readouts with units retain their current precision rules. Full values/units remain in the tooltips. Frequency sensors hide
this option and retain their existing normal/digital formatting rules.

The setting is `trayShowUnit`. Existing memory-amount sensors default to enabled;
other sensors default to disabled, including available RAM already configured as
a percentage. Explicit choices are retained across format changes and restarts.
Unit glyphs participate in font fitting and its cache identity, including degree
and percent signs. Optional unit readouts fit their current digit/sign/decimal
pattern: `11%` and `88%` share a stable two-digit fit, while `100%`, negative
readings, and different decimal layouts use independent fits. This avoids
reserving four unused digits plus a unit, and prevents a longer exceptional
reading from permanently shrinking ordinary readings. Frequency and adaptive
memory fitting retain their existing rules. Saving, Cancel, and reset use the
ordinary sensor draft path.

Each sensor tray menu identifies its configured/original name as `[ Name ]`;
selecting that entry opens its System Tray settings. **Remove Sensor**
immediately saves only that sensor's tray enablement as off. Colors, font, units,
intervals, alarms, dashboard selection, and other preferences are retained.
Re-enable it in Configure Sensors to restore its icon with those settings.
If the settings dialog is already open, its tray checkbox follows removal while
other draft edits remain intact, so a later Apply/OK cannot undo the removal.
Cancel does not undo this separate menu action. Removing the last sensor restores
the existing application tray icon. Save errors leave the sensor selected and
are reported. The name brackets preserve literal ampersands as well.

The tray font uses a family selector and automatically fits the complete value
to the icon. Bitmap fonts use native glyph sizes; if no size fits, a scalable
fallback renders that value. Text is rasterized at the requested device
resolution without path conversion or glyph scaling. Font Reset restores the
system fixed font without changing colors, shadows or backgrounds. Cast shadow
draws a one-pixel contrasting offset behind the foreground, without an outline.
There is no manual font-size preference. Uptime's existing shadow stays enabled
by default; other dashboard sensors default to no shadow.

Reset color uses the system palette. A custom solid background with no custom
text color chooses contrasting black/white text. Transparent retains the saved
background color for later reuse. Explicit color combinations are respected.

Apply saves the draft without closing; OK saves and closes; Cancel discards
changes since the last successful Apply, including resets. Rescan preserves unapplied drafts and
updates the catalog; saved missing-device preferences are never removed merely
because hardware is absent. Duplicate labels are independent because IDs are
used for persistence. The empty tile view explains how to select sensors.
The table and tiles use the same dashboard selection. All dashboard table
columns can be resized by dragging their header boundaries, or fitted by
double-clicking a boundary. Clicking any header sorts that column and toggles
ascending/descending order. Values sort by calibrated numeric readings before
compact unit formatting; unavailable readings stay last in either direction.
The chosen sort stays active during readings, rescans and settings changes,
and manual column widths are not replaced by those updates. Tray selection stays
independent and works when the dashboard is hidden.

While Settings is visible, its value preview temporarily requests every detected
sensor through the existing worker at the configured update interval. Closing it
releases that demand, leaving dashboard/tray selections unchanged. Missing or
failed readings show Unavailable, with error details in a tooltip; readings that
have not arrived yet show Waiting. No last-good value is retained after failure.

Alarm limits/actions, hysteresis and reference-based alarm presets are added
with the working phase-6 alarm engine. This page's gauge bounds are display-only
and do not produce alarms.

## Decimal separator

View → Decimal Separator offers mutually exclusive Dot (.), Comma (,), and
System Default choices. System Default is the initial choice and follows Qt's
current default numeric locale. Dot/Comma use explicit ungrouped numeric locales,
so grouping cannot be mistaken for the chosen decimal separator. They affect
numeric display and editor parsing, without changing the application's dates,
language, or other system locale settings.

`NumberFormatSettings` caches the choice; `numberLocale()` supplies the shared
numeric policy to tile text/gauge labels, table/settings readings, tray values
and tooltips, numeric editors, alarm notifications, and test countdowns. The
existing tray separator helper uses this policy too. Digital commas include a
visible tail. Precision, measurement units, and frequency display rules are
unchanged.

Changing the choice immediately refreshes cached presentations. It does not emit
sensor preference changes or rearm alarms. Open numeric editors switch parsing
and display locales with edit signals blocked; canonical drafts remain intact.
The setting applies immediately and is independent of a sensor dialog's Cancel.

`[Display] decimalSeparator` stores `dot` or `comma`; System Default removes the
key. Unknown values also use System Default. Numeric settings and command data
(such as `SENSOR_VALUE`) keep machine-readable representations. Restart preserves
the choice. Check dot/comma/system switching, editor input with each separator,
restart persistence, and unchanged alarm state with an active alarm.

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
canonical values, converted for editing/rendering. Changing the global temperature unit
reloads the controls without rewriting their canonical draft values. Only
actual numeric edits write those draft fields. All floating-point editors use `DecimalSpinBox` (`src/ui/decimalspinbox.*`),
which owns display precision, locale formatting, deferred keyboard edits,
precision-preserving loads, and intentional same-value edits. Its Standard
policy displays/accepts two decimals. Its explicit Fine policy is used for
calibration, gauge ranges, and per-sensor intervals: it accepts three decimals
and shows the third only when nonzero. Alarm thresholds/hysteresis and both
global-interval controls use Standard. The menu's interval dialog uses the same
widget as the toolbar; individual pages do not call `setDecimals`.
Readout formatting (including compact tray/digital text) remains a separate
presentation policy in the rendering code.
Their internal spin-box rounding is never copied into the draft while loading.
Opening and accepting, changing units, visiting another sensor, or editing an
unrelated field preserves the full precision of untouched stored values. A
tooltip exposes the precise draft value in the current display units.

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

QSettings stores only non-default sensor preferences in separate top-level
`[Sensor-001]`, `[Sensor-002]`, ... sections. Each section has one readable `id`
value; the remaining keys are only overrides. Labels never identify sensors.
Resetting all preferences removes that sensor's section. A disabled sensor with
a custom label, color or calibration still has an override and is retained.
Missing devices retain their customizations until reset.

Only current sensor settings are read; there is no compatibility reader for
older gauge/presentation choices or shared tray colors. Missing keys use the
current defaults. The tray foreground is independent (`default` means the
system palette). Edit the file while QSensors is stopped. Qt may reorder keys/sections and
rewrite comments when saving; this is an editable settings file, not a comment-
preserving document. Sensor section names are regenerated on Apply/OK; identity comes
from `id`, not the section number. Duplicate identity sections should be avoided.

Example (all omitted options use defaults):

```ini
[Sensor-001]
id=system/cpu/activity
dashboard=true
label=CPU

[Sensor-002]
id=libsensors/coretemp:1:0:/sys/devices/platform/coretemp.0/temp1_input
tray=true
label=CPU Package
```

Optional `column` and `row` store the selected dashboard cell. An omitted position
is allocated into the first empty cell. Disabled sensors do not retain a cell.
The complete optional presentation keys are `dashboard=false`, `tray=false`, `label=`,
`multiplier=1`, `offset=0`, `foreground=`, `background=`, `customRange=false`,
`rangeLower=0`, `rangeUpper=100`, `memoryAsPercent=false`. These are their implicit defaults, not lines
that must be added. Colors accept Qt color strings, including `#RRGGBB` and
`#AARRGGBB`. Calibration and range values use canonical units as described above.
Available RAM exposes a display choice in Settings: automatic MiB/GiB/TiB/PiB
or percentage of total RAM. Corrections and custom range bounds remain canonical
MiB in either display mode; changing the choice does not rewrite those values.
Use the identity emitted for your machine; the hardware identity in this example
is illustrative. Slashes and colons in identity values are ordinary text.

`[Display] temperatureUnit` accepts `Celsius` (default), `Fahrenheit` or `Kelvin`.
`[Dashboard]` uses plain `width`, `height`, `x`, `y`, and `maximized` values;
X11 supports position restoration while Wayland controls placement. Selected dashboard sensors store `column` and `row` directly in their
`[Sensor-NNN]` sections. Field bounds are derived from these coordinates; there
are no separate Tile sections or saved dimensions. Layout saves preserve other
preferences, and an open Settings draft does not overwrite newer positions.
Deselecting a dashboard sensor releases its cell. `controlsVisible` remembers
menu/toolbar/status-bar visibility. Tile size and view remain
`tileSize` and `tableView`. The obsolete global `gauges` key is ignored and
removed on save. `[Monitoring] intervalSeconds` is unchanged.
Sensor preferences are written on Apply/OK, not on measurements. Opening Settings and
pressing OK without modifications does not dump the discovered catalog.

Only selected dashboard sensors are exposed by either dashboard view. Tiles
request those intersecting the viewport, table mode requests the selected
catalog, and tray selections add background demand. With no selected presentations, armed alarms still request readings. Reading
stops when there is also no alarm or preview demand. No new polling worker is
introduced; alarm tests and tray blinking use a timer only while active.

Each presentation can use global timing or a retained custom interval in
0.05–3600 seconds. The existing single-shot timer schedules independent
acquisition and presentation deadlines; the existing serialized worker reads
each sensor at the fastest cadence required by its active consumers (including
the settings preview). A slower presentation uses the shared latest cache when
its own deadline falls between acquisitions, rather than doing another hardware
read. Coincident requests are batched and deduplicated. Partial acquisition
batches replace only the channels read, including failures; untouched channels
and presentations keep their own last samples. Dashboard/table and tray updates
are restricted to due IDs. Global interval changes reschedule global consumers
without resetting custom presentation deadlines. Hidden/off-screen dashboard
consumers release demand; disabled consumers retain preferences but add no
periodic work. Discovery, recovery and Apply may force immediate refreshes.

Additional persisted keys (implicit defaults):

- `dashboardUseGlobal=true`, `dashboardInterval=5`, `dashboardGauge=true`
  (false for Uptime), `dashboardTextStyle=Normal` (Digital for Uptime).
- `dashboardShadow=false` (true for Uptime), `dashboardTransparent=false`.
- `trayUseGlobal=true`, `trayInterval=5`, `trayForeground=default`,
  `trayBackground=`, `trayShadow=true`, `trayTransparent=true`.
- `trayFontFamily=` (system fixed font).

Custom intervals, backgrounds and gauge limits are persisted even when their
corresponding global/transparent/gauge-off option is selected. Disabled dashboard
sensors release their cell; re-enabling fills the first available empty cell
and grows the rectangular field if needed.

## Autostart

Autostart is opt-in and uses the standard user configuration autostart folder,
with `io.github.birdie_github.QSensors.desktop`. Exec contains the quoted current
executable path; the Start at Login action tooltip shows it. Spaces/backslashes/quotes,
dollar signs/backticks and literal percent characters follow desktop-entry
escaping rules; no shell is invoked. Paths with newlines or equals signs cannot
be represented and are rejected. If the executable is later moved, disable and
re-enable autostart to update its path.

Disabling writes a user `Hidden=true` entry, masking a same-name system entry.
There is no KDE-specific flag or system-wide installation. Files are replaced
atomically with QSaveFile; failures are reported and the action checkmark is
restored to the actual autostart state. This application setting is independent
of pending sensor edits. Active sensor preferences update only after their
settings sync succeeds.
Desktop-session launch behavior still needs real environment testing.

## Verification status

Static patch application, source/declaration/reference checks and manual review
of conversion, identity, invalid-data, selection, persistence and autostart
escaping paths were performed. No configuration, compilation, application
launch or runtime unit tests were executed. Test sensor selection, duplicate
labels, C/F/K changes with offsets/custom ranges, Cancel/OK/reset, light/dark
colors, restart/rescan/missing devices and autostart on actual hardware.
