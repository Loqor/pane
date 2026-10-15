// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Glyphs.h"
#include "effects/EffectUtil.h"

namespace pane {

using namespace fx;

namespace {

const QString Cat = QStringLiteral("Blurs");

// Circular (disc) average via per-row prefix sums: O(radius) per pixel.
FBuf discBlur(const FBuf &in, const QRect &out, int r)
{
    FBuf res;
    res.rect = out;
    res.px.resize(size_t(out.width()) * out.height());
    const int w = in.rect.width();
    // Prefix sums per row of the input.
    std::vector<PF> prefix(size_t(w + 1) * in.rect.height());
    for (int y = 0; y < in.rect.height(); ++y) {
        PF acc;
        PF *pr = &prefix[size_t(y) * (w + 1)];
        pr[0] = acc;
        for (int x = 0; x < w; ++x) {
            acc += quantize64(in.px[size_t(y) * w + x]);
            pr[x + 1] = acc;
        }
    }
    std::vector<int> half(size_t(2 * r + 1));
    int count = 0;
    for (int dy = -r; dy <= r; ++dy) {
        half[size_t(dy + r)] = int(std::floor(std::sqrt(double(r) * r - double(dy) * dy + 0.25)));
        count += 2 * half[size_t(dy + r)] + 1;
    }
    const float inv = 1.0f / float(count);
    for (int y = out.top(); y <= out.bottom(); ++y)
        for (int x = out.left(); x <= out.right(); ++x) {
            PF acc;
            for (int dy = -r; dy <= r; ++dy) {
                int ry = std::clamp(y + dy, in.rect.top(), in.rect.bottom()) - in.rect.top();
                int hw = half[size_t(dy + r)];
                int x0 = std::clamp(x - hw, in.rect.left(), in.rect.right()) - in.rect.left();
                int x1 = std::clamp(x + hw, in.rect.left(), in.rect.right()) - in.rect.left();
                const PF *pr = &prefix[size_t(ry) * (w + 1)];
                acc += pr[x1 + 1];
                acc -= pr[x0];
                // Clamped edges: approximate missing samples with the edge pixel.
                int missing = (2 * hw + 1) - (x1 - x0 + 1);
                if (missing > 0) acc += in.px[size_t(ry) * w + (x - hw < in.rect.left() ? 0 : w - 1)] * float(missing);
            }
            res.at(x, y) = acc * inv;
        }
    return res;
}

std::shared_ptr<EffectInstance> gaussian(double radius)
{
    double sigma = radiusToSigma(radius);
    int pad = gaussianPad(sigma);
    return std::make_shared<FnInstance>([sigma, pad, radius](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        FBuf b = fetchF(src, rect.adjusted(-pad, -pad, pad, pad));
        if (radius > 0) gaussianBlur(b, sigma);
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) dst[size_t(y - rect.top()) * stride + (x - rect.left())] = fromPF(b.at(x, y));
    });
}

// Averages samples of the source along offsets produced by `offsets(x, y)`.
template <typename Offsets> std::shared_ptr<EffectInstance> sampleAverage(Offsets offsets)
{
    return std::make_shared<FnInstance>([offsets](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        std::vector<QPointF> pts;
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) {
                pts.clear();
                offsets(x + 0.5, y + 0.5, pts);
                PF acc;
                for (QPointF p : pts) acc += sampleBilinearPF(src, p.x(), p.y());
                dst[size_t(y - rect.top()) * stride + (x - rect.left())] = pts.empty() ? src.pixel(x, y) : fromPF(acc * (1.0f / float(pts.size())));
            }
    });
}

} // namespace

void registerBlurEffects(EffectRegistry &r)
{
    add(r, "Bokeh", "Bokeh", Cat, glyphs::FxBlur,
        {PropertySpec::integer("radius", "Radius", 1, 100, 10), PropertySpec::real("exposure", "Exposure", -4, 4, 0, 2),
         PropertySpec::integer("granularity", "Granularity", 0, 6, 3)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "radius");
            double gamma = 2.2 + pint(p, "granularity") * 0.4; // emphasize highlights
            double gain = std::pow(2.0, preal(p, "exposure"));
            return std::make_shared<FnInstance>([rad, gamma, gain](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                FBuf in = fetchF(src, rect.adjusted(-rad, -rad, rad, rad));
                // Into a "light" space where highlights dominate the average.
                for (PF &f : in.px) {
                    if (f.a <= 0) continue;
                    float a = f.a / 255.0f;
                    auto lin = [&](float c) { return float(std::pow(c / 255.0f / a, gamma)) * 255.0f * a; };
                    f.r = lin(f.r);
                    f.g = lin(f.g);
                    f.b = lin(f.b);
                }
                FBuf out = discBlur(in, rect, rad);
                for (int y = rect.top(); y <= rect.bottom(); ++y)
                    for (int x = rect.left(); x <= rect.right(); ++x) {
                        PF f = out.at(x, y);
                        if (f.a > 0) {
                            float a = f.a / 255.0f;
                            auto back = [&](float c) {
                                return float(std::pow(std::clamp(c / 255.0f / a * gain, 0.0, 1.0), 1.0 / gamma)) * 255.0f * a;
                            };
                            f.r = back(f.r);
                            f.g = back(f.g);
                            f.b = back(f.b);
                        }
                        dst[size_t(y - rect.top()) * stride + (x - rect.left())] = fromPF(f);
                    }
            });
        });

    add(r, "Fragment", "Fragment", Cat, glyphs::FxBlur,
        {PropertySpec::integer("fragments", "Fragments", 2, 50, 4), PropertySpec::integer("distance", "Distance", 0, 100, 8),
         PropertySpec::integer("rotation", "Rotation", 0, 360, 0, QStringLiteral("°"))},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int n = pint(p, "fragments");
            double d = pint(p, "distance"), rot = pint(p, "rotation") * M_PI / 180.0;
            std::vector<QPointF> offs;
            for (int i = 0; i < n; ++i) {
                double a = rot + 2 * M_PI * i / n;
                offs.emplace_back(std::cos(a) * d, std::sin(a) * d);
            }
            return sampleAverage([offs](double x, double y, std::vector<QPointF> &pts) {
                for (QPointF o : offs) pts.emplace_back(x + o.x(), y + o.y());
            });
        });

    add(r, "GaussianBlur", "Gaussian Blur", Cat, glyphs::FxBlur, {PropertySpec::integer("radius", "Radius", 0, 200, 2)},
        [](const QVariantMap &p, const Surface &, const Selection &) { return gaussian(pint(p, "radius")); });

    add(r, "MotionBlur", "Motion Blur", Cat, glyphs::FxBlur,
        {PropertySpec::integer("angle", "Angle", -180, 180, 25, QStringLiteral("°")),
         PropertySpec::integer("distance", "Distance", 1, 200, 10), PropertySpec::boolean("centered", "Centered", true)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            double a = pint(p, "angle") * M_PI / 180.0, dist = pint(p, "distance");
            bool centered = pbool(p, "centered");
            int n = std::max(2, int(dist) + 1);
            QPointF dir(std::cos(a), -std::sin(a));
            return sampleAverage([=](double x, double y, std::vector<QPointF> &pts) {
                for (int i = 0; i < n; ++i) {
                    double t = double(i) / (n - 1) * dist - (centered ? dist / 2 : 0);
                    pts.emplace_back(x + dir.x() * t, y + dir.y() * t);
                }
            });
        });

    QList<PropertySpec> radialProps{PropertySpec::integer("angle", "Angle", 0, 360, 2, QStringLiteral("°"))};
    radialProps << centerProps() << PropertySpec::integer("quality", "Quality", 1, 5, 2);
    add(r, "RadialBlur", "Radial Blur", Cat, glyphs::FxBlur, radialProps,
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QPointF c = centerPoint(p, sel.boundsOrImage());
            double ang = pint(p, "angle") * M_PI / 180.0;
            int q = pint(p, "quality");
            return sampleAverage([=](double x, double y, std::vector<QPointF> &pts) {
                double dx = x - c.x(), dy = y - c.y();
                double rlen = std::hypot(dx, dy);
                int n = std::clamp(int(rlen * ang * q / 2) + 1, 1, 64 * q);
                for (int i = 0; i < n; ++i) {
                    double t = n == 1 ? 0 : (double(i) / (n - 1) - 0.5) * ang;
                    double cs = std::cos(t), sn = std::sin(t);
                    pts.emplace_back(c.x() + dx * cs - dy * sn, c.y() + dx * sn + dy * cs);
                }
            });
        });

    add(r, "SurfaceBlur", "Surface Blur", Cat, glyphs::FxBlur,
        {PropertySpec::integer("radius", "Radius", 1, 100, 6), PropertySpec::integer("threshold", "Threshold", 0, 100, 50)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "radius");
            double thr = std::max(1, pint(p, "threshold")) * 2.55;
            return regionFn(rad, [rad, thr](const Region &reg, int x, int y) {
                Pixel c = reg.at(x, y);
                PF acc;
                float wsum = 0;
                // Sparse sampling keeps large radii fast.
                int step = std::max(1, rad / 8);
                for (int dy = -rad; dy <= rad; dy += step)
                    for (int dx = -rad; dx <= rad; dx += step) {
                        if (dx * dx + dy * dy > rad * rad) continue;
                        Pixel q = reg.at(x + dx, y + dy);
                        double diff = (std::abs(int(pxR(q)) - int(pxR(c))) + std::abs(int(pxG(q)) - int(pxG(c))) +
                                       std::abs(int(pxB(q)) - int(pxB(c)))) / 3.0;
                        float wgt = float(std::max(0.0, 1.0 - diff / thr));
                        if (wgt <= 0) continue;
                        acc += toPF(q) * wgt;
                        wsum += wgt;
                    }
                return wsum > 0 ? fromPF(acc * (1.0f / wsum)) : c;
            });
        });

    add(r, "Unfocus", "Unfocus", Cat, glyphs::FxBlur, {PropertySpec::integer("radius", "Radius", 1, 200, 4)},
        [](const QVariantMap &p, const Surface &, const Selection &) {
            int rad = pint(p, "radius");
            return std::make_shared<FnInstance>([rad](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
                FBuf in = fetchF(src, rect.adjusted(-rad, -rad, rad, rad));
                FBuf out = discBlur(in, rect, rad);
                for (int y = rect.top(); y <= rect.bottom(); ++y)
                    for (int x = rect.left(); x <= rect.right(); ++x)
                        dst[size_t(y - rect.top()) * stride + (x - rect.left())] = fromPF(out.at(x, y));
            });
        });

    QList<PropertySpec> zoomProps{PropertySpec::integer("amount", "Zoom amount", 0, 100, 10)};
    zoomProps << centerProps();
    add(r, "ZoomBlur", "Zoom Blur", Cat, glyphs::FxBlur, zoomProps,
        [](const QVariantMap &p, const Surface &, const Selection &sel) {
            QPointF c = centerPoint(p, sel.boundsOrImage());
            double amount = pint(p, "amount") / 100.0;
            return sampleAverage([=](double x, double y, std::vector<QPointF> &pts) {
                double dx = x - c.x(), dy = y - c.y();
                int n = std::clamp(int(std::hypot(dx, dy) * amount / 2) + 1, 1, 96);
                for (int i = 0; i < n; ++i) {
                    double k = 1.0 - amount * (n == 1 ? 0 : double(i) / (n - 1));
                    pts.emplace_back(c.x() + dx * k, c.y() + dy * k);
                }
            });
        });
}

} // namespace pane
