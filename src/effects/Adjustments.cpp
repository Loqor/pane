// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "effects/Adjustments.h"

#include "core/Glyphs.h"

#include <QColor>
#include <cmath>

namespace pane {

// ------------------------------------------------------------ helpers

std::array<uint8_t, 256> splineTable(QList<QPointF> pts)
{
    std::sort(pts.begin(), pts.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    // Drop duplicate x.
    QList<QPointF> p;
    for (QPointF q : pts)
        if (p.isEmpty() || q.x() > p.last().x()) p << q;
    std::array<uint8_t, 256> out{};
    if (p.size() < 2) {
        for (int i = 0; i < 256; ++i) out[size_t(i)] = uint8_t(i);
        return out;
    }
    const int n = int(p.size());
    // Natural cubic spline second derivatives.
    std::vector<double> y2(size_t(n), 0.0), u(size_t(n), 0.0);
    for (int i = 1; i < n - 1; ++i) {
        double sig = (p[i].x() - p[i - 1].x()) / (p[i + 1].x() - p[i - 1].x());
        double q = sig * y2[size_t(i - 1)] + 2.0;
        y2[size_t(i)] = (sig - 1.0) / q;
        double d = (p[i + 1].y() - p[i].y()) / (p[i + 1].x() - p[i].x()) - (p[i].y() - p[i - 1].y()) / (p[i].x() - p[i - 1].x());
        u[size_t(i)] = (6.0 * d / (p[i + 1].x() - p[i - 1].x()) - sig * u[size_t(i - 1)]) / q;
    }
    for (int k = n - 2; k >= 0; --k) y2[size_t(k)] = y2[size_t(k)] * y2[size_t(k + 1)] + u[size_t(k)];
    for (int x = 0; x < 256; ++x) {
        double v;
        if (x <= p.first().x()) v = p.first().y();
        else if (x >= p.last().x()) v = p.last().y();
        else {
            int hi = 1;
            while (p[hi].x() < x) ++hi;
            int lo = hi - 1;
            double h = p[hi].x() - p[lo].x();
            double a = (p[hi].x() - x) / h, b = (x - p[lo].x()) / h;
            v = a * p[lo].y() + b * p[hi].y() + ((a * a * a - a) * y2[size_t(lo)] + (b * b * b - b) * y2[size_t(hi)]) * h * h / 6.0;
        }
        out[size_t(x)] = uint8_t(std::clamp(int(std::lround(v)), 0, 255));
    }
    return out;
}

uint8_t LevelChannel::apply(int v) const
{
    double t = inHi == inLo ? (v >= inHi ? 1.0 : 0.0) : std::clamp(double(v - inLo) / (inHi - inLo), 0.0, 1.0);
    if (exponent != 1.0) t = std::pow(t, exponent);
    return uint8_t(std::clamp(int(std::lround(outLo + (outHi - outLo) * t)), 0, 255));
}

Histogram Histogram::compute(const Surface &src, const Selection &sel)
{
    Histogram h;
    QRect region = sel.boundsOrImage();
    for (int idx : src.tilesIn(region)) {
        TileCoverage cov = sel.coverage(idx);
        if (cov.kind == TileCoverage::None) continue;
        QRect tr = src.tileRect(idx);
        QRect part = tr.intersected(region);
        const Tile *t = src.tile(idx);
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                int i = (y - tr.top()) * TileSize + (x - tr.left());
                if (cov.kind == TileCoverage::Partial && cov.mask[i] < 128) continue;
                Pixel p = t ? t->px[i] : 0;
                if (!pxA(p)) continue;
                h.rgb[0][pxR(p)]++;
                h.rgb[1][pxG(p)]++;
                h.rgb[2][pxB(p)]++;
                h.count++;
            }
    }
    return h;
}

std::array<LevelChannel, 3> autoLevels(const Histogram &h)
{
    std::array<LevelChannel, 3> out;
    if (!h.count) return out;
    for (int c = 0; c < 3; ++c) {
        const auto &hist = h.rgb[size_t(c)];
        auto percentile = [&](double f) {
            uint64_t target = uint64_t(f * double(h.count)), acc = 0;
            for (int i = 0; i < 256; ++i) {
                acc += hist[size_t(i)];
                if (acc > target) return i;
            }
            return 255;
        };
        double mean = 0;
        for (int i = 0; i < 256; ++i) mean += double(i) * double(hist[size_t(i)]);
        mean /= double(h.count);
        int lo = percentile(0.005), hi = percentile(0.995);
        LevelChannel &l = out[size_t(c)];
        l.inLo = lo;
        l.inHi = std::max(hi, lo + 1);
        if (lo < mean && mean < hi) {
            double m = (mean - lo) / double(hi - lo);
            l.exponent = std::clamp(std::log(0.5) / std::log(m), 0.1, 10.0);
        }
    }
    return out;
}

QVariantList levelsToVariant(const std::array<LevelChannel, 3> &l)
{
    QVariantList v;
    for (const LevelChannel &c : l) v << QVariantList{c.inLo, c.inHi, c.exponent, c.outLo, c.outHi};
    return v;
}

std::array<LevelChannel, 3> levelsFromVariant(const QVariantList &v)
{
    std::array<LevelChannel, 3> out;
    for (int i = 0; i < 3 && i < v.size(); ++i) {
        QVariantList c = v[i].toList();
        if (c.size() < 5) continue;
        out[size_t(i)] = {c[0].toInt(), c[1].toInt(), c[2].toDouble(), c[3].toInt(), c[4].toInt()};
    }
    return out;
}

QVariantList pointsToVariant(const QList<QPointF> &pts)
{
    QVariantList v;
    for (QPointF p : pts) v << QVariant(p);
    return v;
}

QList<QPointF> pointsFromVariant(const QVariantList &v)
{
    QList<QPointF> out;
    for (const QVariant &p : v) out << p.toPointF();
    return out;
}

Pixel hueSaturationLightness(Pixel p, int hue, int saturation, int lightness)
{
    // Saturation: scale each channel's distance from intensity (paint.net).
    int intensity = intensityByte(p);
    int satFactor = saturation * 1024 / 100;
    auto sat = [&](int c) { return std::clamp((intensity * 1024 + (c - intensity) * satFactor) >> 10, 0, 255); };
    int r = sat(pxR(p)), g = sat(pxG(p)), b = sat(pxB(p));
    // Hue rotation in HSV.
    if (hue != 0) {
        QColor c(r, g, b);
        float h, s, v;
        c.getHsvF(&h, &s, &v);
        if (h >= 0) {
            float nh = std::fmod(h + hue / 360.0f + 1.0f, 1.0f);
            c = QColor::fromHsvF(nh, s, v);
            r = c.red();
            g = c.green();
            b = c.blue();
        }
    }
    // Lightness: blend toward white or black.
    if (lightness != 0) {
        int a = std::abs(lightness) * 255 / 100;
        int target = lightness > 0 ? 255 : 0;
        auto l = [&](int ch) { return (ch * (255 - a) + target * a + 127) / 255; };
        r = l(r);
        g = l(g);
        b = l(b);
    }
    return makePixel(uint32_t(r), uint32_t(g), uint32_t(b), pxA(p));
}

namespace {

struct BcTable {
    int multiply = 1, divide = 1;
    std::vector<uint8_t> rgb; // 65536 entries, or 256 when divide == 0
};

BcTable makeBcTable(int brightness, int contrast)
{
    BcTable t;
    if (contrast < 0) {
        t.multiply = contrast + 100;
        t.divide = 100;
    } else if (contrast > 0) {
        t.multiply = 100;
        t.divide = 100 - contrast;
    }
    if (t.divide == 0) {
        t.rgb.resize(256);
        for (int i = 0; i < 256; ++i) t.rgb[size_t(i)] = i + brightness < 128 ? 0 : 255;
        return t;
    }
    t.rgb.resize(65536);
    for (int intensity = 0; intensity < 256; ++intensity) {
        int shift = t.divide == 100 ? (intensity - 127) * t.multiply / t.divide + 127 - intensity + brightness
                                    : (intensity - 127 + brightness) * t.multiply / t.divide + 127 - intensity;
        for (int col = 0; col < 256; ++col) t.rgb[size_t(intensity * 256 + col)] = uint8_t(std::clamp(col + shift, 0, 255));
    }
    return t;
}

Pixel applyBc(const BcTable &t, Pixel p)
{
    int i = intensityByte(p);
    if (t.divide == 0) {
        uint8_t v = t.rgb[size_t(i)];
        return makePixel(v, v, v, pxA(p));
    }
    size_t base = size_t(i) * 256;
    return makePixel(t.rgb[base + pxR(p)], t.rgb[base + pxG(p)], t.rgb[base + pxB(p)], pxA(p));
}

} // namespace

Pixel brightnessContrast(Pixel p, int brightness, int contrast) { return applyBc(makeBcTable(brightness, contrast), p); }

std::function<Pixel(Pixel)> brightnessContrastFn(int brightness, int contrast)
{
    auto t = std::make_shared<BcTable>(makeBcTable(brightness, contrast));
    return [t](Pixel p) { return applyBc(*t, p); };
}

namespace {

Pixel desaturate(Pixel p)
{
    uint32_t i = intensityByte(p);
    return makePixel(i, i, i, pxA(p));
}

// ------------------------------------------------------------ effects

class SimpleAdjustment : public Effect {
public:
    SimpleAdjustment(QString id, QString name, QString glyph, QKeySequence key)
        : m_id(std::move(id)), m_name(std::move(name)), m_glyph(std::move(glyph)), m_key(std::move(key)) {}
    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return QStringLiteral("Adjustments"); }
    QString glyph() const override { return m_glyph; }
    QKeySequence shortcut() const override { return m_key; }

private:
    QString m_id, m_name, m_glyph;
    QKeySequence m_key;
};

class InvertColors final : public SimpleAdjustment {
public:
    InvertColors() : SimpleAdjustment("InvertColors", "Invert Colors", glyphs::Invert, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)) {}
    std::shared_ptr<EffectInstance> instance(const QVariantMap &, const Surface &, const Selection &) const override
    {
        return std::make_shared<PixelOpInstance>([](Pixel p) { return (p & 0xFF000000u) | (~p & 0x00FFFFFFu); });
    }
};

class BlackAndWhite final : public SimpleAdjustment {
public:
    BlackAndWhite() : SimpleAdjustment("BlackAndWhite", "Black and White", glyphs::BlackWhite, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)) {}
    std::shared_ptr<EffectInstance> instance(const QVariantMap &, const Surface &, const Selection &) const override
    {
        return std::make_shared<PixelOpInstance>(desaturate);
    }
};

class Sepia final : public SimpleAdjustment {
public:
    Sepia() : SimpleAdjustment("Sepia", "Sepia", glyphs::Sepia, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)) {}
    std::shared_ptr<EffectInstance> instance(const QVariantMap &, const Surface &, const Selection &) const override
    {
        // Per-channel level gamma: R 0.8, G 1.0, B 1.2 (paint.net's sepia tone).
        std::array<uint8_t, 256> lr{}, lg{}, lb{};
        for (int i = 0; i < 256; ++i) {
            LevelChannel r{0, 255, 0.8, 0, 255}, g{0, 255, 1.0, 0, 255}, b{0, 255, 1.2, 0, 255};
            lr[size_t(i)] = r.apply(i);
            lg[size_t(i)] = g.apply(i);
            lb[size_t(i)] = b.apply(i);
        }
        return std::make_shared<PixelOpInstance>([lr, lg, lb](Pixel p) {
            uint8_t i = intensityByte(p);
            return makePixel(lr[i], lg[i], lb[i], pxA(p));
        });
    }
};

class AutoLevel final : public SimpleAdjustment {
public:
    AutoLevel() : SimpleAdjustment("AutoLevel", "Auto-Level", glyphs::AutoLevel, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)) {}
    std::shared_ptr<EffectInstance> instance(const QVariantMap &, const Surface &src, const Selection &sel) const override
    {
        auto levels = autoLevels(Histogram::compute(src, sel));
        auto lut = std::make_shared<LutInstance>();
        for (int i = 0; i < 256; ++i) {
            lut->r[size_t(i)] = levels[0].apply(i);
            lut->g[size_t(i)] = levels[1].apply(i);
            lut->b[size_t(i)] = levels[2].apply(i);
        }
        return lut;
    }
};

class BrightnessContrast final : public SimpleAdjustment {
public:
    BrightnessContrast() : SimpleAdjustment("BrightnessContrast", "Brightness / Contrast", glyphs::BrightContrast, {}) {}
    QList<PropertySpec> properties() const override
    {
        return {PropertySpec::integer("brightness", "Brightness", -100, 100, 0),
                PropertySpec::integer("contrast", "Contrast", -100, 100, 0)};
    }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &, const Selection &) const override
    {
        auto table = std::make_shared<BcTable>(makeBcTable(p.value("brightness").toInt(), p.value("contrast").toInt()));
        return std::make_shared<PixelOpInstance>([table](Pixel px) { return applyBc(*table, px); });
    }
};

class HueSaturation final : public SimpleAdjustment {
public:
    HueSaturation() : SimpleAdjustment("HueSaturation", "Hue / Saturation", glyphs::HueSat, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U)) {}
    QList<PropertySpec> properties() const override
    {
        return {PropertySpec::integer("hue", "Hue", -180, 180, 0, QStringLiteral("°")),
                PropertySpec::integer("saturation", "Saturation", 0, 200, 100),
                PropertySpec::integer("lightness", "Lightness", -100, 100, 0)};
    }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &, const Selection &) const override
    {
        int h = p.value("hue").toInt(), s = p.value("saturation").toInt(), l = p.value("lightness").toInt();
        return std::make_shared<PixelOpInstance>([h, s, l](Pixel px) { return hueSaturationLightness(px, h, s, l); });
    }
};

class Posterize final : public SimpleAdjustment {
public:
    Posterize() : SimpleAdjustment("Posterize", "Posterize", glyphs::Posterize, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)) {}
    QList<PropertySpec> properties() const override
    {
        return {PropertySpec::integer("red", "Red", 2, 64, 16), PropertySpec::integer("green", "Green", 2, 64, 16),
                PropertySpec::integer("blue", "Blue", 2, 64, 16), PropertySpec::boolean("linked", "Linked", true)};
    }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &, const Selection &) const override
    {
        auto lut = std::make_shared<LutInstance>();
        auto make = [](int levels, std::array<uint8_t, 256> &t) {
            levels = std::clamp(levels, 2, 64);
            for (int i = 0; i < 256; ++i) {
                int step = int(std::lround(double(i) * (levels - 1) / 255.0));
                t[size_t(i)] = uint8_t(std::lround(255.0 * step / (levels - 1)));
            }
        };
        bool linked = p.value("linked", true).toBool();
        int r = p.value("red").toInt();
        make(r, lut->r);
        make(linked ? r : p.value("green").toInt(), lut->g);
        make(linked ? r : p.value("blue").toInt(), lut->b);
        return lut;
    }
};

class Levels final : public SimpleAdjustment {
public:
    Levels() : SimpleAdjustment("Levels", "Levels", glyphs::Levels, QKeySequence(Qt::CTRL | Qt::Key_L)) {}
    UI ui() const override { return UI::Custom; }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &, const Selection &) const override
    {
        auto levels = levelsFromVariant(p.value("levels").toList());
        auto lut = std::make_shared<LutInstance>();
        for (int i = 0; i < 256; ++i) {
            lut->r[size_t(i)] = levels[0].apply(i);
            lut->g[size_t(i)] = levels[1].apply(i);
            lut->b[size_t(i)] = levels[2].apply(i);
        }
        return lut;
    }
};

class Curves final : public SimpleAdjustment {
public:
    Curves() : SimpleAdjustment("Curves", "Curves", glyphs::Curves, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)) {}
    UI ui() const override { return UI::Custom; }
    std::shared_ptr<EffectInstance> instance(const QVariantMap &p, const Surface &, const Selection &) const override
    {
        if (p.value("mode").toInt() == 0) {
            auto curve = splineTable(pointsFromVariant(p.value("luminosity").toList()));
            return std::make_shared<PixelOpInstance>([curve](Pixel px) {
                int l = intensityByte(px);
                int d = int(curve[size_t(l)]) - l;
                auto c = [d](int v) { return uint32_t(std::clamp(v + d, 0, 255)); };
                return makePixel(c(pxR(px)), c(pxG(px)), c(pxB(px)), pxA(px));
            });
        }
        auto lut = std::make_shared<LutInstance>();
        auto pts = [&](const char *k) {
            QVariantList v = p.value(QString::fromLatin1(k)).toList();
            return v.isEmpty() ? QList<QPointF>{{0, 0}, {255, 255}} : pointsFromVariant(v);
        };
        lut->r = splineTable(pts("red"));
        lut->g = splineTable(pts("green"));
        lut->b = splineTable(pts("blue"));
        return lut;
    }
};

} // namespace

void registerAdjustments(EffectRegistry &r)
{
    // paint.net's Adjustments menu order.
    r.add(std::make_unique<AutoLevel>());
    r.add(std::make_unique<BlackAndWhite>());
    r.add(std::make_unique<BrightnessContrast>());
    r.add(std::make_unique<Curves>());
    r.add(std::make_unique<HueSaturation>());
    r.add(std::make_unique<InvertColors>());
    r.add(std::make_unique<Levels>());
    r.add(std::make_unique<Posterize>());
    r.add(std::make_unique<Sepia>());
}

} // namespace pane
