# Vector dashboard — phase 4

## Controls

Tiles are the default view. **View → Table View** restores the existing table.
The chosen view is saved. **Tile size** accepts any integer from 48 to 2048
logical pixels; its default is 128. Presets are 64, 96, 128, 192, 256, 512, 1024
and 2048, also available under View. Incomplete edits revert to the current
valid size when editing finishes. **View → Show Gauges** toggles the gauges.

Drag a tile before/after another tile to reorder it. A line marks the insertion
position. Near viewport edges, scrolling runs only during an active drag.
Arrow keys select adjacent tiles; Home/End select the first/last. Ctrl+arrow
or Ctrl+Home/End moves the selected tile before/after the target. Keyboard
navigation scrolls the selection into reach, including tiles larger than the
viewport. Reordering uses Qt drag-and-drop, without direct X11 window movement.

The grid wraps to the viewport width and provides horizontal scrolling when
one tile exceeds that width. All discovered supported channels are included;
per-sensor dashboard selection belongs to phase 5. Full Unicode labels,
device names, values and units remain available through tooltips; labels are
elided when necessary. Device text appears on the tile at sizes of 192 or more.
At small sizes the renderer omits gauge endpoint labels and uses a compact arc.

## Rendering and ranges

One QAbstractScrollArea paints only visible tiles intersecting the dirty paint
region. QPainter draws rounded faces, semicircular arcs, needles and text at
the actual logical tile size. Qt handles device-pixel ratio for the viewport;
there is no enlargement of a pre-rendered small tile. Font metrics fit values
and elide labels. The normal application palette controls contrast.

There are no per-tile widgets, full-grid images, per-tile pixmap caches, QQuick
widgets, custom Vulkan code or permanent repaint/frame timers. Fonts are shared
for the current size; reading text, range labels and fractions are prepared on
sample changes. Hidden/offscreen tiles do not paint. Changed readings dirty
only visible affected tile rectangles; gauge movement matters only when gauges
are enabled. Scrolling/resizing/dragging can repaint the affected viewport.

Gauge defaults are display guidance, not hardware/user alarm thresholds:

| Type | Initial display range |
| --- | --- |
| Temperature | −20–120 °C |
| Fan | 0–3000 RPM |
| Voltage | 0–16 V, or −16–0 V for negative readings |
| Current | 0–10 A |
| Power | 0–100 W |
| Energy | 0–1000 J |
| Humidity | 0–100% |

Ranges expand to accommodate outlying readings; displayed endpoint labels show
those bounds at sizes of 96 and above. They are not recommendations for safe
hardware operation and do not produce alarms. Zero RPM and negative readings
are valid. Missing/failed/non-finite/invalid-timestamp readings show an em dash
and no needle; they are never interpreted as zero. Invalid/zero-width/overflowing
range spans cannot produce an invalid needle calculation. Configurable ranges,
calibration, unit conversions and alarm thresholds belong to later phases.

## Demand, persistence and window geometry

The tile viewport publishes the IDs of tiles intersecting it. Only those IDs
plus tray selections are read; scrolling/resizing requests newly visible data
through the same coalesced serialized worker. Table mode requests the complete
catalog, preserving its existing behavior. Discovery obtains descriptors first,
then the chosen view supplies reading demand. No competing sensor reader is
introduced. A newly exposed tile can show unavailable briefly until its batch
returns. An already in-flight old-demand batch may finish; demand changes are
coalesced for the next batch.

QSettings saves `Dashboard/tileSize`, `Dashboard/gauges`, `Dashboard/order`,
`Dashboard/tableView` and `Dashboard/windowGeometry`. Order uses stable sensor
IDs, retains missing IDs, and appends newly discovered IDs. Resizing/wrapping
changes grid columns without changing that order. Missing channels return to
their saved relative positions when rediscovered. Settings are never written
on measurements.

Qt saveGeometry/restoreGeometry retain supported window geometry, with restored
size bounded to the current screen's available area. Exact global positioning
and virtual-desktop restoration are not required on Wayland. The application
has no custom top-level window dragging in this phase.

## Validation

The user confirmed phase 3 works. Phase 4 has static review and clean patch
application checks only; no configure/build or application execution was done.
Visual inspection is still required at 48, 128, 256, 512 and 2048 logical pixels,
normal/fractional/high DPI, light/dark palettes, native Wayland drag/drop,
large-tile horizontal/vertical scrolling, restart/rescan order persistence and
tray/table/tile demand transitions. CPU/memory performance remains unmeasured.
