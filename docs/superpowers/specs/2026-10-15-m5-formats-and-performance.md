# Pane — M5 File Formats + Performance Pass

**Date:** 2026-10-15 · **Milestone:** M5 of 6 (performance work pulled forward from M6)

## File formats
| Format | Load | Save | Options | Notes |
|--------|------|------|---------|-------|
| PDN | layers | layers | — | Own NRBF (BinaryFormatter) writer; deferred gzip chunks; XML header with thumbnail. Round-trips exactly through Pane's reader. **Not yet verified against paint.net itself.** |
| OpenRaster (ORA) | layers | layers | — | stack.xml, per-layer PNG, merged image, thumbnail; svg:* blend modes (Xor/Reflect/Glow/Negation use pane:*). |
| PNG | ✓ | ✓ | — | Save uses a new parallel encoder (pigz-style strips with dictionary chaining). |
| JPEG | ✓ | ✓ | quality, chroma subsampling | |
| BMP | ✓ | ✓ | — | |
| WebP | ✓ (first frame) | ✓ | quality, lossless, effort | |
| AVIF | ✓ | ✓ | quality, chroma subsampling, speed | |
| HEIC/HEIF | ✓ | ✓ when an HEVC encoder is present | quality | |
| TIFF | ✓ (pages of equal size → layers) | ✓ | None / LZW / Deflate | exact 8-bit fast path |
| TGA | ✓ (all common variants) | ✓ | RLE | sniffed via TGA 2.0 footer |
| DDS | BC1/2/3, uncompressed (masks), DX10 | BC1, BC2, BC3, BGRA8 | format, mip maps | BC7/HDR not yet supported |
| GIF | ✓ (first frame) | ✓ | dithering, transparency threshold | median-cut palette + ordered dither |
| ICO | ✓ (largest entry) | ✓ (PNG entries) | size set | |

Open uses content sniffing first, then the file extension. The Save Configuration
dialog is generic: it is built from each type's `PropertySpec` options, shows the
real encoded size (encoded in the background), shows a decoded preview for lossy
formats, and remembers the options per format.

## Performance (6000×4000, 5 layers, 12 threads, Release)
| Operation | Before | After |
|-----------|--------|-------|
| Flatten | 893 ms | 93 ms |
| Save PNG | 2422 ms | 581 ms |
| Save JPEG | 1466 ms | 568 ms |
| Fill selection (whole image) | 256 ms | 111 ms |
| Rotate 90° | 513 ms | 151 ms |
| Flip | 475 ms | 109 ms |
| Resize 50 % (Best Quality) | 1074 ms | 473 ms |
| Flood fill contiguous / global | 240 / 169 ms | 92 / 32 ms |
| Move Selected Pixels, per drag step | 74 ms | 41 ms |
| Gradient, per drag step | 43–65 ms | ~50 ms |
| Brush stroke p95 | 6.3 ms | 3.6 ms |
| Full composite (1 / 2 / 5 layers) | — / — / 168 ms | 31 / 38 / 103 ms |

Techniques:
- division-free blending via an exact reciprocal table, verified exhaustively;
- blend loops specialised per blend mode;
- a deadlock-safe `parallelFor` (it only uses idle pool threads, and the
  calling thread participates) used for flatten, fills, undo-data compression
  and decompression, flip/rotate (exact tile remaps), resampling, flood-fill
  similarity, Move Selected Pixels tiles, and codecs;
- resampling streams row by row instead of making full-image float copies;
- compositor tile images are reused instead of reallocated;
- moves that are pure translations snap to whole pixels, as in paint.net.
