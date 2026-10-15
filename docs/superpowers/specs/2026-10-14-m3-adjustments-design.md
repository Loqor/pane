# Pane — M3 Adjustments

**Date:** 2026-10-14 · **Milestone:** M3 of 6

## Scope
All of paint.net 5's Adjustments, with paint.net's shortcuts:
Auto-Level (Ctrl+Shift+L), Black and White (Ctrl+Shift+G), Brightness / Contrast,
Curves (Ctrl+Shift+M), Hue / Saturation (Ctrl+Shift+U), Invert Colors
(Ctrl+Shift+I), Levels (Ctrl+L), Posterize (Ctrl+Shift+P), Sepia (Ctrl+Shift+E).

## Effect framework (shared with M4)
- `Effect`: an id, name, category (Adjustments, or an Effects submenu),
  shortcut, glyph, a `PropertySpec` list (Int / Double / Bool / Choice with
  ranges and defaults), and a UI kind (None / Properties / Custom). It
  creates a thread-safe `EffectInstance` from parameters plus the source
  surface and selection (so whole-image analysis such as Auto-Level happens
  once).
- `EffectInstance::render(src, rect, dst, stride)` renders a rectangle. Helper
  instances exist for 8-bit LUTs and per-pixel functions.
- `EffectSession`: snapshots the layer (copy-on-write) and renders the
  selection's tiles on the thread pool. Finished tiles are blended with
  selection coverage against the source and applied to the layer every
  ~40 ms on the UI thread (progressive preview). A parameter change cancels
  the running job and starts a new one. `finish()` waits for the job and
  pushes one history item named after the effect; `cancel()` restores the
  layer exactly.
- `EffectDialog` builds a dialog from the property list: slider + spin box
  per number, checkboxes, combo boxes, and a Reset button. Changes preview
  live. The last-used values are remembered per effect.

## Adjustments (paint.net algorithms)
- **Invert Colors:** RGB → 255 − c; alpha kept.
- **Black and White:** intensity = (7471·B + 38470·G + 19595·R) >> 16.
- **Sepia:** desaturate, then Levels with per-channel gamma (R 0.8, G 1.0, B 1.2).
- **Brightness / Contrast:** paint.net's 64K intensity/channel table
  (brightness and contrast each −100..100).
- **Hue / Saturation:** hue −180..180, saturation 0..200, lightness −100..100.
  Saturation scales each channel's distance from intensity, hue rotates in
  HSV, and lightness blends toward white or black.
- **Posterize:** 2..64 levels per channel, linked by default; snaps to the
  nearest level.
- **Levels:** input black/white, gamma, and output black/white, per channel
  or linked RGB. Shows input and output histograms. Auto computes the levels
  from the 0.5% and 99.5% percentiles and the mean.
- **Auto-Level:** Levels with the automatic parameters, computed from the
  selection's histogram.
- **Curves:** Luminosity mode (the curve maps intensity; the difference is
  added to RGB) or RGB mode (separate R/G/B curves, editable together or
  per channel). Natural cubic spline through control points; click to add a
  point, drag one off the graph to delete it.

## Testing
Unit tests check each adjustment on known pixels against the formulas, plus
spline interpolation, Auto-Level statistics, and EffectSession preview,
cancel and commit on a selection. The UI test runs the non-dialog
adjustments from the menu and drives the generic dialog.
