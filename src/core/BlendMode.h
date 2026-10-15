// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Color.h"

#include <QString>
#include <array>

namespace pane {

// paint.net's layer blend modes, in its UI order.
enum class BlendMode : uint8_t {
    Normal,
    Multiply,
    ColorBurn,
    ColorDodge,
    Reflect,
    Glow,
    Overlay,
    Difference,
    Negation,
    Lighten,
    Darken,
    Screen,
    Xor,
    Additive,
};
constexpr int BlendModeCount = 14;

QString blendModeName(BlendMode m);

// The per-channel blend function F(lhs, rhs) (lhs = bottom, rhs = top).
uint8_t blendChannel(BlendMode m, uint32_t lhs, uint32_t rhs);

// Composites straight-alpha rhs (scaled by opacity 0..255) over straight-alpha lhs
// using paint.net's UserBlendOps rule:
//   out.a = lhs.a*(1-rhs.a) + rhs.a
//   out.c = (lhs.c*lhs.a*(1-rhs.a) + rhs.c*rhs.a*(1-lhs.a) + F(lhs.c, rhs.c)*lhs.a*rhs.a) / out.a
Pixel blendPixel(BlendMode m, Pixel lhs, Pixel rhs, uint32_t opacity);

// Row variant: dst[i] = blendPixel(m, dst[i], src[i], opacity).
void blendRow(BlendMode m, Pixel *dst, const Pixel *src, int count, uint32_t opacity);

} // namespace pane
