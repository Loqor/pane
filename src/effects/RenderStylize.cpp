#include "core/BlendMode.h"
#include "core/Glyphs.h"
#include "effects/EffectUtil.h"

#include <QColor>

namespace pane {

using namespace fx;

namespace {

uint8_t clamp8(double v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); }

QStringList blendNames()
{
    QStringList l;
    for (int i = 0; i < BlendModeCount; ++i) l << blendModeName(BlendMode(i));
    return l;
}

// Smooth fractal coloring: a hue cycle by normalized iteration count.
Pixel fractalColor(double t, double factor, bool invert)
{
    if (t < 0) return invert ? 0xFFFFFFFF : 0xFF000000;
    double h = std::fmod(t * factor / 40.0, 1.0);
    QColor c = QColor::fromHsvF(float(h), 0.85f, float(std::clamp(0.4 + t / 60.0, 0.0, 1.0)));
    Pixel p = pixelFromQColor(c);
    return invert ? (p & 0xFF000000u) | (~p & 0x00FFFFFFu) : p;
}

// Directional derivative over a 3x3 neighborhood, per channel.
std::array<double, 3> directional(const Region &reg, int x, int y, double angleDeg)
{
    double a = angleDeg * M_PI / 180.0;
    double cx = std::cos(a), cy = -std::sin(a);
    std::array<double, 3> acc{};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            double w = (dx * cx + dy * cy);
            Pixel q = reg.at(x + dx, y + dy);
            acc[0] += w * pxR(q);
            acc[1] += w * pxG(q);
            acc[2] += w * pxB(q);
        }
    return acc;
}

} // namespace

void registerRenderEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Render");
    add(r, "Clouds", "Clouds", cat, glyphs::FxRender,
        {PropertySpec::integer("scale", "Scale", 2, 1000, 250), PropertySpec::real("power", "Roughness", 0, 1, 0.5, 2),
         PropertySpec::choice("blend", "Blend mode", blendNames(), 0), PropertySpec::seed()},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double scale = pint(p, "scale"), power = preal(p, "power");
            auto mode = BlendMode(pint(p, "blend"));
            uint32_t seed = uint32_t(pint(p, "seed"));
            Pixel c0 = primaryColor(p), c1 = secondaryColor(p);
            int octaves = std::clamp(int(std::log2(scale)) + 1, 1, 10);
            return pixelFn([=](const Surface &src, int x, int y) {
                double n = fbm(x / scale, y / scale, seed, octaves, power);
                double t = std::clamp(n * 0.5 + 0.5, 0.0, 1.0);
                Pixel c = lerpPixel(c0, c1, uint32_t(std::lround(t * 255)));
                return blendPixel(mode, src.pixel(x, y), c, 255);
            });
        });

    auto fractalProps = [](bool mandel) {
        QList<PropertySpec> l{PropertySpec::integer("factor", "Factor", 1, 10, 4), PropertySpec::integer("quality", "Quality", 1, 5, 2),
                              PropertySpec::integer("zoom", "Zoom", 0, 50, mandel ? 10 : 1),
                              PropertySpec::integer("angle", "Angle", -180, 180, 0, QStringLiteral("°"))};
        if (mandel) l << PropertySpec::boolean("invert", "Invert colors", false);
        return l;
    };
    auto fractal = [](bool mandel) {
        return [mandel](const QVariantMap &p, const Surface &, const Selection &sel) {
            QRectF b = sel.boundsOrImage();
            double factor = pint(p, "factor"), zoom = std::max(1, pint(p, "zoom")) / (mandel ? 10.0 : 1.0);
            int q = pint(p, "quality");
            double ang = pint(p, "angle") * M_PI / 180.0;
            bool invert = mandel && pbool(p, "invert");
            QPointF c = b.center();
            double scale = 2.5 / (std::min(b.width(), b.height()) * zoom);
            return std::static_pointer_cast<EffectInstance>(std::make_shared<FnInstance>(
                [=](const Surface &, const QRect &rect, Pixel *dst, int stride) {
                    for (int y = rect.top(); y <= rect.bottom(); ++y)
                        for (int x = rect.left(); x <= rect.right(); ++x) {
                            PF acc;
                            for (int j = 0; j < q; ++j)
                                for (int i = 0; i < q; ++i) {
                                    double px = (x + (i + 0.5) / q - c.x()) * scale, py = (y + (j + 0.5) / q - c.y()) * scale;
                                    double rx = px * std::cos(ang) - py * std::sin(ang), ry = px * std::sin(ang) + py * std::cos(ang);
                                    double zr, zi, cr, ci;
                                    if (mandel) { zr = zi = 0; cr = rx - 0.6; ci = ry; }
                                    else { zr = rx; zi = ry; cr = -0.7; ci = 0.27015; }
                                    int it = 0;
                                    const int maxIt = 256;
                                    while (zr * zr + zi * zi < 16 && it < maxIt) {
                                        double t = zr * zr - zi * zi + cr;
                                        zi = 2 * zr * zi + ci;
                                        zr = t;
                                        ++it;
                                    }
                                    double sm = it >= maxIt ? -1 : it + 1 - std::log2(std::log2(zr * zr + zi * zi) / 2);
                                    acc += toPF(fractalColor(sm, factor, invert));
                                }
                            dst[size_t(y - rect.top()) * stride + (x - rect.left())] = fromPF(acc * (1.0f / float(q * q)));
                        }
                }));
        };
    };
    add(r, "JuliaFractal", "Julia Fractal", cat, glyphs::FxRender, fractalProps(false), fractal(false));
    add(r, "MandelbrotFractal", "Mandelbrot Fractal", cat, glyphs::FxRender, fractalProps(true), fractal(true));

    add(r, "Voronoi", "Voronoi Diagram", cat, glyphs::FxRender,
        {PropertySpec::choice("metric", "Distance metric", {"Euclidean", "Manhattan", "Chebyshev"}, 0),
         PropertySpec::integer("cells", "Number of cells", 2, 1000, 100), PropertySpec::seed()},
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QRect b = sel.boundsOrImage();
            int n = pint(p, "cells"), metric = pint(p, "metric");
            uint32_t seed = uint32_t(pint(p, "seed"));
            struct Cell { QPointF pt; Pixel color; };
            auto cells = std::make_shared<std::vector<Cell>>();
            for (int i = 0; i < n; ++i) {
                QPointF pt(b.left() + rand01(i, 0, seed, 1) * b.width(), b.top() + rand01(i, 0, seed, 2) * b.height());
                QColor c = QColor::fromHsvF(float(rand01(i, 0, seed, 3)), float(0.4 + 0.5 * rand01(i, 0, seed, 4)),
                                            float(0.6 + 0.4 * rand01(i, 0, seed, 5)));
                cells->push_back({pt, pixelFromQColor(c)});
            }
            return pixelFn([cells, metric](const Surface &, int x, int y) {
                double best = 1e300;
                Pixel col = 0;
                for (const Cell &c : *cells) {
                    double dx = std::abs(x + 0.5 - c.pt.x()), dy = std::abs(y + 0.5 - c.pt.y());
                    double d = metric == 0 ? dx * dx + dy * dy : metric == 1 ? dx + dy : std::max(dx, dy);
                    if (d < best) {
                        best = d;
                        col = c.color;
                    }
                }
                return col;
            });
        });
}

void registerStylizeEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Stylize");
    add(r, "EdgeDetect", "Edge Detect", cat, glyphs::FxStylize,
        {PropertySpec::integer("angle", "Angle", -180, 180, 45, QStringLiteral("°"))},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double ang = pint(p, "angle");
            return regionFn(1, [ang](const Region &reg, int x, int y) {
                auto d = directional(reg, x, y, ang);
                return makePixel(clamp8(128 + d[0] / 2), clamp8(128 + d[1] / 2), clamp8(128 + d[2] / 2), pxA(reg.at(x, y)));
            });
        });

    add(r, "Emboss", "Emboss", cat, glyphs::FxStylize, {PropertySpec::integer("angle", "Angle", -180, 180, 0, QStringLiteral("°"))},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double ang = pint(p, "angle");
            return regionFn(1, [ang](const Region &reg, int x, int y) {
                auto d = directional(reg, x, y, ang);
                uint8_t v = clamp8(128 + (0.299 * d[0] + 0.587 * d[1] + 0.114 * d[2]) / 2);
                return makePixel(v, v, v, pxA(reg.at(x, y)));
            });
        });

    add(r, "OutlineEdge", "Outline Edge", cat, glyphs::FxStylize,
        {PropertySpec::integer("thickness", "Thickness", 1, 200, 3), PropertySpec::integer("intensity", "Intensity", 0, 100, 50)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "thickness");
            double k = pint(p, "intensity") / 100.0;
            return regionFn(rad, [=](const Region &reg, int x, int y) {
                Pixel s = reg.at(x, y);
                int lo = 255, hi = 0;
                Pixel darkest = s;
                int step = std::max(1, rad / 6);
                for (int dy = -rad; dy <= rad; dy += step)
                    for (int dx = -rad; dx <= rad; dx += step) {
                        Pixel q = reg.at(x + dx, y + dy);
                        int i = intensityByte(q);
                        if (i < lo) {
                            lo = i;
                            darkest = q;
                        }
                        hi = std::max(hi, i);
                    }
                double strength = std::clamp((hi - lo) / 255.0 * 2 * k, 0.0, 1.0);
                return lerpPixel(s, withAlpha(darkest, pxA(s)), uint32_t(std::lround(strength * 255)));
            });
        });

    add(r, "Relief", "Relief", cat, glyphs::FxStylize, {PropertySpec::integer("angle", "Angle", -180, 180, 45, QStringLiteral("°"))},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double ang = pint(p, "angle");
            return regionFn(1, [ang](const Region &reg, int x, int y) {
                auto d = directional(reg, x, y, ang);
                Pixel s = reg.at(x, y);
                return makePixel(clamp8(pxR(s) + d[0] / 2), clamp8(pxG(s) + d[1] / 2), clamp8(pxB(s) + d[2] / 2), pxA(s));
            });
        });
}

} // namespace pane
