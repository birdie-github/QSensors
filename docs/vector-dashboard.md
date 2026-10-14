# Vector dashboard — phase 4

## Controls

Tiles are the default view. **View → Table View** restores the existing table.
The chosen view is saved. **Tile size** accepts any integer from 64 to 2048
logical pixels; its default is 128. Presets are 64, 96, 128, 192, 256, 384, 512,
and 1024, also available under View. Both the View → Tile Size menu and toolbar
selector have Custom…: an integer dialog initialized to the current size,
limited to 64–2048 with step 1, and only OK/Cancel buttons. OK applies and saves
the size; Cancel leaves it unchanged. Direct toolbar edits remain available;
incomplete edits revert to the current valid size when editing finishes. Each sensor's Dashboard settings independently
choose Normal/Digital text and Show gauge; Uptime has no gauge. The previous
global gauge menu item is replaced by these per-sensor choices.

The dashboard is a fixed rectangular field of square cells. Resizing the window
changes the visible area and scrollbars, never tile positions or the field's
column count. Changing tile size preserves cell coordinates. An empty field
has one cell. If selected sensors have no saved coordinates, the initial layout
uses `rows = floor(sqrt(N))` (at least one) and `columns = ceil(N / rows)`.
For example, six sensors start in three columns and two rows. This is initial
placement, not a migration or reflow: existing coordinates are left intact.
Enabling a sensor in an existing layout fills the first unoccupied cell
in row-major order (top to bottom, left to right). If all cells are occupied,
the field gains a row and uses its first cell. Empty cells are dotted outlines,
not sensor objects. Temporarily missing selected sensors retain visibly unavailable cells until
rediscovery or deselection. No cached value is shown as current.

Hold the left mouse button on a tile and move it to a new cell. A translucent
preview shows the destination. Dropping on an occupied cell swaps the two tiles.
Dragging beyond any field edge expands the rectangle with empty cells; adding
left/top cells does not alter existing sensor coordinates. Edge scrolling runs
only during an active drag. Release commits the move; Esc, hiding, deactivation
or a catalog change cancels it and restores the previous layout. After a move
or cancellation, empty outer rows/columns are removed. The resulting rectangle
is bounded by the outermost occupied cells; internal gaps remain. Deleting or
deselecting a border tile also trims the field. A completely empty dashboard
keeps one cell. After trimming, all coordinates are shifted together so the occupied rectangle
starts at column 0, row 0. Relative tile positions and interior gaps stay
unchanged, and the trimmed bounds are
saved. Input uses Qt
widget mouse events/implicit mouse capture, without native X11 window movement.

Left/Right select tiles in row order, continuing into the previous/next occupied
row. Up/Down use column order, continuing into the previous/next occupied column.
Empty cells are skipped and navigation wraps at either end of the field. Enter
(including keypad Enter) opens Settings at the selected sensor. Ctrl+arrow
moves/swaps the selected tile one cell and expands the field if necessary.
The selected tile scrolls into reach. Layout extents are checked against integer
pixel-coordinate capacity at the maximum supported tile size; empty cells are
never individually allocated.

Double-click empty space (including gutters between tiles) to hide controls.
In compact view, double-click anywhere, including on a sensor, to restore controls.
Hiding controls switches to a frameless, always-on-top window sized to the tile
field. The menu, toolbar, status bar and table view are hidden. This view has a
fixed size and disables mouse and keyboard tile rearrangement. Left-dragging a
tile or empty space requests native window movement through Qt; X11 also has a
manual movement fallback. Wayland movement and stacking are compositor-managed.
Clicking a tile also selects it and focuses the grid for keyboard navigation.
Compact windows match the complete field even when it exceeds the screen.
Compact view never has scrollbars or scroll offsets. Tile size or sensor selection
changes refit the compact window. Showing controls restores the previous normal
window geometry, maximized state and chosen tile/table view. The controls state
is saved; compact dimensions do not overwrite the normal window dimensions.
Compact sizing is reapplied after show/layout events so startup does not retain
stale central-widget dimensions or transient scrollbars.
**Ctrl+Shift+M** toggles controls even in table mode.

### Dashboard colors and backgrounds

**View → Dashboard Palette** offers System (the default), Classic KSensors,
Graphite, and Custom. Classic uses the original 0.7.3 white/black panel colors,
green/red gauge bands, and blue/teal voltage bands. Graphite uses charcoal panels
with amber selection and needles. Palettes affect dashboard tiles in both normal
and compact views; menus, the table view and tray keep their own appearance.

Classic's gauge colors reproduce the original RGB values, but band positions
are fixed proportions of the displayed range, not hardware alarm thresholds.
Fans put the end band at the low end; voltage uses it at both ends. Other gauges
put it at the high end. Unavailable readings use the plain gauge track.

**Custom Colors…** starts with the currently selected palette. It edits canvas,
tile, text, unavailable text, border, selection, pattern (including opacity),
gauge track, needles, uptime digits, and the main/transition/end gauge bands.
Voltage colors have separate controls. Colored bands can be enabled or disabled.
OK saves and activates Custom; Cancel discards the draft. Switching presets keeps
the saved custom palette; choose Custom before editing to resume that palette.
Existing per-sensor text/background overrides take precedence. Reset those colors
in sensor settings to follow the palette again; automatic contrast still applies
when only a custom per-sensor background is set. Shadows remain derived from
their text color, controlled by the existing per-sensor shadow option.

**View → Dashboard Background** independently selects Solid (default), Dots, or
Grid. Patterns cover the field and show through transparent tiles, while solid
tiles retain their own background. A cached 16×16 brush repeats across the field;
no full-dashboard bitmap or animation is allocated. It moves with the tiles when
scrolling. No original wallpaper is enabled or required.

Appearance choices are saved immediately from View. The custom-color editor saves
only on OK. Settings use `Dashboard/palette` (`system`, `classic`, `graphite`,
`custom`), `Dashboard/backgroundPattern` (`solid`, `dots`, `grid`), and colors under
`Dashboard/CustomColors`. Unknown preset/pattern names fall back to System/Solid;
invalid custom colors fall back to Classic defaults. Changing appearance does not
change sensor polling, text styles, tile positions, sizes or per-sensor settings.

Right-click anywhere on a sensor tile for Configure, Delete and Hide/Show controls.
Empty cells and gutters offer only Hide/Show controls. Both menus work in compact
view; keyboard context menus work on the selected tile. Configure opens Settings
at that sensor. Delete clears only its dashboard selection: labels, colors,
calibration, ranges and any tray selection remain. Its cell becomes available,
without compacting other tiles. Re-enabling fills the first empty cell rather
than recovering its old position. Deleting while Settings is open updates that
draft's dashboard checkbox without throwing away other edits, so OK cannot
accidentally restore the deleted tile.

Settings chooses which discovered channels appear on the dashboard; table mode
uses the same selection. Full Unicode labels, device names, values and units
remain available through tooltips. Sources are omitted from tile faces at all
sizes. At small sizes gauge endpoint labels are omitted.

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
range spans cannot produce an invalid needle calculation. Custom ranges, calibration and temperature units are configured in Settings.
Alarm thresholds remain separate and follow in phase 6.

## Demand, persistence and window geometry

The tile viewport publishes the IDs of tiles intersecting it. Only those IDs
plus tray selections are read; scrolling/resizing requests newly visible data
through the same coalesced serialized worker. Table mode requests the selected dashboard catalog. Discovery obtains descriptors first,
then the chosen view supplies reading demand. No competing sensor reader is
introduced. A newly exposed tile can show unavailable briefly until its batch
returns. An already in-flight old-demand batch may finish; demand changes are
coalesced for the next batch.

Each `[Sensor-NNN]` section stores the readable `id`, sensor preferences and,
when selected for the dashboard, `column` and `row`. There are no independent
Tile sections or stored grid dimensions. Field bounds are computed from occupied
coordinates; after trimming, they start at (0, 0). Layout-only saves preserve
current preferences and do not refresh measurement consumers. Saving an open
Settings draft preserves newer layout coordinates. Deselecting releases the
position while keeping other sensor preferences.

This is a new layout format with no migration reader for Tile sections or old
field dimensions. Close the app and remove the old configuration before using
it. Sensor positions, internal gaps and control visibility survive restart.
Layouts are written only on selection/layout changes, never sample updates.
Invalid/conflicting coordinates are discarded and placed into free cells.

Tile size, table view, control visibility and plain window geometry stay
under Dashboard. Only visible sensor tiles (including a visible drag preview)
request readings; canvas growth creates no monitoring demand itself.

Plain numeric settings retain supported normal-window size and X11 position, with restored
normal-window size bounded to the current screen's available area. Compact dimensions
are always derived from the field and are never capped to the screen. Exact global positioning
and virtual-desktop restoration are not required on Wayland. Compact dragging requests native system movement, with a fallback only on X11.

## Validation

This revision has static review and clean patch-application checks only. No
configure, compilation, application execution or native mouse-capture test was
performed. Manual checks:

- Enable a few sensors; resize narrower/wider and change tile size. Confirm cell
  coordinates do not change. Disable the first tile; a new selection fills it.
- Fill the field, then enable another sensor. Only a new row should be added.
- Drag into an empty cell and onto an occupied cell; test all four growth edges,
  including left/top. Cancel with Esc and ensure temporary growth is undone and pre-existing empty
  outer layers are trimmed. Keep two separated tiles to verify interior gaps
  survive; delete the last tile to verify the single empty-cell fallback.
- Scroll large tiles, hide/show, rescan and restart; ensure positions remain and
  drag/edge timers stop after release, cancellation or hiding.
- Configure from a title menu; delete while Settings is open and press OK. Check
  that other settings/tray selection survive and the dashboard tile stays off.
- Double-click a gutter to hide/restore controls. Repeat via the title menu and
  Ctrl+Shift+M; confirm the state persists and the application remains reachable.
- Repeat native X11 and Wayland mouse dragging at fractional/high DPI. Inspect
  64, 128, 256, 512 and 1024 logical-pixel tiles. Performance is unmeasured.
