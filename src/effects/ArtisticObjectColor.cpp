// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Glyphs.h"
#include "effects/Adjustments.h"
#include "effects/EffectUtil.h"
#include "core/Quantize.h"

namespace pane {

using namespace fx;

namespace {

uint8_t clamp8(double v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); }

// Bounds of non-transparent pixels inside the selection.
QRect objectBounds(const Surface &src, const Selection &sel)
{
    QRect region = sel.boundsOrImage();
    int minX = INT_MAX, minY = INT_MAX, maxX = -1, maxY = -1;
    for (int idx : src.tilesIn(region)) {
        const Tile *t = src.tile(idx);
        if (!t) continue;
        QRect tr = src.tileRect(idx);
        QRect part = tr.intersected(region);
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x)
                if (pxA(t->px[(y - tr.top()) * TileSize + (x - tr.left())])) {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
    }
    return maxX < 0 ? QRect() : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

std::shared_ptr<EffectInstance> ditherTo(std::shared_ptr<PaletteMap> pm, double strength, double spread)
{
    return pixelFn([pm, strength, spread](const Surface &src, int x, int y) {
        Pixel s = src.pixel(x, y);
        double d = bayer8(x, y) * strength * spread;
        Pixel q = pm->map(int(pxR(s) + d), int(pxG(s) + d), int(pxB(s) + d));
        return withAlpha(q, pxA(s));
    });
}

// Median-cut palette from the selection's colors.
std::vector<Pixel> medianCut(const Surface &src, const Selection &sel, int count)
{
    std::vector<Pixel> colors;
    QRect region = sel.boundsOrImage();
    // Subsample large images.
    int64_t area = int64_t(region.width()) * region.height();
    int step = std::max(1, int(std::sqrt(double(area) / 200000.0)));
    for (int y = region.top(); y <= region.bottom(); y += step)
        for (int x = region.left(); x <= region.right(); x += step) {
            Pixel p = src.pixel(x, y);
            if (pxA(p)) colors.push_back(p | 0xFF000000u);
        }
    return medianCutPalette(std::move(colors), count);
}

} // namespace

void registerArtisticEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Artistic");
    add(r, "InkSketch", "Ink Sketch", cat, glyphs::FxArtistic,
        {PropertySpec::integer("ink", "Ink outline", 0, 99, 50), PropertySpec::integer("coloring", "Coloring", 0, 100, 50)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double ink = pint(p, "ink") / 99.0, coloring = pint(p, "coloring") / 100.0;
            return regionFn(1, [=](const Region &reg, int x, int y) {
                // Sobel edge magnitude on intensity.
                auto I = [&](int dx, int dy) { return double(intensityByte(reg.at(x + dx, y + dy))); };
                double gx = -I(-1, -1) - 2 * I(-1, 0) - I(-1, 1) + I(1, -1) + 2 * I(1, 0) + I(1, 1);
                double gy = -I(-1, -1) - 2 * I(0, -1) - I(1, -1) + I(-1, 1) + 2 * I(0, 1) + I(1, 1);
                double edge = std::clamp(std::hypot(gx, gy) / 255.0 * (0.5 + ink * 2), 0.0, 1.0);
                Pixel s = reg.at(x, y);
                uint8_t gray = intensityByte(s);
                // Wash out the color toward paper white, then lay ink on edges.
                auto wash = [&](int c) { return (gray + (c - gray) * coloring) * 0.35 + 255 * 0.65; };
                double k = 1.0 - edge;
                return makePixel(clamp8(wash(pxR(s)) * k), clamp8(wash(pxG(s)) * k), clamp8(wash(pxB(s)) * k), pxA(s));
            });
        });

    add(r, "OilPainting", "Oil Painting", cat, glyphs::FxArtistic,
        {PropertySpec::integer("brush", "Brush size", 1, 8, 3), PropertySpec::integer("coarseness", "Coarseness", 3, 255, 50)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "brush"), levels = pint(p, "coarseness");
            return regionFn(rad, [=](const Region &reg, int x, int y) {
                std::vector<int> count(size_t(levels), 0);
                std::vector<std::array<int, 4>> sum(size_t(levels), {0, 0, 0, 0});
                for (int dy = -rad; dy <= rad; ++dy)
                    for (int dx = -rad; dx <= rad; ++dx) {
                        Pixel q = reg.at(x + dx, y + dy);
                        int bin = intensityByte(q) * (levels - 1) / 255;
                        count[size_t(bin)]++;
                        auto &s = sum[size_t(bin)];
                        s[0] += pxR(q); s[1] += pxG(q); s[2] += pxB(q); s[3] += pxA(q);
                    }
                size_t best = size_t(std::max_element(count.begin(), count.end()) - count.begin());
                int n = count[best];
                auto &s = sum[best];
                return makePixel(uint32_t(s[0] / n), uint32_t(s[1] / n), uint32_t(s[2] / n), uint32_t(s[3] / n));
            });
        });

    add(r, "PencilSketch", "Pencil Sketch", cat, glyphs::FxArtistic,
        {PropertySpec::integer("tip", "Pencil tip size", 1, 20, 2), PropertySpec::integer("range", "Color range", -20, 20, 0)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double sigma = radiusToSigma(pint(p, "tip") * 2.0);
            int pad = gaussianPad(sigma), range = pint(p, "range");
            auto bc = brightnessContrastFn(range * 3, range * 2);
            return std::static_pointer_cast<EffectInstance>(std::make_shared<FnInstance>(
                [=](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                    QRect padded = rect.adjusted(-pad, -pad, pad, pad);
                    Region reg(src, padded);
                    FBuf inv;
                    inv.rect = padded;
                    inv.px.resize(size_t(padded.width()) * padded.height());
                    for (int y = padded.top(); y <= padded.bottom(); ++y)
                        for (int x = padded.left(); x <= padded.right(); ++x) {
                            float v = 255.0f - intensityByte(reg.at(x, y));
                            inv.at(x, y) = {v, v, v, 255};
                        }
                    gaussianBlur(inv, sigma);
                    for (int y = rect.top(); y <= rect.bottom(); ++y)
                        for (int x = rect.left(); x <= rect.right(); ++x) {
                            Pixel s = reg.at(x, y);
                            double g = intensityByte(s), b = std::min(254.0, double(inv.at(x, y).r));
                            double dodge = std::min(255.0, g * 255.0 / (255.0 - b));
                            uint8_t v = clamp8(dodge);
                            Pixel out = bc(makePixel(v, v, v, pxA(s)));
                            dst[size_t(y - rect.top()) * stride + (x - rect.left())] = out;
                        }
                }));
        });
}

void registerObjectEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Object");
    add(r, "AlignObject", "Align Object", cat, glyphs::FxObject,
        {PropertySpec::choice("align", "Alignment",
                              {"Top left", "Top", "Top right", "Left", "Center", "Right", "Bottom left", "Bottom", "Bottom right"}, 4)},
        [](const QVariantMap &p, const Surface &src, const Selection &sel) {
            QRect area = sel.boundsOrImage(), obj = objectBounds(src, sel);
            int a = pint(p, "align");
            QPoint delta;
            if (!obj.isEmpty()) {
                int ax = a % 3, ay = a / 3;
                int tx = ax == 0 ? area.left() : ax == 1 ? area.left() + (area.width() - obj.width()) / 2 : area.right() - obj.width() + 1;
                int ty = ay == 0 ? area.top() : ay == 1 ? area.top() + (area.height() - obj.height()) / 2 : area.bottom() - obj.height() + 1;
                delta = QPoint(tx - obj.left(), ty - obj.top());
            }
            QRect moved = obj.translated(delta);
            return pixelFn([obj, moved, delta](const Surface &s, int x, int y) -> Pixel {
                if (obj.isEmpty()) return s.pixel(x, y);
                return moved.contains(x, y) ? s.pixel(x - delta.x(), y - delta.y()) : 0;
            });
        });

    add(r, "DropShadow", "Drop Shadow", cat, glyphs::FxObject,
        {PropertySpec::integer("offsetX", "Offset X", -100, 100, 4), PropertySpec::integer("offsetY", "Offset Y", -100, 100, 4),
         PropertySpec::integer("radius", "Blur radius", 0, 100, 6), PropertySpec::integer("spread", "Spread", 0, 50, 0),
         PropertySpec::integer("opacity", "Opacity", 0, 100, 50, QStringLiteral("%")),
         PropertySpec::color("color", "Color", 0xFF000000u), PropertySpec::boolean("keep", "Keep original image", true)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            QPoint off(pint(p, "offsetX"), pint(p, "offsetY"));
            int spread = pint(p, "spread");
            double sigma = radiusToSigma(pint(p, "radius"));
            int pad = gaussianPad(sigma) + spread;
            double opacity = pint(p, "opacity") / 100.0;
            Pixel color = pcolor(p, "color");
            bool keep = pbool(p, "keep");
            return std::static_pointer_cast<EffectInstance>(std::make_shared<FnInstance>(
                [=](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                    QRect shadowSrc = rect.translated(-off).adjusted(-pad, -pad, pad, pad);
                    Region reg(src, shadowSrc);
                    FBuf a;
                    a.rect = shadowSrc;
                    a.px.resize(size_t(shadowSrc.width()) * shadowSrc.height());
                    for (int y = shadowSrc.top(); y <= shadowSrc.bottom(); ++y)
                        for (int x = shadowSrc.left(); x <= shadowSrc.right(); ++x) {
                            // Out-of-image pixels are transparent (no edge clamp for shadows).
                            float v = src.bounds().contains(x, y) ? float(pxA(reg.at(x, y))) : 0.0f;
                            if (spread) {
                                for (int dy = -spread; dy <= spread && v < 255; dy += std::max(1, spread / 4))
                                    for (int dx = -spread; dx <= spread; dx += std::max(1, spread / 4))
                                        if (src.bounds().contains(x + dx, y + dy)) v = std::max(v, float(pxA(reg.at(x + dx, y + dy))));
                            }
                            a.at(x, y) = {v, v, v, v};
                        }
                    gaussianBlur(a, sigma);
                    Region orig(src, rect);
                    for (int y = rect.top(); y <= rect.bottom(); ++y)
                        for (int x = rect.left(); x <= rect.right(); ++x) {
                            uint32_t sa = uint32_t(std::clamp(a.at(x - off.x(), y - off.y()).a * opacity * pxA(color) / 255.0, 0.0, 255.0));
                            Pixel shadow = withAlpha(color, sa);
                            Pixel o = orig.at(x, y);
                            dst[size_t(y - rect.top()) * stride + (x - rect.left())] = keep ? overPixel(shadow, o, pxA(o)) : shadow;
                        }
                }));
        });

    add(r, "OutlineObject", "Outline Object", cat, glyphs::FxObject,
        {PropertySpec::integer("thickness", "Thickness", 1, 200, 3), PropertySpec::integer("hardness", "Hardness", 0, 100, 75),
         PropertySpec::color("color", "Color", 0xFF000000u)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "thickness");
            double hard = pint(p, "hardness") / 100.0;
            Pixel color = pcolor(p, "color");
            return regionFn(rad + 1, [=](const Region &reg, int x, int y) {
                // Distance to the nearest opaque-ish pixel within the radius.
                double best = 1e9;
                int step = std::max(1, rad / 16);
                for (int dy = -rad; dy <= rad; dy += step)
                    for (int dx = -rad; dx <= rad; dx += step) {
                        if (pxA(reg.at(x + dx, y + dy)) < 128) continue;
                        best = std::min(best, std::hypot(double(dx), double(dy)));
                    }
                Pixel o = reg.at(x, y);
                if (best > rad) return o;
                double soft = rad * (1 - hard);
                double cov = soft > 0 ? std::clamp((rad - best) / soft, 0.0, 1.0) : 1.0;
                Pixel outline = withAlpha(color, uint32_t(std::lround(cov * pxA(color))));
                return overPixel(outline, o, pxA(o));
            });
        });
}

void registerColorEffects(EffectRegistry &r)
{
    const QString cat = QStringLiteral("Color");
    add(r, "Dithering", "Dithering", cat, glyphs::FxColor,
        {PropertySpec::choice("palette", "Palette", {"Black and white", "Grayscale (4 shades)", "Grayscale (16 shades)", "Web-safe (216)", "RGB 3-3-2 (256)"}, 0),
         PropertySpec::integer("strength", "Dithering strength", 0, 100, 100)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            std::vector<Pixel> pal;
            int which = pint(p, "palette");
            auto grays = [&](int n) {
                for (int i = 0; i < n; ++i) {
                    uint32_t v = uint32_t(i * 255 / (n - 1));
                    pal.push_back(makePixel(v, v, v, 255));
                }
            };
            double spread = 255.0;
            switch (which) {
            case 0: grays(2); break;
            case 1: grays(4); spread = 85; break;
            case 2: grays(16); spread = 17; break;
            case 3:
                for (int rr = 0; rr < 6; ++rr)
                    for (int g = 0; g < 6; ++g)
                        for (int b = 0; b < 6; ++b) pal.push_back(makePixel(uint32_t(rr * 51), uint32_t(g * 51), uint32_t(b * 51), 255));
                spread = 51;
                break;
            default:
                for (int rr = 0; rr < 8; ++rr)
                    for (int g = 0; g < 8; ++g)
                        for (int b = 0; b < 4; ++b) pal.push_back(makePixel(uint32_t(rr * 255 / 7), uint32_t(g * 255 / 7), uint32_t(b * 85), 255));
                spread = 36;
                break;
            }
            return ditherTo(std::make_shared<PaletteMap>(std::move(pal)), pint(p, "strength") / 100.0, spread);
        });

    add(r, "Quantize", "Quantize", cat, glyphs::FxColor,
        {PropertySpec::integer("colors", "Color count", 2, 256, 16), PropertySpec::integer("strength", "Dithering strength", 0, 100, 50)},
        [](const QVariantMap &p, const Surface &src, const Selection &sel) {
            int n = pint(p, "colors");
            auto pal = medianCut(src, sel, n);
            double spread = 255.0 / std::cbrt(double(std::max<size_t>(2, pal.size())));
            return ditherTo(std::make_shared<PaletteMap>(std::move(pal)), pint(p, "strength") / 100.0, spread);
        });
}

} // namespace pane
