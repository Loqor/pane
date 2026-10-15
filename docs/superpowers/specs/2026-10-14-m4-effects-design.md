# Pane — M4 Effects

**Date:** 2026-10-14 · **Milestone:** M4 of 6 · Builds on the M3 effect framework.

## Scope (paint.net 5 Effects menu)
| Submenu | Effects |
|---------|---------|
| Artistic | Ink Sketch, Oil Painting, Pencil Sketch |
| Blurs | Bokeh, Fragment, Gaussian Blur, Motion Blur, Radial Blur, Surface Blur, Unfocus, Zoom Blur |
| Color | Dithering, Quantize |
| Distort | Bulge, Crystalize, Dents, Frosted Glass, Morphology, Pixelate, Polar Inversion, Tile Reflection, Twist |
| Noise | Add Noise, Median, Reduce Noise |
| Object | Align Object, Drop Shadow, Outline Object |
| Photo | Glow, Red Eye Removal, Sharpen, Soften Portrait, Vignette |
| Render | Clouds, Julia Fractal, Mandelbrot Fractal, Voronoi Diagram |
| Stylize | Edge Detect, Emboss, Outline Edge, Relief |

**Repeat last effect** (Ctrl+F) re-applies the last effect or adjustment with its last
parameters and no dialog, labelled "Repeat <name>" at the top of the Effects menu.

## Framework additions
- **Property types:** `Seed` (an integer with a Reseed button), `Color` (a color
  button), plus the existing Int / Double / Bool / Choice. Centers are two
  Doubles in −1…1, relative to the selection bounds.
- **Context:** the primary and secondary colors are injected into the params as
  `_primary` and `_secondary`. Effects receive the selection bounds through the
  `Selection` passed to `instance()`.
- **`EffectUtil`:**
  - padded source fetch with clamp-to-edge;
  - premultiplied int/float buffers;
  - a separable Gaussian, done as three box passes so cost doesn't grow with
    radius;
  - bilinear sampling straight from a `Surface`;
  - a tile-independent hash RNG and Perlin noise, so results never depend on
    tile boundaries;
  - a generic `LambdaEffect` / rect-render instance to keep each effect short.
- **Tile independence:** every effect computes each output pixel from the
  source alone. Effects that depend on the whole image (Quantize's palette,
  Align Object's bounds, Drop Shadow) compute it once when the instance is
  created.
- **Dithering** uses ordered (Bayer) dithering, which stays tile-independent;
  paint.net's error-diffusion option is not offered.

## Fidelity
The algorithms follow paint.net's published behaviour and parameter ranges.
Some internals aren't documented (Dents, Tile Reflection, Ink Sketch and the
local-histogram filters); those use standard equivalents with the same
controls and visual intent. Comparison against real paint.net output is
deferred until reference images are available.

## Testing
- **Unit tests:** every effect renders a test image tile-by-tile, and that
  output must equal a single whole-image render (tile independence). Each
  effect runs at its defaults without crashing or producing NaN. Targeted
  checks: Gaussian preserves flat color, Pixelate cells are uniform, Invert
  via Edge Detect symmetry, Drop Shadow extends past the object, Quantize
  produces ≤ N colors, Median removes salt noise.
- **UI test:** every effect is opened from the menu and accepted, then Repeat
  (Ctrl+F) is run.
