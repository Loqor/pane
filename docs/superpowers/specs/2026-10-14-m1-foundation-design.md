# Pane — M1 Foundation Design

**Date:** 2026-10-14
**Status:** Draft for review
**Milestone:** M1 of 6

## 1. Purpose and context

Pane is a Linux-native image editor with the same UI, features and behaviour as
paint.net 5.x. The user wants a **daily-driver replacement**: behavioural
fidelity, performance on large images and safety of work take priority over
everything else.

**Stated by the user**
- Same UI and functional parity with paint.net.
- Daily driver (not a learning project, not primarily .pdn interchange).
- paint.net plugin compatibility is **not** required.
- Desktop: **niri** (scrollable-tiling Wayland compositor).
- Stack: **C++20 + Qt 6**.
- Theming: Light, Dark, and all four Catppuccin flavours (Latte, Frappé,
  Macchiato, Mocha) with Catppuccin accents.
- No image-file or AI-generated icons; icons are **text glyphs** from a bundled
  **Symbols Nerd Font**.

**Assumptions (correct if wrong)**
- 8-bit-per-channel BGRA is sufficient (matches paint.net).
- Single-process, single main window; multiple documents in tabs.
- English UI only for now.

### Roadmap (each milestone gets its own spec → plan → implementation)

| # | Milestone | Scope |
|---|-----------|-------|
| **M1** | **Foundation (this spec)** | Engine, compositor, shell UI, panels, layers, history, colors, 10 tools, Edit/Image/Layers menus, PNG/JPEG/BMP + .pdn read |
| M2 | Remaining tools | Lasso, Ellipse Select, Magic Wand, Gradient, Shapes, Line/Curve, Text, Clone Stamp, Recolor; selection combine modes |
| M3 | Adjustments | All paint.net adjustments |
| M4 | Effects | All paint.net effect categories, Repeat Last Effect |
| M5 | File formats | .pdn write, WebP/AVIF/HEIC/TIFF/TGA/DDS/GIF/ORA/ICO, save-config dialogs with size preview |
| M6 | Polish | Settings dialog, printing, shortcut remapping, autosave/crash recovery, HiDPI multi-monitor, tablet tuning, perf passes |

### M1 success criteria
1. The app launches on niri as a single toplevel and its layout is
   recognisably paint.net 5's.
2. A user can open a PNG/JPEG/BMP/.pdn, paint on multiple layers with blend
   modes, select/move/crop, resize, undo/redo through history, and save to
   PNG/JPEG/BMP without data loss.
3. Brush strokes stay fluid (no visible lag) on a 6000×4000, 5-layer document.
4. All 6 themes plus accents switch live.
5. The test suite passes under ASan/UBSan.

## 2. Tech stack and build

- C++20, CMake ≥ 3.28, Qt 6 (Core, Gui, Widgets, Concurrent, Test, OpenGL
  optional).
- `flake.nix` dev shell: Qt 6, `kimageformats` (for M5), libpng, libjpeg-turbo,
  zlib, lz4, Catch2 v3, clang-tools, ninja.
- Bundled font: Symbols Nerd Font (OFL/MIT, license file shipped alongside),
  embedded as a Qt resource.
- Build types: `Debug` (ASan + UBSan), `RelWithDebInfo`, `Release`
  (`-O3 -march=x86-64-v2`, AVX2 paths selected at runtime).
- `nix flake check` builds and runs all tests.

## 3. Repository layout

```
CMakeLists.txt  flake.nix  LICENSE  third_party/fonts/
src/
  core/     Surface, Tile, ColorBgra, Document, Layer, BlendMode, Selection,
            History, commands, geometry helpers. No QtWidgets dependency
            (QtCore/QtGui only for QPainterPath, QImage interop).
  render/   Compositor, TileCache, ViewTransform, SIMD blend kernels, ThreadPool
  tools/    Tool interface, ToolRegistry, each M1 tool
  io/       FileType interface, FileTypeRegistry, Png/Jpeg/Bmp, PdnReader (NRBF)
  ui/       MainWindow, MenuBuilder, CommonToolbar, ToolOptionsBar,
            ImageTabStrip, CanvasView, Rulers, StatusBar, FloatingPanelHost,
            panels/{Tools,History,Layers,Colors}, dialogs/, theme/, Glyphs
  app/      main.cpp, Application, ActionRegistry (shortcuts), Settings
tests/      unit/, golden/, io/, ui/  (Catch2 + QTest)
```

Dependency direction: `app → ui → tools/io → render → core`. `core` and
`render` are fully unit-testable without a display.

## 4. Core engine

### 4.1 Pixels
- `ColorBgra`: 4×uint8, **premultiplied** in memory. Conversion helpers to/from
  straight alpha, HSV and hex.
- `Surface`: width × height, stored as 256×256 `Tile`s. Tiles are allocated
  lazily; an unallocated tile means fully transparent. Each tile has a
  `generation` counter that increments on write.
- `Surface::lockRect(rect)` returns tile views for bulk processing; helpers to
  read/write `QImage` (ARGB32_Premultiplied) for I/O and the clipboard.

### 4.2 Document and layers
- `Document`: size, DPI (x/y), `std::vector<Layer>`, active layer index,
  `Selection`, `History`, file path, `FileType*`, save options, dirty flag,
  metadata map.
- `Layer`: name, visible, opacity (0–255), `BlendMode`, `Surface`.
- **Blend modes (all 14):** Normal, Multiply, ColorBurn, ColorDodge, Reflect,
  Glow, Overlay, Difference, Negation, Lighten, Darken, Screen, Xor, Additive.
  Formulas follow paint.net's documented UserBlendOps (blend the colour channels
  on straight colour, then composite with the source-over alpha rule). Each mode
  has a scalar reference implementation plus an AVX2 kernel. Tests assert that
  both agree exactly.

### 4.3 Selection
- Source of truth is a `QPainterPath` polygon set in image coordinates
  (odd-even fill), plus an `antialias` flag.
- `SelectionMask`: an 8-bit coverage mask, rasterised lazily per tile and
  cached; invalidated when the path changes.
- An empty selection means "everything". All pixel operations clip through
  `Selection::coverageAt(tile)`.
- Marching ants are drawn in screen space: a 1px dashed line in two tones,
  animated, rebuilt on zoom.
- M1 combine mode is Replace only (Union/Exclude/Intersect/Xor come in M2), but
  the API takes a `CombineMode` from day one.

### 4.4 History
- `HistoryItem { QString name; QChar glyph; virtual undo(Document&);
  virtual redo(Document&); size_t memoryBytes(); }`.
- `PixelHistoryItem`: per layer, the touched tiles' **before** and **after**
  data, LZ4-compressed in memory.
- Structural items: AddLayer, DeleteLayer, DuplicateLayer, MoveLayer,
  MergeLayerDown, LayerProperties, Flatten, ResizeImage, CanvasSize, Rotate/Flip
  (image and layer), SelectionChange, Crop, PasteIntoNewLayer.
- `History` keeps an undo stack and a redo stack. Pushing a new item clears the
  redo stack. Jumping to step N applies undo/redo repeatedly. The first item is
  always "Open Image" or "New Image", which cannot be undone.
- Undoing or redoing a selection change restores the selection too, as
  paint.net does.

### 4.5 Commands
All document changes go through `Command` objects executed on the UI thread.
A command either completes and pushes a `HistoryItem` or throws; on throw, any
partially applied state is rolled back from the staged data.

## 5. Rendering

- `Compositor`: given a dirty tile set, re-composites those tiles (layers in
  order with blend modes and opacity) into a `FlattenedCache` (a premultiplied
  `Surface`). The work runs as tile jobs on a `ThreadPool`. Live tool staging
  surfaces are composited in place of the active layer while a stroke is in
  progress.
- `CanvasView` (QWidget; QOpenGLWidget is optional and decided during
  planning by profiling):
  - `ViewTransform`: zoom (paint.net's preset steps, 1%–3200%, plus a free
    zoom value) and scroll offset.
  - Zoomed in past 100%: nearest-neighbour sampling. Zoomed out: box-filtered
    mip levels (a ½ chain built lazily from the flattened cache).
  - Draw order: checkerboard (theme-coloured), image, pixel grid (View → Pixel
    Grid, shown only at ≥ 400% zoom), selection outline, tool overlays (handles,
    brush cursor outline).
  - A drop shadow around the image on the backdrop, as in paint.net.
- Rulers: top and left, in the selected units (pixels, inches, centimetres)
  using document DPI, with cursor position markers.
- Thumbnails (for the tabs and layer rows) are rebuilt with throttling (about
  every 250 ms while edits are happening) from the flattened cache or the layer.

## 6. Shell UI

### 6.1 Layout (top to bottom)
1. **Menu row:** File, Edit, View, Image, Layers, Adjustments, Effects (the
   last two hold placeholder "coming in M3/M4" items, disabled), followed by
   the **ImageTabStrip** (live thumbnails, `*` for unsaved, hover close,
   middle-click close, drag to reorder), then the Settings (⚙) and Help glyph
   buttons.
2. **Common actions toolbar:** New, Open, Save, Print (disabled until M6), Cut,
   Copy, Paste, Crop to Selection, Deselect, Undo, Redo, Pixel Grid toggle,
   Rulers toggle, Units combo, Zoom combo.
3. **Tool options toolbar:** "Tool:" glyph button (opens a tool picker), then
   the widgets the current tool provides.
4. **Canvas area:** CanvasView plus rulers, with the **FloatingPanelHost**
   overlaying it.
5. **Status bar:** current tool hint, image size, cursor position, selection
   size, units, zoom slider plus percentage.

### 6.2 Floating panels
- Four panels: **Tools** (F5), **History** (F6), **Layers** (F7), **Colors**
  (F8). Each is a child widget of FloatingPanelHost, so niri only ever sees
  one window.
- A title strip with a close glyph; drag to move; resize from edges (History
  and Layers only, like paint.net); snap to host edges and to other panels
  within 8 px.
- **Translucency:** when the pointer is outside a panel *and* a canvas
  interaction is in progress, the panel fades to 40% opacity (toggle in View).
- Default positions match paint.net: Tools top-left, Colors bottom-left,
  History top-right, Layers bottom-right. Positions, sizes and visibility are
  saved in Settings. View → Panels → "Reset Panel Positions".
- When the host is resized, panels stay anchored to their nearest edge.

### 6.3 Panels
- **Tools:** a two-column glyph grid of every tool in the registry, in
  paint.net order. M2+ tools appear disabled. Hovering shows the name and
  shortcut.
- **History:** a list of items (glyph + name); undone items are greyed out.
  Click to jump. Footer buttons: Undo, Redo (and First/Last via the context
  menu).
- **Layers:** rows with visibility checkbox, thumbnail and name; the active row
  is highlighted. Double-click opens Layer Properties. Drag to reorder. Footer:
  Add, Delete, Duplicate, Merge Down, Move Up, Move Down, Properties.
- **Colors:** primary/secondary swatches with swap (X) and reset (D) glyphs,
  "More >>" expanding to a full editor (HSV wheel, saturation/value square,
  RGB/HSV sliders and spin boxes, hex field, alpha slider), plus a 96-swatch
  palette (paint.net default palette; left-click sets primary, right-click sets
  secondary). Palette load/save comes in M6.

### 6.4 Dialogs (real modal windows; niri floats dialogs)
New Image (size presets from the clipboard, maintain aspect ratio, DPI, units),
Resize Image (by percentage or absolute, resampling: Best Quality /
Bicubic / Bilinear / Nearest Neighbor, maintain aspect ratio), Canvas Size
(anchor 3×3 grid), Layer Properties (live preview, cancel reverts), Save
options for JPEG (quality), Unsaved Changes prompt (Save / Don't Save / Cancel,
listing every dirty document when quitting), error message box.

### 6.5 Menus (M1 contents)
- **File:** New, Open, Open Recent (10), Save, Save As, Save All, Close,
  Close All, Exit.
- **Edit:** Undo, Redo, Cut, Copy, Copy Merged, Paste, Paste into New Layer,
  Paste into New Image, Select All, Deselect, Invert Selection, Erase
  Selection, Fill Selection, Crop to Selection.
- **View:** Zoom In, Zoom Out, Zoom to Window, Zoom to Selection, Actual Size,
  Pixel Grid, Rulers, Units, Translucent Panels, Theme ▸ (themes and accents),
  Panels ▸ (four toggles plus Reset Positions).
- **Image:** Crop to Selection, Resize, Canvas Size, Flip Horizontal, Flip
  Vertical, Rotate 90° CW / 90° CCW / 180°, Flatten.
- **Layers:** Add New Layer, Delete Layer, Duplicate Layer, Merge Layer Down,
  Import from File, Flip Horizontal, Flip Vertical, Rotate 180°, Layer
  Properties, Move Layer Up, Move Layer Down.

### 6.6 Keyboard (single `ActionRegistry` table)
Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+W, Ctrl+Q, Ctrl+Z, Ctrl+Y,
Ctrl+X, Ctrl+C, Ctrl+Shift+C (copy merged), Ctrl+V, Ctrl+Shift+V,
Ctrl+Alt+V, Ctrl+A, Ctrl+D, Ctrl+I, Delete (erase selection), Backspace (fill
with primary), Ctrl+Shift+X (crop), Ctrl+R (resize), Ctrl+Shift+R (canvas
size), Ctrl+H / Ctrl+G / Ctrl+J (rotate CW / CCW / 180), Ctrl+Shift+F
(flatten), Ctrl+Shift+N (add layer), Ctrl+Shift+D (duplicate), Ctrl+M (merge
down), F4 (layer properties), Ctrl++ / Ctrl+- (zoom), Ctrl+B (zoom to window),
Ctrl+0 (actual size), Ctrl+Tab / Ctrl+Shift+Tab (next/previous image),
F5–F8 (panels), X (swap colors), D (default colors). Single-key tool shortcuts
(S, M, Z, H, B, E, P, F, K) cycle through the tools that share a key. Holding
Space temporarily switches to Pan.

## 7. Tools (M1)

Common interface: `activate()`, `deactivate()`, `pointerDown/Move/Up(PointerEvent)`,
`keyPress`, `paintOverlay(QPainter&, ViewTransform)`, `optionsWidgets()`,
`cursor()`, `hint()`, `commit()`, `cancel()`. `PointerEvent` carries image-space
float coordinates, pressure (1.0 for a mouse), buttons and modifiers. Left
button uses the primary colour, right button the secondary.

Shared tool options (shown per tool as in paint.net): brush width (1–2000,
with − / + buttons), hardness (0–100%, brush only), antialiasing on/off, alpha
blending (Normal / Overwrite), fill mode (flood: Contiguous / Global),
tolerance (0–100%), sampling (Layer / Image).

| Tool | Key | Behaviour |
|------|-----|-----------|
| Rectangle Select | S | Drag to select; Shift constrains to a square; handles to adjust after drawing; options: fixed ratio/size |
| Move Selected Pixels | M | Lifts the selected pixels into a floating staging layer; drag to move, handles to scale, right-drag to rotate; Ctrl+drag leaves a copy behind; commits on tool change or Enter |
| Move Selection | M | Same handles but only moves the outline |
| Zoom | Z | Left-click zooms in, right-click zooms out, drag a rectangle to zoom to it |
| Pan | H | Drag to scroll; Space+drag from any tool |
| Paintbrush | B | Round brush dabs with spacing about 1/8 of width, hardness falloff, pressure → width, antialiasing |
| Pencil | P | Aliased 1px, always opaque; Bresenham lines between samples |
| Eraser | E | Brush with alpha subtraction; the same options as Paintbrush |
| Paint Bucket | F | Flood fill: contiguous or global, tolerance using paint.net's distance metric, sampling layer or image, clipped by the selection |
| Color Picker | K | Picks into primary/secondary; sampling: single pixel or 3×3 … 9×9 average; layer or image; after picking: do nothing / switch to previous tool / switch to Pencil |

## 8. File I/O

- `FileType` interface: name, extensions, `canLoad`, `canSave`,
  `load(QIODevice&) → Document`, `save(Document&, QIODevice&, SaveOptions)`,
  `optionsDialog()`.
- **PNG, JPEG, BMP:** load and save through libpng/libjpeg-turbo directly (not
  QImageReader) for full control over metadata and DPI. Saving flattens the
  image with a "Flatten image?" prompt, as in paint.net.
- **.pdn (read only in M1):** container is the `PDN3` magic, a 24-bit
  little-endian header length, a UTF-8 XML header, then .NET BinaryFormatter
  (NRBF) records describing the Document/Layer graph, followed by
  gzip-compressed layer chunks. M1 implements a **minimal NRBF reader**
  (ClassWithMembersAndTypes, SystemClass*, BinaryObjectString, ArraySingle*,
  MemberReference, ObjectNull, MessageEnd) sufficient for the paint.net
  Document / Layer / BitmapLayer / LayerProperties / Surface types. Unknown or
  newer variants fail with a clear "unsupported .pdn version" error. Saving a
  .pdn is disabled until M5.
- **Safe save:** write to `<name>.pane-tmp` in the same directory, fsync,
  rename over the target, fsync the directory. On error the temp file is
  removed and the original is untouched.
- Open dialog: native Qt dialog via the XDG portal. File types are filtered
  from the registry. Drag and drop from the file manager is supported.

## 9. Theming

- `Theme` = name plus tokens: `base, mantle, crust, surface0, surface1,
  surface2, overlay0, overlay1, overlay2, text, subtext0, subtext1, accent`,
  plus derived values (`selection`, `checkerA/B`, `canvasBackdrop`, `danger`).
- Built in: **Light**, **Dark** (paint.net 5-like neutrals), **Catppuccin
  Latte / Frappé / Macchiato / Mocha** (official palette hex values).
- **Accent:** for Catppuccin themes, one of the 14 accents (rosewater,
  flamingo, pink, mauve, red, maroon, peach, yellow, green, teal, sky,
  sapphire, blue, lavender). Light/Dark accept any colour (default blue).
- **Custom themes:** `~/.config/pane/themes/*.json` with the same token keys;
  invalid files are skipped with a warning in the log.
- `ThemeEngine` builds a `QPalette` and one QSS sheet (Fusion base style,
  4 px radii, compact metrics) and broadcasts `themeChanged()`. Custom-painted
  widgets (canvas, rulers, panels, tab strip) read tokens from the engine.
  Changes apply live.
- Default: follows the XDG portal colour-scheme preference (Light or Dark)
  until the user picks one.

## 10. Icons

- Every icon is a `QChar`/string from **Symbols Nerd Font**, rendered as text
  by a `GlyphIcon` helper (a QIconEngine that paints the glyph in the current
  theme's text or accent colour, at any DPI).
- A single `Glyphs.h` table maps semantic names (`Tool::Paintbrush`,
  `Action::Undo`, …) to codepoints, so swapping glyphs is a one-line change.
- If a glyph is missing, the fallback is a short text label (for example
  "Wd").
- No raster or vector image assets in the repository except the app's .desktop
  icon. That icon is also rendered from a glyph at build time by a small tool
  (no generated artwork).

## 11. Settings

`QSettings` (INI) at `~/.config/pane/pane.conf`: theme, accent, panel
geometry and visibility, translucent panels, units, rulers, pixel grid,
recent files, last-used per-tool options, last dialog values (resize mode,
JPEG quality).

## 12. Error handling

- I/O errors → a modal message box (title, plain-language reason, path).
  Failed opens create no document. Failed saves leave the original file
  intact (see safe save).
- Allocation failure while creating or resizing a large surface is caught
  (`std::bad_alloc`), the command is rolled back, and the user sees "Not
  enough memory to …".
- Exceptions thrown from tools or commands are caught at the command boundary,
  the staging surface is discarded, and history stays consistent.
- Quitting with unsaved documents shows the Unsaved Changes prompt.
- Logging: `qCInfo/qCWarning` categories (`pane.io`, `pane.render`, …) to
  stderr.

## 13. Testing

- **Unit (Catch2):** ColorBgra conversions and premultiply round trips; each
  blend mode (scalar vs AVX2 vs reference formula on edge alphas 0/1/254/255);
  Surface tiling and lazy allocation; Selection mask rasterisation
  (antialiased and aliased); History: 1,000 random operations, then undo to
  start and redo to end, with pixel-identical checks at every step; flood fill
  tolerance; resize resamplers on known inputs.
- **Golden images:** fixtures under `tests/golden/`, operation results compared
  with PNGs (exact for integer operations; max-channel-delta ≤ 1 for
  resampling).
- **I/O:** PNG/JPEG/BMP round trips (JPEG compared by PSNR threshold);
  `.pdn` reading against fixtures. Fixtures are produced by a test-only NRBF
  writer, plus real paint.net files if the user supplies them.
- **UI smoke (QTest, `QT_QPA_PLATFORM=offscreen`):** start MainWindow, switch
  each tool, perform a synthetic brush stroke, undo, toggle each panel, switch
  each theme.
- **Performance check:** a benchmark target that brushes 1,000 dabs on
  6000×4000×5 layers and reports frame times (target: p95 < 16 ms for
  composite plus present of the dirty region).
- Debug builds run all tests under ASan/UBSan. `nix flake check` runs the
  whole suite.

## 14. Out of scope for M1

Every M2–M6 item in the roadmap table; printing; plugins (permanently out of
scope); scanner/camera import; localisation; 16-bit/HDR.

## 15. Risks

- **.pdn NRBF variability between paint.net versions.** Mitigation: a minimal
  tolerant reader, a clear error for unknown layouts, and fixtures from real
  files when available.
- **Blend-mode fidelity without the paint.net source.** Mitigation: documented
  formulas and golden tests; comparison against real paint.net exports when the
  user can supply them.
- **QWidget canvas performance at 8K.** Mitigation: dirty-rect presentation
  and a profiling gate in planning that decides whether to switch to
  QOpenGLWidget.

## 16. Implementation notes (deviations from this spec, 2026-10-14)

- **Layer pixels are stored with straight alpha**, not premultiplied: this is what
  paint.net's ColorBgra does. It preserves color under transparent pixels and
  matches its blend math exactly. Only the compositor's display tiles are
  premultiplied.
- **No separate flattened cache:** the compositor keeps premultiplied display
  tiles with a 6-level mip chain. Sampling the flattened image uses
  `Document::flatten()` or the per-pixel `compositePixel()`.
- **There are no hand-written AVX2 blend kernels yet.** The scalar kernels
  (compiled with `-O3 -march=x86-64-v2`) already meet the performance target
  (6000×4000×5 layers: p95 6 ms per brush event, see `tests/bench`). AVX2
  moves to M6.
- **Move Selected Pixels records one history item per move session**
  (committed on tool change, Enter, undo or any command), not one per drag.
- **Selection combine modes** (Ctrl = union, Alt = subtract, right-drag = xor,
  Alt+right = intersect, plus the toolbar selector) shipped in M1 for
  Rectangle Select.
