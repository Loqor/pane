// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Glyphs.h"
#include "effects/Adjustments.h"
#include "effects/EffectUtil.h"

namespace pane {

using namespace fx;

namespace {

// Per-channel percentile within a square window, using per-pixel histograms
// over the window (O(r^2) for small radii, sampled for large ones).
Pixel windowPercentile(const Region &reg, int x, int y, int r, int percentile)
{
    int hist[4][256] = {};
    int n = 0;
    int step = std::max(1, r / 12);
    for (int dy = -r; dy <= r; dy += step)
        for (int dx = -r; dx <= r; dx += step) {
            if (dx * dx + dy * dy > r * r + r) continue;
            Pixel q = reg.at(x + dx, y + dy);
            hist[0][pxR(q)]++;
            hist[1][pxG(q)]++;
            hist[2][pxB(q)]++;
            hist[3][pxA(q)]++;
            ++n;
        }
    int target = std::clamp(n * percentile / 100, 0, n - 1);
    uint32_t v[4];
    for (int c = 0; c < 4; ++c) {
        int acc = 0, i = 0;
        for (; i < 255; ++i) {
            acc += hist[c][i];
            if (acc > target) break;
        }
        v[c] = uint32_t(i);
    }
    return makePixel(v[0], v[1], v[2], v[3]);
}

std::shared_ptr<EffectInstance> blurThen(double radius, std::function<Pixel(Pixel orig, Pixel blurred, int x, int y)> combine)
{
    double sigma = radiusToSigma(radius);
    int pad = gaussianPad(sigma);
    return std::make_shared<FnInstance>([=](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        QRect padded = rect.adjusted(-pad, -pad, pad, pad);
        Region orig(src, rect);
        FBuf b = fetchF(src, padded);
        gaussianBlur(b, sigma);
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                dst[size_t(y - rect.top()) * stride + (x - rect.left())] = combine(orig.at(x, y), fromPF(b.at(x, y)), x, y);
    });
}

uint8_t clamp8(double v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); }

} // namespace

void registerNoiseEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Noise");
    add(r, "AddNoise", "Add Noise", cat, glyphs::FxNoise,
        {PropertySpec::integer("intensity", "Intensity", 0, 100, 64), PropertySpec::integer("saturation", "Color saturation", 0, 400, 100),
         PropertySpec::integer("coverage", "Coverage", 0, 100, 100), PropertySpec::seed()},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double intensity = pint(p, "intensity") / 100.0, sat = pint(p, "saturation") / 100.0, cov = pint(p, "coverage") / 100.0;
            uint32_t seed = uint32_t(pint(p, "seed"));
            return pixelFn([=](const Surface &src, int x, int y) {
                Pixel s = src.pixel(x, y);
                if (rand01(x, y, seed, 9) >= cov) return s;
                // Approximately Gaussian noise from summed uniforms.
                auto g = [&](uint32_t k) { return (rand01(x, y, seed, k) + rand01(x, y, seed, k + 1) + rand01(x, y, seed, k + 2) - 1.5) * 2; };
                double lum = g(0) * intensity * 100;
                double cr = g(3) * intensity * 100 * sat, cg = g(6) * intensity * 100 * sat, cb = g(10) * intensity * 100 * sat;
                return makePixel(clamp8(pxR(s) + lum + cr - (cg + cb) / 2), clamp8(pxG(s) + lum + cg - (cr + cb) / 2),
                                 clamp8(pxB(s) + lum + cb - (cr + cg) / 2), pxA(s));
            });
        });

    add(r, "Median", "Median", cat, glyphs::FxNoise,
        {PropertySpec::integer("radius", "Radius", 1, 200, 10), PropertySpec::integer("percentile", "Percentile", 0, 100, 50)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "radius"), pct = pint(p, "percentile");
            return regionFn(rad, [=](const Region &reg, int x, int y) { return windowPercentile(reg, x, y, rad, pct); });
        });

    add(r, "ReduceNoise", "Reduce Noise", cat, glyphs::FxNoise,
        {PropertySpec::integer("radius", "Radius", 0, 200, 6), PropertySpec::real("strength", "Strength", 0, 1, 0.4, 2)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = std::max(1, pint(p, "radius"));
            double k = preal(p, "strength");
            return regionFn(rad, [=](const Region &reg, int x, int y) {
                Pixel m = windowPercentile(reg, x, y, rad, 50), s = reg.at(x, y);
                return lerpPixel(s, withAlpha(m, pxA(s)), uint32_t(std::lround(k * 255)));
            });
        });
}

void registerPhotoEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Photo");
    add(r, "Glow", "Glow", cat, glyphs::FxPhoto,
        {PropertySpec::integer("radius", "Radius", 1, 20, 6), PropertySpec::integer("brightness", "Brightness", -100, 100, 10),
         PropertySpec::integer("contrast", "Contrast", -100, 100, 10)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            auto bc = brightnessContrastFn(pint(p, "brightness"), pint(p, "contrast"));
            return blurThen(pint(p, "radius") * 2, [bc](Pixel o, Pixel bl, int, int) {
                Pixel adj = bc(bl);
                auto screen = [](uint32_t a, uint32_t s) { return a + s - mul255(a, s); };
                return makePixel(screen(pxR(o), pxR(adj)), screen(pxG(o), pxG(adj)), screen(pxB(o), pxB(adj)), pxA(o));
            });
        });

    add(r, "RedEyeRemoval", "Red Eye Removal", cat, glyphs::FxPhoto,
        {PropertySpec::integer("tolerance", "Tolerance", 0, 100, 70), PropertySpec::integer("saturation", "Saturation percentage", 0, 100, 90)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double tol = pint(p, "tolerance") / 100.0, sat = pint(p, "saturation") / 100.0;
            return pixelFn([=](const Surface &src, int x, int y) {
                Pixel s = src.pixel(x, y);
                int rr = pxR(s), gg = pxG(s), bb = pxB(s);
                int other = std::max(gg, bb);
                // "Red" pixels: red clearly dominates.
                if (rr <= other || rr - other < (1.0 - tol) * 120) return s;
                double k = sat * std::min(1.0, (rr - other) / 80.0);
                int nr = int(std::lround(rr - (rr - (gg + bb) / 2.0) * k));
                return makePixel(uint32_t(std::clamp(nr, 0, 255)), uint32_t(gg), uint32_t(bb), pxA(s));
            });
        });

    add(r, "Sharpen", "Sharpen", cat, glyphs::FxPhoto, {PropertySpec::integer("amount", "Amount", 1, 20, 10)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double amount = pint(p, "amount") / 10.0;
            return blurThen(2.5, [amount](Pixel o, Pixel bl, int, int) {
                auto ch = [&](int a, int b) { return clamp8(a + (a - b) * amount); };
                return makePixel(ch(pxR(o), pxR(bl)), ch(pxG(o), pxG(bl)), ch(pxB(o), pxB(bl)), pxA(o));
            });
        });

    add(r, "SoftenPortrait", "Soften Portrait", cat, glyphs::FxPhoto,
        {PropertySpec::integer("softness", "Softness", 0, 10, 5), PropertySpec::integer("lighting", "Lighting", -20, 20, 0),
         PropertySpec::integer("warmth", "Warmth", 0, 20, 10)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int soft = pint(p, "softness"), light = pint(p, "lighting"), warm = pint(p, "warmth");
            auto bc = brightnessContrastFn(light * 2, -light);
            return blurThen(std::max(1, soft), [=](Pixel o, Pixel bl, int, int) {
                Pixel mix = lerpPixel(o, withAlpha(bl, pxA(o)), uint32_t(soft * 255 / 20));
                Pixel lit = bc(mix);
                // Warmth: blend toward a sepia tone of the pixel.
                uint8_t i = intensityByte(lit);
                LevelChannel lr{0, 255, 0.8, 0, 255}, lb{0, 255, 1.2, 0, 255};
                Pixel sep = makePixel(lr.apply(i), i, lb.apply(i), pxA(lit));
                return lerpPixel(lit, sep, uint32_t(warm * 255 / 40));
            });
        });

    QList<PropertySpec> vig = centerProps();
    vig << PropertySpec::real("radius", "Radius", 0.1, 4, 0.5, 2) << PropertySpec::real("density", "Density", 0, 1, 1, 2);
    add(r, "Vignette", "Vignette", cat, glyphs::FxPhoto, vig, [](const QVariantMap &p, const Surface &, const Selection &sel) {
        QRect b = sel.boundsOrImage();
        QPointF c = centerPoint(p, b);
        double R = std::hypot(b.width(), b.height()) / 2.0 * preal(p, "radius") * 2;
        double density = preal(p, "density");
        return pixelFn([=](const Surface &src, int x, int y) {
            Pixel s = src.pixel(x, y);
            double d = std::hypot(x + 0.5 - c.x(), y + 0.5 - c.y()) / R;
            double t = std::clamp(d, 0.0, 1.0);
            double k = 1.0 - density * t * t * (3 - 2 * t);
            return makePixel(clamp8(pxR(s) * k), clamp8(pxG(s) * k), clamp8(pxB(s) * k), pxA(s));
        });
    });
}

} // namespace pane
