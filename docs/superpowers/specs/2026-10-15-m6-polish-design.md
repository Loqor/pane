# Pane — M6 Polish

**Date:** 2026-10-15 · **Milestone:** M6 of 6

## Scope
1. **Settings dialog** (sidebar pages like paint.net 5):
   - User Interface: theme, accent, translucent panels;
   - Tools & Tablet: pressure → size, pressure → opacity, response curve, minimum size;
   - Autosave: on/off, interval, open the recovery folder;
   - Keyboard: every menu command and every tool letter;
   - Diagnostics: versions, paths, thread count.

   Changes apply immediately.
2. **Shortcut remapping:** every registered action has an id and a default
   sequence. Overrides are stored under `Shortcuts/<id>`. Tool letters (which
   cycle through tools sharing a key) are remappable per tool under
   `ToolKeys/<id>`. Conflicts are flagged in the editor, and you can reset one
   shortcut or all of them.
3. **Autosave & crash recovery:**
   - Every N minutes (default 5), each document changed since its last
     autosave is snapshotted. The snapshot is copy-on-write, so it's instant
     on the UI thread.
   - The snapshot is encoded as OpenRaster (lossless, keeps layers) in the
     background into `~/.local/share/pane/recovery/<uuid>.ora`, with a
     `<uuid>.json` holding the original path, name, file type, owning pid and
     time. Writes are atomic.
   - Saving or closing a document deletes its recovery files.
   - On startup, entries whose pid is no longer a running Pane are offered for
     recovery (with thumbnails). Recovered images open as unsaved documents
     with their original path.
4. **Printing:** File → Print (Ctrl+P) opens a print preview. The image prints
   at its physical size (from its DPI) when that fits on the page, and is
   scaled to fit otherwise, centred. Uses Qt Print Support.
5. **Tablet:** a pressure curve `p' = p^γ` (γ 0.3–3) and a minimum size
   percentage. Pressure can drive brush size and/or opacity (dab strength).
   This applies to Paintbrush, Eraser, Clone Stamp and Recolor.
6. **HiDPI multi-monitor:** the canvas reacts to device-pixel-ratio changes by
   relaying out its scroll range and keeping the centre point. Glyph icons
   already render per DPR.
7. **Palettes:** the Colors panel gets a palette menu with Open, Save As and
   Reset to Default. It uses paint.net's palette text format: one `AARRGGBB`
   hex per line, `;` comments, 96 entries padded with white. The current
   palette persists between sessions. Shift+click sets a swatch to the primary
   color.

## Testing
- **Unit tests:** palette parse/format round trip (including comments and
  padding); pressure curve; autosave writes a recoverable ORA plus metadata,
  and the recovery scan ignores live pids; print layout math (physical size vs
  fit).
- **UI tests:** open every Settings page; theme changes from the dialog; a
  remapped shortcut fires and persists; a remapped tool key cycles; crash
  recovery offers and restores a document.
