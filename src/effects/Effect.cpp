#include "effects/Effect.h"

namespace pane {

namespace {

template <typename F> void forEachRow(const Surface &src, const QRect &rect, Pixel *dst, int stride, F &&rowFn)
{
    for (int idx : src.tilesIn(rect)) {
        QRect tr = src.tileRect(idx);
        QRect part = tr.intersected(rect);
        const Tile *t = src.tile(idx);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            Pixel *out = dst + size_t(y - rect.top()) * stride + (part.left() - rect.left());
            if (t) rowFn(&t->px[(y - tr.top()) * TileSize + (part.left() - tr.left())], out, part.width());
            else rowFn(nullptr, out, part.width());
        }
    }
}

} // namespace

void PixelOpInstance::render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const
{
    const Pixel zeroOut = m_fn(0);
    forEachRow(src, rect, dst, stride, [&](const Pixel *in, Pixel *out, int n) {
        if (!in) {
            std::fill(out, out + n, zeroOut);
            return;
        }
        for (int i = 0; i < n; ++i) out[i] = m_fn(in[i]);
    });
}

LutInstance::LutInstance()
{
    for (int i = 0; i < 256; ++i) r[size_t(i)] = g[size_t(i)] = b[size_t(i)] = a[size_t(i)] = uint8_t(i);
}

void LutInstance::render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const
{
    forEachRow(src, rect, dst, stride, [&](const Pixel *in, Pixel *out, int n) {
        if (!in) {
            std::fill(out, out + n, apply(0));
            return;
        }
        for (int i = 0; i < n; ++i) out[i] = apply(in[i]);
    });
}

QVariantMap Effect::defaults() const { return propertyDefaults(properties()); }

EffectRegistry &EffectRegistry::instance()
{
    static EffectRegistry r;
    return r;
}

EffectRegistry::EffectRegistry()
{
    registerAdjustments(*this);
    // Effects submenus, alphabetical like paint.net.
    registerArtisticEffects(*this);
    registerBlurEffects(*this);
    registerColorEffects(*this);
    registerDistortEffects(*this);
    registerNoiseEffects(*this);
    registerObjectEffects(*this);
    registerPhotoEffects(*this);
    registerRenderEffects(*this);
    registerStylizeEffects(*this);
}

void EffectRegistry::add(std::unique_ptr<Effect> e)
{
    m_effects.push_back(e.get());
    m_owned.push_back(std::move(e));
}

const Effect *EffectRegistry::byId(const QString &id) const
{
    for (const Effect *e : m_effects)
        if (e->id() == id) return e;
    return nullptr;
}

} // namespace pane
