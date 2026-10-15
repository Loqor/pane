#include "core/Glyphs.h"
#include "effects/EffectUtil.h"

namespace pane {

using namespace fx;

namespace {

const QString Cat = QStringLiteral("Distort");

QList<PropertySpec> withQuality(QList<PropertySpec> props, int def = 2)
{
    props << PropertySpec::integer("quality", "Quality", 1, 5, def);
    return props;
}

// Max/min filter over a square or disc.
Pixel morph(const Region &reg, int x, int y, int r, bool dilate, bool disc)
{
    int best[4] = {dilate ? 0 : 255, dilate ? 0 : 255, dilate ? 0 : 255, dilate ? 0 : 255};
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (disc && dx * dx + dy * dy > r * r) continue;
            Pixel q = reg.at(x + dx, y + dy);
            int v[4] = {pxR(q), pxG(q), pxB(q), pxA(q)};
            for (int c = 0; c < 4; ++c) best[c] = dilate ? std::max(best[c], v[c]) : std::min(best[c], v[c]);
        }
    return makePixel(uint32_t(best[0]), uint32_t(best[1]), uint32_t(best[2]), uint32_t(best[3]));
}

} // namespace

void registerDistortEffects(EffectRegistry &r)
{
    QList<PropertySpec> bulge{PropertySpec::integer("amount", "Bulge", -200, 100, 45)};
    bulge << centerProps();
    add(r, "Bulge", "Bulge", Cat, glyphs::FxDistort, withQuality(bulge),
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QRect b = sel.boundsOrImage();
            QPointF c = centerPoint(p, b);
            double R = std::min(b.width(), b.height()) / 2.0;
            double amount = pint(p, "amount") / 100.0;
            return mapInstance(pint(p, "quality"), [=](double x, double y, double &sx, double &sy) {
                double dx = x - c.x(), dy = y - c.y();
                double d = std::hypot(dx, dy) / R;
                double k = 1.0;
                if (d < 1.0) k = 1.0 - amount * (1.0 - d * d) * 0.5;
                sx = c.x() + dx * k;
                sy = c.y() + dy * k;
                return true;
            });
        });

    add(r, "Crystalize", "Crystalize", Cat, glyphs::FxDistort,
        {PropertySpec::integer("size", "Cell size", 2, 250, 8), PropertySpec::seed()},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int cell = pint(p, "size");
            uint32_t seed = uint32_t(pint(p, "seed"));
            return pixelFn([cell, seed](const Surface &src, int x, int y) {
                int cx = int(std::floor(double(x) / cell)), cy = int(std::floor(double(y) / cell));
                double best = 1e18;
                QPointF bp;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        int gx = cx + i, gy = cy + j;
                        QPointF pt((gx + rand01(gx, gy, seed, 1)) * cell, (gy + rand01(gx, gy, seed, 2)) * cell);
                        double d = std::pow(pt.x() - x - 0.5, 2) + std::pow(pt.y() - y - 0.5, 2);
                        if (d < best) {
                            best = d;
                            bp = pt;
                        }
                    }
                return src.pixelClamped(int(bp.x()), int(bp.y()));
            });
        });

    add(r, "Dents", "Dents", Cat, glyphs::FxDistort,
        withQuality({PropertySpec::integer("scale", "Scale", 1, 200, 25), PropertySpec::integer("refraction", "Refraction", 0, 200, 50),
                     PropertySpec::integer("roughness", "Roughness", 0, 100, 10), PropertySpec::integer("tension", "Tension", 0, 100, 10),
                     PropertySpec::seed()}),
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double scale = std::max(1, pint(p, "scale")), refr = pint(p, "refraction") / 10.0;
            int octaves = 1 + pint(p, "roughness") / 15;
            double tension = 1.0 + pint(p, "tension") / 25.0;
            uint32_t seed = uint32_t(pint(p, "seed"));
            return mapInstance(pint(p, "quality"), [=](double x, double y, double &sx, double &sy) {
                double nx = fbm(x / scale, y / scale, seed, octaves, 0.5);
                double ny = fbm(x / scale + 31.7, y / scale + 17.3, seed + 7, octaves, 0.5);
                auto shape = [tension](double v) { return std::copysign(std::pow(std::abs(v), 1.0 / tension), v); };
                sx = x + shape(nx) * refr * scale / 4;
                sy = y + shape(ny) * refr * scale / 4;
                return true;
            });
        });

    add(r, "FrostedGlass", "Frosted Glass", Cat, glyphs::FxDistort,
        {PropertySpec::real("scatter", "Scatter radius", 0, 200, 3, 1), PropertySpec::real("minScatter", "Minimum scatter", 0, 200, 0, 1),
         PropertySpec::integer("smoothness", "Smoothness", 1, 16, 2), PropertySpec::seed()},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double rmax = preal(p, "scatter"), rmin = std::min(preal(p, "minScatter"), rmax);
            int smooth = pint(p, "smoothness");
            uint32_t seed = uint32_t(pint(p, "seed"));
            return pixelFn([=](const Surface &src, int x, int y) {
                PF acc;
                for (int k = 0; k < smooth; ++k) {
                    double a = rand01(x, y, seed, uint32_t(2 * k)) * 2 * M_PI;
                    double d = rmin + (rmax - rmin) * std::sqrt(rand01(x, y, seed, uint32_t(2 * k + 1)));
                    acc += toPF(src.pixelClamped(int(std::floor(x + 0.5 + std::cos(a) * d)), int(std::floor(y + 0.5 + std::sin(a) * d))));
                }
                return fromPF(acc * (1.0f / float(smooth)));
            });
        });

    add(r, "Morphology", "Morphology", Cat, glyphs::FxDistort,
        {PropertySpec::choice("mode", "Mode", {"Dilate", "Erode", "Open", "Close"}, 0),
         PropertySpec::integer("radius", "Radius", 1, 100, 2), PropertySpec::choice("shape", "Shape", {"Square", "Circle"}, 1)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int mode = pint(p, "mode"), rad = pint(p, "radius");
            bool disc = pint(p, "shape") == 1;
            if (mode < 2)
                return regionFn(rad, [=](const Region &reg, int x, int y) { return morph(reg, x, y, rad, mode == 0, disc); });
            // Open = erode then dilate; Close = dilate then erode.
            bool first = mode == 3;
            return std::static_pointer_cast<EffectInstance>(std::make_shared<FnInstance>(
                [=](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                    QRect mid = rect.adjusted(-rad, -rad, rad, rad);
                    Region reg(src, mid.adjusted(-rad, -rad, rad, rad));
                    std::vector<Pixel> midPx(size_t(mid.width()) * mid.height());
                    for (int y = mid.top(); y <= mid.bottom(); ++y)
                        for (int x = mid.left(); x <= mid.right(); ++x)
                            midPx[size_t(y - mid.top()) * mid.width() + (x - mid.left())] = morph(reg, x, y, rad, first, disc);
                    auto at = [&](int x, int y) {
                        x = std::clamp(x, mid.left(), mid.right());
                        y = std::clamp(y, mid.top(), mid.bottom());
                        return midPx[size_t(y - mid.top()) * mid.width() + (x - mid.left())];
                    };
                    for (int y = rect.top(); y <= rect.bottom(); ++y)
                        for (int x = rect.left(); x <= rect.right(); ++x) {
                            int best[4] = {first ? 255 : 0, first ? 255 : 0, first ? 255 : 0, first ? 255 : 0};
                            for (int dy = -rad; dy <= rad; ++dy)
                                for (int dx = -rad; dx <= rad; ++dx) {
                                    if (disc && dx * dx + dy * dy > rad * rad) continue;
                                    Pixel q = at(x + dx, y + dy);
                                    int v[4] = {pxR(q), pxG(q), pxB(q), pxA(q)};
                                    for (int c = 0; c < 4; ++c) best[c] = first ? std::min(best[c], v[c]) : std::max(best[c], v[c]);
                                }
                            dst[size_t(y - rect.top()) * stride + (x - rect.left())] =
                                makePixel(uint32_t(best[0]), uint32_t(best[1]), uint32_t(best[2]), uint32_t(best[3]));
                        }
                }));
        });

    add(r, "Pixelate", "Pixelate", Cat, glyphs::FxDistort, {PropertySpec::integer("size", "Cell size", 1, 100, 2)},
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            int cell = pint(p, "size");
            QRect img = sel.boundsOrImage();
            QPoint org = img.topLeft();
            return std::static_pointer_cast<EffectInstance>(std::make_shared<FnInstance>(
                [cell, org](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                    auto cellOf = [&](int v, int o) { return int(std::floor(double(v - o) / cell)); };
                    QRect cells(QPoint(org.x() + cellOf(rect.left(), org.x()) * cell, org.y() + cellOf(rect.top(), org.y()) * cell),
                                QPoint(org.x() + (cellOf(rect.right(), org.x()) + 1) * cell - 1, org.y() + (cellOf(rect.bottom(), org.y()) + 1) * cell - 1));
                    Region reg(src, cells.intersected(src.bounds()));
                    for (int cy = cellOf(rect.top(), org.y()); cy <= cellOf(rect.bottom(), org.y()); ++cy)
                        for (int cx = cellOf(rect.left(), org.x()); cx <= cellOf(rect.right(), org.x()); ++cx) {
                            QRect c(org.x() + cx * cell, org.y() + cy * cell, cell, cell);
                            QRect ci = c.intersected(src.bounds());
                            PF acc;
                            for (int y = ci.top(); y <= ci.bottom(); ++y)
                                for (int x = ci.left(); x <= ci.right(); ++x) acc += toPF(reg.at(x, y));
                            Pixel v = fromPF(acc * (1.0f / float(std::max(1, ci.width() * ci.height()))));
                            QRect out = c.intersected(rect);
                            for (int y = out.top(); y <= out.bottom(); ++y)
                                for (int x = out.left(); x <= out.right(); ++x) dst[size_t(y - rect.top()) * stride + (x - rect.left())] = v;
                        }
                }));
        });

    QList<PropertySpec> polar{PropertySpec::real("amount", "Amount", -4, 4, 4, 2)};
    polar << centerProps();
    add(r, "PolarInversion", "Polar Inversion", Cat, glyphs::FxDistort, withQuality(polar),
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QRect b = sel.boundsOrImage();
            QPointF c = centerPoint(p, b);
            double amount = preal(p, "amount");
            double R = std::max(b.width(), b.height()) / 2.0;
            return mapInstance(pint(p, "quality"), [=](double x, double y, double &sx, double &sy) {
                double dx = (x - c.x()) / R, dy = (y - c.y()) / R;
                double d2 = dx * dx + dy * dy;
                if (d2 < 1e-9) return false;
                double k = amount / d2 / 4;
                sx = c.x() + dx * k * R;
                sy = c.y() + dy * k * R;
                // Wrap into the selection like paint.net's default edge behavior.
                sx = b.left() + std::fmod(std::fmod(sx - b.left(), b.width()) + b.width(), b.width());
                sy = b.top() + std::fmod(std::fmod(sy - b.top(), b.height()) + b.height(), b.height());
                return true;
            });
        });

    add(r, "TileReflection", "Tile Reflection", Cat, glyphs::FxDistort,
        withQuality({PropertySpec::integer("angle", "Angle", -180, 180, 30, QStringLiteral("°")),
                     PropertySpec::integer("size", "Tile size", 2, 200, 40), PropertySpec::integer("curvature", "Curvature", -100, 100, 8)}),
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            double a = pint(p, "angle") * M_PI / 180.0, size = pint(p, "size"), curv = pint(p, "curvature") / 100.0;
            QPointF c = QRectF(sel.boundsOrImage()).center();
            double cs = std::cos(a), sn = std::sin(a);
            return mapInstance(pint(p, "quality"), [=](double x, double y, double &sx, double &sy) {
                double dx = x - c.x(), dy = y - c.y();
                double u = dx * cs + dy * sn, v = -dx * sn + dy * cs;
                // Position within the tile in [-1, 1], with a curved (lens) profile.
                auto bend = [&](double t) {
                    double cell = std::floor(t / size + 0.5);
                    double f = (t / size - cell) * 2; // -1..1
                    double bent = f + curv * f * (1 - f * f) * 2;
                    return (cell + bent / 2) * size;
                };
                double bu = bend(u), bv = bend(v);
                sx = c.x() + bu * cs - bv * sn;
                sy = c.y() + bu * sn + bv * cs;
                return true;
            });
        });

    QList<PropertySpec> twist{PropertySpec::integer("amount", "Amount", -200, 200, 45), PropertySpec::real("size", "Size", 0.01, 2, 1, 2)};
    twist << centerProps();
    add(r, "Twist", "Twist", Cat, glyphs::FxDistort, withQuality(twist),
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QRect b = sel.boundsOrImage();
            QPointF c = centerPoint(p, b);
            double R = std::min(b.width(), b.height()) / 2.0 * preal(p, "size");
            double amount = pint(p, "amount") * M_PI / 180.0 * 2;
            return mapInstance(pint(p, "quality"), [=](double x, double y, double &sx, double &sy) {
                double dx = x - c.x(), dy = y - c.y();
                double d = std::hypot(dx, dy) / R;
                if (d >= 1) {
                    sx = x;
                    sy = y;
                    return true;
                }
                double t = amount * (1 - d) * (1 - d);
                double cs = std::cos(t), sn = std::sin(t);
                sx = c.x() + dx * cs - dy * sn;
                sy = c.y() + dx * sn + dy * cs;
                return true;
            });
        });
}

} // namespace pane
