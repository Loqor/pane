// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QImage>
#include <QString>

namespace pane {

enum class Resampling { BestQuality, Bicubic, Bilinear, NearestNeighbor };

QString resamplingName(Resampling r);

// Resamples a Format_ARGB32 (straight alpha) image. Filtering happens on
// premultiplied data so transparent pixels don't bleed color.
// BestQuality = super-sampling (area average) when shrinking, bicubic when enlarging.
QImage resampleImage(const QImage &src, QSize size, Resampling mode);

} // namespace pane
