// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/BlendMode.h"

#include <QCoreApplication>
#include <cstdlib>

namespace pane {

QString blendModeName(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal: return QStringLiteral("Normal");
    case BlendMode::Multiply: return QStringLiteral("Multiply");
    case BlendMode::ColorBurn: return QStringLiteral("Color Burn");
    case BlendMode::ColorDodge: return QStringLiteral("Color Dodge");
    case BlendMode::Reflect: return QStringLiteral("Reflect");
    case BlendMode::Glow: return QStringLiteral("Glow");
    case BlendMode::Overlay: return QStringLiteral("Overlay");
    case BlendMode::Difference: return QStringLiteral("Difference");
    case BlendMode::Negation: return QStringLiteral("Negation");
    case BlendMode::Lighten: return QStringLiteral("Lighten");
    case BlendMode::Darken: return QStringLiteral("Darken");
    case BlendMode::Screen: return QStringLiteral("Screen");
    case BlendMode::Xor: return QStringLiteral("Xor");
    case BlendMode::Additive: return QStringLiteral("Additive");
    }
    return {};
}

uint8_t blendChannel(BlendMode m, uint32_t a, uint32_t b)
{
    switch (m) {
    case BlendMode::Normal: return uint8_t(b);
    case BlendMode::Multiply: return uint8_t(mul255(a, b));
    case BlendMode::ColorBurn:
        if (b == 0) return 0;
        return uint8_t(std::max(0, 255 - int(((255 - a) * 255) / b)));
    case BlendMode::ColorDodge:
        if (b == 255) return 255;
        return uint8_t(std::min<uint32_t>(255, (a * 255) / (255 - b)));
    case BlendMode::Reflect:
        if (b == 255) return 255;
        return uint8_t(std::min<uint32_t>(255, (a * a) / (255 - b)));
    case BlendMode::Glow:
        if (a == 255) return 255;
        return uint8_t(std::min<uint32_t>(255, (b * b) / (255 - a)));
    case BlendMode::Overlay:
        if (a < 128) return uint8_t(mul255(2 * a, b));
        return uint8_t(255 - mul255(2 * (255 - a), 255 - b));
    case BlendMode::Difference: return uint8_t(std::abs(int(a) - int(b)));
    case BlendMode::Negation: return uint8_t(255 - std::abs(255 - int(a) - int(b)));
    case BlendMode::Lighten: return uint8_t(std::max(a, b));
    case BlendMode::Darken: return uint8_t(std::min(a, b));
    case BlendMode::Screen: return uint8_t(a + b - mul255(a, b));
    case BlendMode::Xor: return uint8_t(a ^ b);
    case BlendMode::Additive: return uint8_t(std::min<uint32_t>(255, a + b));
    }
    return uint8_t(b);
}

namespace {

template <BlendMode M> inline uint32_t channelT(uint32_t a, uint32_t b) { return blendChannel(M, a, b); }

template <BlendMode M> inline Pixel blendPixelT(Pixel lhs, Pixel rhs, uint32_t opacity)
{
    uint32_t rA = opacity == 255 ? pxA(rhs) : mul255(pxA(rhs), opacity);
    if (rA == 0) return lhs;
    uint32_t lA = pxA(lhs);
    if (lA == 0) return withAlpha(rhs, rA);
    if constexpr (M == BlendMode::Normal) return overPixel(lhs, rhs, rA);
    uint32_t y = mul255(lA, 255 - rA); // lhs only
    uint32_t total = y + rA;
    uint32_t x = mul255(lA, rA);       // overlap
    uint32_t z = rA - x;               // rhs only
    auto ch = [&](uint32_t l, uint32_t r) { return divSmall(l * y + r * z + channelT<M>(l, r) * x + total / 2, total); };
    return makePixel(ch(pxR(lhs), pxR(rhs)), ch(pxG(lhs), pxG(rhs)), ch(pxB(lhs), pxB(rhs)), total);
}

template <BlendMode M> void blendRowT(Pixel *dst, const Pixel *src, int count, uint32_t opacity)
{
    for (int i = 0; i < count; ++i) {
        Pixel s = src[i];
        if (!(s >> 24)) continue; // transparent source: nothing to do
        if constexpr (M == BlendMode::Normal) {
            if (opacity == 255 && (s >> 24) == 255) {
                dst[i] = s;
                continue;
            }
        }
        dst[i] = blendPixelT<M>(dst[i], s, opacity);
    }
}

} // namespace

Pixel blendPixel(BlendMode m, Pixel lhs, Pixel rhs, uint32_t opacity)
{
    switch (m) {
#define PANE_CASE(M) case BlendMode::M: return blendPixelT<BlendMode::M>(lhs, rhs, opacity);
        PANE_CASE(Normal) PANE_CASE(Multiply) PANE_CASE(ColorBurn) PANE_CASE(ColorDodge) PANE_CASE(Reflect)
        PANE_CASE(Glow) PANE_CASE(Overlay) PANE_CASE(Difference) PANE_CASE(Negation) PANE_CASE(Lighten)
        PANE_CASE(Darken) PANE_CASE(Screen) PANE_CASE(Xor) PANE_CASE(Additive)
#undef PANE_CASE
    }
    return lhs;
}

void blendRow(BlendMode m, Pixel *dst, const Pixel *src, int count, uint32_t opacity)
{
    if (opacity == 0) return;
    switch (m) {
#define PANE_CASE(M) case BlendMode::M: return blendRowT<BlendMode::M>(dst, src, count, opacity);
        PANE_CASE(Normal) PANE_CASE(Multiply) PANE_CASE(ColorBurn) PANE_CASE(ColorDodge) PANE_CASE(Reflect)
        PANE_CASE(Glow) PANE_CASE(Overlay) PANE_CASE(Difference) PANE_CASE(Negation) PANE_CASE(Lighten)
        PANE_CASE(Darken) PANE_CASE(Screen) PANE_CASE(Xor) PANE_CASE(Additive)
#undef PANE_CASE
    }
}

} // namespace pane
