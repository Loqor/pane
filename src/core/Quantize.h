// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Color.h"

#include <vector>

namespace pane {

// 8x8 ordered-dither (Bayer) threshold in [-0.5, 0.5).
double bayer8(int x, int y);

// Median-cut palette (opaque colors) of at most `count` entries.
std::vector<Pixel> medianCutPalette(std::vector<Pixel> colors, int count);

// Nearest-palette mapping through a 15-bit lookup table.
struct PaletteMap {
    std::vector<Pixel> palette;
    std::vector<uint16_t> lut; // 32768 entries
    explicit PaletteMap(std::vector<Pixel> pal);
    int index(int r, int g, int b) const
    {
        r = std::clamp(r, 0, 255);
        g = std::clamp(g, 0, 255);
        b = std::clamp(b, 0, 255);
        return lut[size_t(((r * 31 + 127) / 255) << 10 | ((g * 31 + 127) / 255) << 5 | ((b * 31 + 127) / 255))];
    }
    Pixel map(int r, int g, int b) const { return palette[size_t(index(r, g, b))]; }
};

} // namespace pane
