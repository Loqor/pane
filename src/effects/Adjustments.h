// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "effects/Effect.h"

#include <QPoint>
#include <QVariantList>
#include <functional>

namespace pane {

// Natural cubic spline through control points (x strictly increasing after
// sorting), sampled to a 256-entry table clamped to 0..255.
std::array<uint8_t, 256> splineTable(QList<QPointF> points);

// paint.net's Level operation for one channel: input range, gamma (exponent),
// output range. v' = outLo + (outHi - outLo) * pow((v - inLo) / (inHi - inLo), exponent)
struct LevelChannel {
    int inLo = 0, inHi = 255;
    double exponent = 1.0;
    int outLo = 0, outHi = 255;
    uint8_t apply(int v) const;
};

// Histogram over the pixels of `src` inside the selection.
struct Histogram {
    std::array<std::array<uint64_t, 256>, 3> rgb{}; // R, G, B
    uint64_t count = 0;
    static Histogram compute(const Surface &src, const Selection &sel);
};

// Auto-level parameters from the 0.5%/99.5% percentiles and mean (paint.net).
std::array<LevelChannel, 3> autoLevels(const Histogram &h);

// Serialization of Levels/Curves params into QVariantMap.
QVariantList levelsToVariant(const std::array<LevelChannel, 3> &l);
std::array<LevelChannel, 3> levelsFromVariant(const QVariantList &v);
QVariantList pointsToVariant(const QList<QPointF> &pts);
QList<QPointF> pointsFromVariant(const QVariantList &v);

// Pixel functions exposed for tests.
Pixel hueSaturationLightness(Pixel p, int hue, int saturation, int lightness);
Pixel brightnessContrast(Pixel p, int brightness, int contrast);
// Same operation with the lookup table built once (use this per pixel).
std::function<Pixel(Pixel)> brightnessContrastFn(int brightness, int contrast);

} // namespace pane
