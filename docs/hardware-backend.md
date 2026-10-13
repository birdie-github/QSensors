# Hardware backend — phase 2

## Discovery and readings

QSensors calls libsensors directly, with its default configuration handling.
Labels, ignore directives and compute rules are handled by libsensors. It does
not run or parse the `sensors` executable and does not write hardware values,
execute `set` directives or need a privileged helper.

Typed readable inputs cover temperature (°C), fan (RPM), voltage (V), current
(A), power (W), energy (J) and humidity (%). Power input and average are separate
channels if both exist; averages are explicitly labelled. Optional threshold
attributes are not required. PWM, intrusion, VID, frequency and min/max/history
attributes are outside this phase. Channels with a failed/non-finite read remain
unavailable, with an error tooltip. Values remain in libsensors canonical units.

Labels are not identities. The Device column shows the complete chip name so
similar devices and duplicate labels remain distinguishable. Hovering either
identity column reveals the provider/channel ID.

## Identity and device changes

IDs combine the libsensors provider, chip prefix, bus type/address, resolved
physical sysfs path and subfeature name. Terminal `/hwmon/hwmonN` components
are removed. If no usable physical path resolves, the complete libsensors chip
name (including bus number and address) is the fallback. Components are escaped
so labels and path separators cannot silently merge settings groups.

This distinguishes same-prefix devices and does not depend on labels, feature
numbers or enumeration order. Physical port changes, bus renumbering embedded
in physical paths, driver/name changes, and fallback bus renumbering can still
change IDs; libsensors does not provide universal hardware serial identities.
Duplicate IDs are reported as an error rather than silently merged.

Explicit Rescan (F5) reinitializes libsensors/configuration and replaces the
catalog safely. Resume/long-hidden restoration triggers rescan after a gap;
read failures or an empty catalog trigger recovery scans with 10, 20, 40, 80,
160 and then 300-second backoff. Healthy reads do not rescan each tick.
New devices which do not cause any existing channel to fail require F5 or a
resume rescan; there is no udev subscription in this phase. With the dashboard hidden or minimized, polling and automatic recovery continue
only for tray selections. With no selections they pause; an in-flight batch
may finish after hiding. Newly available rows appear after rescan, removed rows
leave the view, and the application does not erase sensor preferences on rescan.
Per-sensor display/alarm preferences are implemented in phase 5.
`Monitoring/intervalSeconds` (1–3600, default 5) and `Tray/sensors` selections
are currently saved.

Disk temperatures are available when a kernel driver exposes them to
hwmon/libsensors. This does not cover every SATA or USB disk; privileged SMART
integration is not included.

## Threading and bounded scheduling

One QObject worker owns the backend on one QThread. Backend construction,
initialization, discovery, all reads, rescan and destruction/cleanup occur there.
Only copied snapshots cross to the GUI; every library-owned pointer stays in
the backend's channel map and is discarded before cleanup.

The GUI schedules at most one worker operation at a time. A single-shot timer
is armed after completion, so a slow read extends the interval instead of
queuing overlapping work. Multiple rescan requests coalesce into one follow-up.
The visible dashboard demands every displayed channel; tray selections add
background demand, including selected missing IDs for explicit unavailable
results and recovery. The worker reads the union once. Alarm demand follows
in phase 6 without adding competing readers.

Shutdown requests interruption, stops the worker event loop, waits for the
current call to finish, and destroys the backend on its owning thread. Work
checks interruption between chips/readings. libsensors calls are synchronous
and cannot safely be forcibly cancelled mid-call; a stuck kernel/driver read
can consequently delay exit. The GUI is not blocked during normal discovery or
polling. No thread termination is used.

## Validation status

The user confirmed phase 2 displays their real sensors and allows interval
changes. Phase 3 has only undergone static review and patch-application checks.
Hardware comparison, slow/failing reads, suspend/resume, removal/reappearance
and native X11/Wayland tray operation still need targeted hardware testing.
