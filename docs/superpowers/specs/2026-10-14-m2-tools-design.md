# Pane — M2 Remaining Tools

**Date:** 2026-10-14 · **Milestone:** M2 of 6 · Builds on the M1 foundation spec.

## Scope
Ellipse Select, Lasso Select, Magic Wand, Gradient, Shapes, Line/Curve, Text,
Clone Stamp, Recolor. With these, the full paint.net tool set is available.

## Shared infrastructure
- **`maskToPath`** (core): traces an 8-bit mask's pixel-boundary edges into
  closed loops and returns an odd-even `QPainterPath` with collinear edges
  merged. Magic Wand uses it. It runs in O(pixels).
- **`CoverageStroke` color function:** an optional per-pixel
  `fn(x, y, before) → color` lets Clone Stamp (sample from an offset) and
  Recolor (adjust colors similar to a basis color) reuse the brush coverage
  machinery.
- **`EditSession`** (tools): an editable render on one layer for tools whose
  result stays adjustable until committed (Gradient, Shapes, Line/Curve,
  Text). Each update restores the previously touched tiles from their "before"
  snapshot and re-renders. Rendering is clipped by the selection and runs in
  parallel per tile. Commit pushes one history item. Commit happens on tool
  change, Enter, starting a new object, or any menu command.
- **Handles:** these reuse `BoxTransform` (move, scale, right-drag to rotate)
  for Shapes, plus point nubs for Gradient and Line/Curve.

## Tools
| Tool | Behaviour |
|------|-----------|
| Ellipse Select | Same as Rectangle Select, with an ellipse path; Shift = circle; edge handles; antialiasing follows the tool option. |
| Lasso Select | Freeform mode (drag) or Polygon mode (click points; double-click, Enter or clicking the first point closes). Selection modes as in M1. |
| Magic Wand | Flood mask (contiguous/global, tolerance, layer/image sampling) → `maskToPath` → selection with combine modes. |
| Gradient | Types: Linear, Linear Reflected, Linear Diamond, Radial, Conical, Spiral CW, Spiral CCW. Modes: Color (primary→secondary) and Transparency (scales existing alpha). Repeat: None, Sawtooth, Triangular. Right-drag swaps the colors. Endpoint nubs and a center move nub stay editable until commit. |
| Shapes | Rectangle, Rounded Rectangle, Ellipse, Diamond, Triangle, Right Triangle, Pentagon, Hexagon, Octagon, Star, Arrow, Heart. Draw style: Outline / Filled / Filled with Outline (outline = primary, fill = secondary; Filled uses primary). Brush width, dash style, antialiasing, alpha blending. Box handles: move, scale (Shift keeps aspect), right-drag rotates. |
| Line / Curve | Drag to draw. Dragging on the line adds a control point (up to 6 points total); nubs are draggable. Curve type: Spline (through the points) or Bézier. Arrowheads at start/end, dash style, brush width, antialiasing, blending. |
| Text | Click to place; type with editing keys (arrows, Home/End, Backspace/Delete, Enter for a new line), clipboard paste, IME commit strings. Font family, size, bold/italic/underline/strikeout, alignment, antialiasing, blending. A move nub drags the text. Clicking elsewhere commits and starts a new text; Escape commits. While editing, the canvas takes the keys, so tool shortcuts don't fire. |
| Clone Stamp | Ctrl+click sets the source. Painting copies from the source offset, sampled from the layer as it was at stroke start. Uses brush width, hardness and antialiasing. A source crosshair is shown. |
| Recolor | Brush that replaces colors similar (within tolerance) to the secondary color with the primary, keeping each pixel's difference from the basis color (paint.net's recolor). Right-click swaps primary and secondary. |

## Testing
Unit tests cover `maskToPath` (area equals mask coverage, holes), gradient
math for each type and repeat mode, recolor, clone offset, and EditSession
cancel/commit round trips. The UI smoke test activates every tool and drives
a click/drag through each.
