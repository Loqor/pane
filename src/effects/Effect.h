#pragma once

#include "core/Property.h"
#include "core/Selection.h"
#include "core/Surface.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <array>
#include <functional>
#include <memory>

namespace pane {


// A configured, thread-safe renderer.
class EffectInstance {
public:
    virtual ~EffectInstance() = default;
    // Renders `rect` (image coordinates) into dst (row stride in pixels).
    // `src` is the unmodified layer.
    virtual void render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const = 0;
};

// Per-pixel function instance.
class PixelOpInstance : public EffectInstance {
public:
    explicit PixelOpInstance(std::function<Pixel(Pixel)> fn) : m_fn(std::move(fn)) {}
    void render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const override;

private:
    std::function<Pixel(Pixel)> m_fn;
};

// Per-channel 8-bit lookup table instance (alpha unchanged unless set).
class LutInstance : public EffectInstance {
public:
    std::array<uint8_t, 256> r{}, g{}, b{}, a{};
    bool useAlpha = false;
    LutInstance();
    void render(const Surface &src, const QRect &rect, Pixel *dst, int stride) const override;
    Pixel apply(Pixel p) const
    {
        return makePixel(r[pxR(p)], g[pxG(p)], b[pxB(p)], useAlpha ? a[pxA(p)] : pxA(p));
    }
};

class Effect {
public:
    enum class UI { None, Properties, Custom };
    virtual ~Effect() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString category() const = 0; // "Adjustments" or an Effects submenu
    virtual QString glyph() const = 0;
    virtual QKeySequence shortcut() const { return {}; }
    virtual UI ui() const { return properties().isEmpty() ? UI::None : UI::Properties; }
    virtual QList<PropertySpec> properties() const { return {}; }
    virtual std::shared_ptr<EffectInstance> instance(const QVariantMap &params, const Surface &src, const Selection &sel) const = 0;

    QVariantMap defaults() const;
};

class EffectRegistry {
public:
    static EffectRegistry &instance();
    const std::vector<const Effect *> &effects() const { return m_effects; }
    const Effect *byId(const QString &id) const;
    void add(std::unique_ptr<Effect> e);

private:
    EffectRegistry();
    std::vector<const Effect *> m_effects;
    std::vector<std::unique_ptr<Effect>> m_owned;
};

void registerAdjustments(EffectRegistry &r);
void registerArtisticEffects(EffectRegistry &r);
void registerBlurEffects(EffectRegistry &r);
void registerColorEffects(EffectRegistry &r);
void registerDistortEffects(EffectRegistry &r);
void registerNoiseEffects(EffectRegistry &r);
void registerObjectEffects(EffectRegistry &r);
void registerPhotoEffects(EffectRegistry &r);
void registerRenderEffects(EffectRegistry &r);
void registerStylizeEffects(EffectRegistry &r);

// paint.net intensity (luma) of a pixel.
inline uint8_t intensityByte(Pixel p) { return uint8_t((7471 * pxB(p) + 38470 * pxG(p) + 19595 * pxR(p)) >> 16); }

} // namespace pane
