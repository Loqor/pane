// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Quantize.h"

#include <algorithm>
#include <climits>

namespace pane {

double bayer8(int x, int y)
{
    static const int m[8][8] = {{0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
                                {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
                                {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
                                {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};
    return (m[y & 7][x & 7] + 0.5) / 64.0 - 0.5;
}

PaletteMap::PaletteMap(std::vector<Pixel> pal) : palette(std::move(pal)), lut(32768)
{
    for (int i = 0; i < 32768; ++i) {
        int r = ((i >> 10) & 31) * 255 / 31, g = ((i >> 5) & 31) * 255 / 31, b = (i & 31) * 255 / 31;
        int best = 0;
        long bestD = LONG_MAX;
        for (size_t k = 0; k < palette.size(); ++k) {
            long dr = r - pxR(palette[k]), dg = g - pxG(palette[k]), db = b - pxB(palette[k]);
            long d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
            if (d < bestD) {
                bestD = d;
                best = int(k);
            }
        }
        lut[size_t(i)] = uint16_t(best);
    }
}

std::vector<Pixel> medianCutPalette(std::vector<Pixel> colors, int count)
{
    if (colors.empty()) return {0xFF000000u};
    struct Box { size_t begin, end; };
    std::vector<Box> boxes{{0, colors.size()}};
    while (int(boxes.size()) < count) {
        // Split the box with the largest channel range.
        int bi = -1, bestRange = 0, bestCh = 0;
        for (int i = 0; i < int(boxes.size()); ++i) {
            const Box &b = boxes[size_t(i)];
            if (b.end - b.begin < 2) continue;
            for (int ch = 0; ch < 3; ++ch) {
                int lo = 255, hi = 0;
                for (size_t k = b.begin; k < b.end; ++k) {
                    int v = int((colors[k] >> (16 - ch * 8)) & 255);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                if (hi - lo > bestRange) {
                    bestRange = hi - lo;
                    bi = i;
                    bestCh = ch;
                }
            }
        }
        if (bi < 0) break;
        Box b = boxes[size_t(bi)];
        int shift = 16 - bestCh * 8;
        size_t mid = b.begin + (b.end - b.begin) / 2;
        std::nth_element(colors.begin() + long(b.begin), colors.begin() + long(mid), colors.begin() + long(b.end),
                         [shift](Pixel a, Pixel c) { return ((a >> shift) & 255) < ((c >> shift) & 255); });
        boxes[size_t(bi)] = {b.begin, mid};
        boxes.push_back({mid, b.end});
    }
    std::vector<Pixel> pal;
    for (const Box &b : boxes) {
        uint64_t r = 0, g = 0, bl = 0, n = b.end - b.begin;
        for (size_t k = b.begin; k < b.end; ++k) {
            r += pxR(colors[k]);
            g += pxG(colors[k]);
            bl += pxB(colors[k]);
        }
        if (n) pal.push_back(makePixel(uint32_t(r / n), uint32_t(g / n), uint32_t(bl / n), 255));
    }
    return pal;
}

} // namespace pane
