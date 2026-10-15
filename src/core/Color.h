// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QColor>
#include <QString>
#include <algorithm>
#include <cstdint>

namespace pane {

// Pixels are 32-bit BGRA in memory (0xAARRGGBB as a little-endian uint32), with
// straight (non-premultiplied) alpha, exactly like paint.net's ColorBgra. This is
// byte-compatible with QImage::Format_ARGB32.
using Pixel = uint32_t;

constexpr uint8_t pxB(Pixel p) { return uint8_t(p); }
constexpr uint8_t pxG(Pixel p) { return uint8_t(p >> 8); }
constexpr uint8_t pxR(Pixel p) { return uint8_t(p >> 16); }
constexpr uint8_t pxA(Pixel p) { return uint8_t(p >> 24); }
constexpr Pixel makePixel(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// x*y/255 with correct rounding, for 0..255 inputs.
constexpr uint32_t mul255(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 0x80;
    return ((t >> 8) + t) >> 8;
}

// Exact floor(num / d) for d in 1..255 and num <= 65535 via a reciprocal table
// (multiply + shift instead of a hardware divide). Verified exhaustively in tests.
namespace detail {
struct Reciprocals {
    uint64_t m[256]; // 64-bit: for d == 1 the reciprocal is 2^32 + 1
    constexpr Reciprocals() : m{}
    {
        for (uint32_t d = 1; d < 256; ++d) m[d] = ((uint64_t(1) << 32) / d) + 1;
    }
};
inline constexpr Reciprocals kReciprocals{};
} // namespace detail

inline uint32_t divSmall(uint32_t num, uint32_t d) { return uint32_t((uint64_t(num) * detail::kReciprocals.m[d]) >> 32); }

constexpr Pixel premultiply(Pixel p)
{
    uint32_t a = pxA(p);
    if (a == 255) return p;
    if (a == 0) return 0;
    return makePixel(mul255(pxR(p), a), mul255(pxG(p), a), mul255(pxB(p), a), a);
}

constexpr Pixel unpremultiply(Pixel p)
{
    uint32_t a = pxA(p);
    if (a == 255) return p;
    if (a == 0) return 0;
    auto un = [a](uint32_t c) { return std::min<uint32_t>(255, (c * 255 + a / 2) / a); };
    return makePixel(un(pxR(p)), un(pxG(p)), un(pxB(p)), a);
}

inline Pixel pixelFromQColor(const QColor &c)
{
    QColor r = c.toRgb();
    return makePixel(r.red(), r.green(), r.blue(), r.alpha());
}

inline QColor qcolorFromPixel(Pixel p)
{
    return QColor(pxR(p), pxG(p), pxB(p), pxA(p));
}

inline Pixel withAlpha(Pixel p, uint32_t a) { return (p & 0x00FFFFFFu) | (a << 24); }

// Linear interpolation of all four channels: t in 0..255.
inline Pixel lerpPixel(Pixel from, Pixel to, uint32_t t)
{
    auto l = [t](uint32_t a, uint32_t b) { return uint32_t((a * (255 - t) + b * t + 127) / 255); };
    return makePixel(l(pxR(from), pxR(to)), l(pxG(from), pxG(to)), l(pxB(from), pxB(to)),
                     l(pxA(from), pxA(to)));
}

// Source-over composite of straight-alpha pixels; srcAlpha overrides the alpha of src.
inline Pixel overPixel(Pixel dst, Pixel src, uint32_t srcAlpha)
{
    if (srcAlpha == 0) return dst;
    uint32_t dA = pxA(dst);
    if (srcAlpha == 255 || dA == 0) return withAlpha(src, srcAlpha);
    uint32_t y = mul255(dA, 255 - srcAlpha);
    uint32_t total = y + srcAlpha;
    auto ch = [&](uint32_t d, uint32_t s) { return divSmall(d * y + s * srcAlpha + total / 2, total); };
    return makePixel(ch(pxR(dst), pxR(src)), ch(pxG(dst), pxG(src)), ch(pxB(dst), pxB(src)), total);
}

inline QString pixelToHex(Pixel p, bool withAlphaChannel)
{
    QString s = QStringLiteral("%1%2%3")
                    .arg(pxR(p), 2, 16, QLatin1Char('0'))
                    .arg(pxG(p), 2, 16, QLatin1Char('0'))
                    .arg(pxB(p), 2, 16, QLatin1Char('0'));
    if (withAlphaChannel) s += QStringLiteral("%1").arg(pxA(p), 2, 16, QLatin1Char('0'));
    return s.toUpper();
}

} // namespace pane
