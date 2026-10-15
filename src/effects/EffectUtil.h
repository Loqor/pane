// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

// Shared building blocks for effects. Everything here is thread-safe and
// tile-independent: results depend only on image coordinates, never on how
// the image is split into tiles.

#include "effects/Effect.h"

#include <QRect>
#include <cmath>
#include <functional>
#include <vector>

namespace pane::fx {

// Premultiplied float pixel (channels 0..255).
struct PF {
    float r = 0, g = 0, b = 0, a = 0;
    PF &operator+=(const PF &o) { r += o.r; g += o.g; b += o.b; a += o.a; return *this; }
    PF &operator-=(const PF &o) { r -= o.r; g -= o.g; b -= o.b; a -= o.a; return *this; }
    PF operator*(float k) const { return {r * k, g * k, b * k, a * k}; }
    PF operator+(const PF &o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
};

inline PF toPF(Pixel p)
{
    float a = pxA(p) / 255.0f;
    return {pxR(p) * a, pxG(p) * a, pxB(p) * a, float(pxA(p))};
}

inline Pixel fromPF(const PF &f)
{
    float a = std::clamp(f.a, 0.0f, 255.0f);
    if (a < 0.5f) return 0;
    float k = 255.0f / a;
    auto c = [&](float v) { return uint32_t(std::clamp(std::lround(v * k), 0L, 255L)); };
    return makePixel(c(f.r), c(f.g), c(f.b), uint32_t(std::lround(a)));
}

// A rectangle of source pixels; reads outside the image clamp to the edge.
class Region {
public:
    Region(const Surface &src, const QRect &rect);
    const QRect &rect() const { return m_rect; }
    Pixel at(int x, int y) const
    {
        x = std::clamp(x, m_rect.left(), m_rect.right());
        y = std::clamp(y, m_rect.top(), m_rect.bottom());
        return m_px[size_t(y - m_rect.top()) * m_rect.width() + (x - m_rect.left())];
    }

private:
    QRect m_rect;
    std::vector<Pixel> m_px;
};

// Premultiplied float buffer over a rectangle (image coordinates).
struct FBuf {
    QRect rect;
    std::vector<PF> px;
    PF &at(int x, int y) { return px[size_t(y - rect.top()) * rect.width() + (x - rect.left())]; }
    const PF &at(int x, int y) const { return px[size_t(y - rect.top()) * rect.width() + (x - rect.left())]; }
};
FBuf fetchF(const Surface &src, const QRect &rect);
// In-place blurs on an FBuf (edges clamp within the buffer).
void boxBlur(FBuf &b, int rx, int ry);
// Snaps a value to the 1/64 grid used by the exact sliding-window blurs.
PF quantize64(const PF &p);
void gaussianBlur(FBuf &b, double sigma);
// Padding needed so gaussianBlur(sigma) is exact in the interior.
int gaussianPad(double sigma);
inline double radiusToSigma(double r) { return std::max(0.3, r / 2.5); }

// Bilinear sample with clamp-to-edge.
Pixel sampleBilinear(const Surface &src, double x, double y);
PF sampleBilinearPF(const Surface &src, double x, double y);

// Hash-based random numbers.
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
inline double rand01(int x, int y, uint32_t seed, uint32_t k = 0)
{
    return hash3(uint32_t(x), uint32_t(y), seed * 0x9e3779b9u + k) / 4294967296.0;
}
// Gradient noise in [-1, 1].
double perlin(double x, double y, uint32_t seed);
// Fractal (fBm) noise in roughly [-1, 1].
double fbm(double x, double y, uint32_t seed, int octaves, double persistence);

// Rect render callback wrapped as an instance.
using RenderFn = std::function<void(const Surface &src, const QRect &rect, Pixel *dst, int stride)>;
class FnInstance : public EffectInstance {
public:
    explicit FnInstance(RenderFn fn) : m_fn(std::move(fn)) {}
    void render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const override { m_fn(src, rect, dst, stride); }

private:
    RenderFn m_fn;
};

// Builds an instance from a per-output-pixel function f(src, x, y) -> Pixel.
template <typename F> std::shared_ptr<EffectInstance> pixelFn(F f)
{
    return std::make_shared<FnInstance>([f](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            Pixel *row = dst + size_t(y - rect.top()) * stride;
            for (int x = rect.left(); x <= rect.right(); ++x) row[x - rect.left()] = f(src, x, y);
        }
    });
}

// Builds an instance that fetches a padded region and maps each pixel with
// f(region, x, y) -> Pixel.
template <typename F> std::shared_ptr<EffectInstance> regionFn(int pad, F f)
{
    return std::make_shared<FnInstance>([pad, f](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        Region reg(src, rect.adjusted(-pad, -pad, pad, pad));
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            Pixel *row = dst + size_t(y - rect.top()) * stride;
            for (int x = rect.left(); x <= rect.right(); ++x) row[x - rect.left()] = f(reg, x, y);
        }
    });
}

using Factory = std::function<std::shared_ptr<EffectInstance>(const QVariantMap &, const Surface &, const Selection &)>;

class LambdaEffect : public Effect {
public:
    LambdaEffect(QString id, QString name, QString category, QString glyph, QList<PropertySpec> props, Factory f)
        : m_id(std::move(id)), m_name(std::move(name)), m_cat(std::move(category)), m_glyph(std::move(glyph)),
          m_props(std::move(props)), m_factory(std::move(f)) {}
    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return m_cat; }
    QString glyph() const override { return m_glyph; }
    QList<PropertySpec> properties() const override { return m_props; }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &src, const Selection &sel) const override
    {
        return m_factory(p, src, sel);
    }

private:
    QString m_id, m_name, m_cat, m_glyph;
    QList<PropertySpec> m_props;
    Factory m_factory;
};

inline void add(EffectRegistry &r, QString id, QString name, QString category, QString glyph, QList<PropertySpec> props,
                Factory f)
{
    r.add(std::make_unique<LambdaEffect>(std::move(id), std::move(name), std::move(category), std::move(glyph),
                                         std::move(props), std::move(f)));
}

// Parameter helpers.
inline int pint(const QVariantMap &p, const char *k) { return p.value(QString::fromLatin1(k)).toInt(); }
inline double preal(const QVariantMap &p, const char *k) { return p.value(QString::fromLatin1(k)).toDouble(); }
inline bool pbool(const QVariantMap &p, const char *k) { return p.value(QString::fromLatin1(k)).toBool(); }
inline Pixel pcolor(const QVariantMap &p, const char *k) { return p.value(QString::fromLatin1(k)).toUInt(); }
inline Pixel primaryColor(const QVariantMap &p) { return p.value(QStringLiteral("_primary"), 0xFF000000u).toUInt(); }
inline Pixel secondaryColor(const QVariantMap &p) { return p.value(QStringLiteral("_secondary"), 0xFFFFFFFFu).toUInt(); }

// Center point from "centerX"/"centerY" (-1..1) relative to the selection bounds.
inline QPointF centerPoint(const QVariantMap &p, const QRect &b)
{
    return {b.left() + b.width() * (1 + preal(p, "centerX")) / 2.0, b.top() + b.height() * (1 + preal(p, "centerY")) / 2.0};
}
inline QList<PropertySpec> centerProps()
{
    return {PropertySpec::real("centerX", "Center X", -2, 2, 0, 2), PropertySpec::real("centerY", "Center Y", -2, 2, 0, 2)};
}

// Supersampled inverse-mapping distortion: map(x, y, &sx, &sy) returns false for
// "transparent" (outside). quality = samples per axis.
std::shared_ptr<EffectInstance> mapInstance(int quality, std::function<bool(double, double, double &, double &)> map);

} // namespace pane::fx
