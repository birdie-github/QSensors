# Per-sensor alarms

Configure Sensors → Alarms provides independent lower/upper limits, hysteresis,
and seven independently selectable actions:

- Change that sensor's dashboard tile background (overrides transparency while active).
- Change its table-row text color.
- Show a desktop notification.
- Run an executable with arguments.
- Draw thin horizontal lines above and below the tray value, spanning the rendered text width.
- Blink the tray icon (timing is configured globally in the INI file).
- Change the tray text color.

Tray blink timing is read once at startup from these global INI settings:

```ini
[Alarms]
trayBlinkOnSeconds=1
trayBlinkOffSeconds=0.75
```

On means visible; Off means hidden. Missing keys use these defaults without being written.
Redundant default timings from older versions are removed at startup.
Use decimal seconds with a dot. Each value must be finite and strictly greater
than 0.1 seconds; invalid values use that phase's default. Values are rounded to
milliseconds, with a minimum effective duration of 101 ms, and must fit Qt's
signed 32-bit millisecond timer range (at most 2147483.647 seconds). Restart
QSensors after editing. These timings also apply to Test Alarm; snooze still
suppresses blinking. Long phases wait for their deadline rather than polling
every second.

Per-sensor alarm persistence stores only fields that differ from `AlarmSettings`
defaults. Returning a field to its default removes its key on the next successful
settings save; older fully expanded configurations remain readable. An explicit
`enabled=false` is retained because the master switch defaults to true, even
though actions and limits default to off. This preserves paused alarms.

All actions and limits default to off. Enable at least one limit and action,
with the **Enable alarms** master switch on, to arm an alarm. The sensor browser's
Alarm column (between Tray and Value) shows whether any action is selected and
the master switch is on. It is a configuration indicator, not a live violation
indicator. Unchecking it pauses every automatic action while preserving limits,
action selections, colors, hysteresis, and the command. Checking it restores
those choices. Explicit enabling from either the column or the Alarms tab selects
Show a notification if no actions were selected. Both column-toggle directions
select/highlight that sensor and open its Alarms tab. Neither operation invents
a threshold: automatic triggering still requires a limit. Opening or resetting
an empty configuration leaves actions off, and both master indicators appear off
until an action is selected.

The master state is stored as `Alarm/enabled` (default true), so older alarm
configurations retain their behavior. The field participates in equality and
persistence through the shared flag inventory. Paused sensors add no alarm
monitoring demand, and applying a pause clears live alarm visuals. Master edits
use the existing Apply/OK/Cancel draft workflow. Test Alarm deliberately bypasses
the master switch and still runs checked actions when explicitly clicked. Alarms have their own limits, independent of gauge display bounds.
The Below, Above, and Hysteresis editors use the shared `DecimalSpinBox`
Standard policy: display and accept two decimal places.
Loading the dialog, changing units, or editing another field preserves the full
precision of untouched saved limits. Explicit numeric edits replace that value
with the edited two-decimal value.

They use calibrated canonical values (`raw * multiplier + offset`). Temperature
limits are edited in the selected C/F/K unit and retained in Celsius; hysteresis
is a temperature difference. Memory limits remain MiB even when the value is
displayed as a percentage. Uptime limits are seconds. Missing sensor types are
shown in canonical units until rediscovery. No hardware limits are guessed.

A low alarm begins strictly below its lower limit; a high alarm begins strictly
above its upper limit, matching KSensors 0.7.3. Recovery occurs at or beyond the
limit plus/minus hysteresis, toward the normal range. Hysteresis defaults to zero
and cannot exceed half the gap when both limits are enabled. Commands and
notifications run once on entry to an episode. They can run again after recovery
and a subsequent violation, or after changing/rearming the alarm settings.

Unavailable/nonfinite readings suppress live visual effects without resetting
the episode latch. A recovered sensor still outside its limits does not repeat
notifications/commands. Disappearance on rescan has the same behavior. Changing
thresholds, actions, or calibration resets that sensor's episode and waits for a
fresh reading; changing display units, fonts, or labels does not retrigger it.

Armed alarms contribute demand at the global interval even if every view is
hidden or the sensor has neither a dashboard tile nor a tray icon. Faster existing
acquisitions also evaluate alarms; the existing worker deduplicates readings.
Alarm state does not force value formatting/rendering at acquisition frequency.
No new acquisition worker is created. A single precise animation timer runs only
while a selected tray icon needs blinking or a test countdown is active.

## Tray and notifications

When the alarm changes the tray text color, Cast shadow uses a slightly darker
shade of that color (`QColor::darker(110)`) for both normal and digital text.
Outside that color override, the usual contrasting black/white shadow is used.
The shadow color is part of the icon cache key, so entry/recovery and tests
restore it even when normal and alarm text colors happen to match.

Tray actions apply to existing selected tray icons (enable them in the System
Tray tab). They do not create extra icons. Uptime does not support tray actions.
Blinking substitutes a transparent icon rather than unregistering it, preserving
its panel slot/menu. The visible icon is retained across blink phases. Markers
reserve narrow top/bottom bands so they remain separate from normal or digital
text; text is fitted directly at the remaining device resolution. Line endpoints
use the fitted font's ink bounds or the digital glyph geometry, rather than the
whole icon width. Bounds are measured during rendering and reuse the icon cache;
no image scanning or additional timer is needed. The existing `trayMarkers`
setting remains valid and now selects these horizontal lines.

Notifications format every available sensor value with exactly two decimal
places, using the selected decimal separator (for example, `97.12 %` or
`97,12 %`). This includes Test Alarm. The standard precision is shared with the numeric editor/tray formatting
policy; command environment values retain their existing machine precision.

Notifications use Qt's tray notification support and depend on the desktop host
and its notification policy. An unavailable host/support is reported, but a host
may accept a notification and suppress it (for example, Do Not Disturb).

## Commands

Executable and Arguments are separate fields. For example:

- Executable: `paplay`
- Arguments: `"/home/user/Sounds/alarm sound.wav"`

A plain executable name is resolved through PATH. Use double quotes around
arguments containing spaces; three consecutive quotes encode a literal double
quote (Qt's `QProcess::splitCommand` rules). No implicit shell, variable expansion,
pipes, or redirection is used. The child inherits the environment plus:

- `SENSOR_ID`: stable internal identity.
- `SENSOR_NAME`: configured or original label.
- `SENSOR_VALUE`: calibrated value in `SENSOR_UNIT`, C-locale numeric text;
  empty if no valid test reading is available.
- `SENSOR_UNIT`: selected temperature unit or canonical unit (MiB for memory).
- `SENSOR_TEST`: `1` for a test, otherwise `0`.

Execution is asynchronous, with at most one running command per sensor. An
attempt while its previous command is running reports an error. Input/output
are redirected to the null device, avoiding growing output buffers. Launch,
crash, and nonzero-exit errors appear in the alarm tab when that sensor is selected
and in the dashboard status bar. Commands are owned by QSensors and are terminated
on application exit. Commands already executed cannot be undone by Cancel.

## Test Alarm and persistence

Test Alarm runs every checked action using the current draft, even before limits
are enabled, without saving or changing the real alarm latch. It uses the latest
acquired raw reading and draft calibration. Colors, markers, and blinking last at
least three seconds with a countdown/progress bar, then restore the live alarm
state. Switching sensors, closing the dialog, or pressing Cancel does not shorten
an already started test. Test actions target existing tiles/table rows/tray icons.
Notifications are labelled Test Alarm; command execution is real.

The Alarms tab has a **Reset** button beside Test Alarm. It replaces only the
current sensor's alarm draft with `AlarmSettings{}`: both limits and all seven
actions disabled (both master indicators appear off), lower/upper values 0/100 in canonical units, zero hysteresis,
default colors, and empty executable/arguments. Other sensor settings and other
sensors are preserved. Apply/OK saves the reset and removes the default alarm
subgroup; Cancel discards it. A test already in progress keeps its original
three-second lifetime, and the live alarm remains unchanged until Apply/OK.

Alarm edits follow the existing Apply/OK/Cancel sensor draft behavior. Settings
are stored under each sensor's `Alarm/` subgroup. Invalid restored settings are
disarmed. Reset sensor preferences resets alarms as well. "Disable all sensors"
clears only dashboard/tray selections, not alarms; this is stated in its tooltip.

## Manual verification

No application compilation or execution was performed for this patch.

1. Enable all applicable actions and Test Alarm: verify the three-second countdown,
   per-sensor colors, markers, 1 s / 0.75 s blink, notification, and command.
2. Cancel draft edits, reopen, and confirm no saved changes. Apply/OK and restart
   should retain limits, actions, colors, and arguments. Reset should disarm.
3. Cross a limit; remain outside for several updates: one notification/command.
   Recover past hysteresis and cross again: a new episode. Test during a live
   alarm must restore that alarm's colors after the countdown.
4. Switch C/F/K with an open draft and after saving. Verify the same physical
   threshold and recovery margin. Check memory MiB limits in percentage mode.
5. Hide the dashboard and disable its tile/tray selection: an armed notification
   or command alarm must still trigger. Try an unavailable/reconnected sensor.
6. Test a quoted sound path, missing executable, nonzero exit, and a long-running
   command. Verify feedback and no overlapping instances for that sensor.
7. Verify tray normal/digital text, transparency, custom fonts, panel sizes/HiDPI,
   and host restart on the target X11/Wayland desktops.
