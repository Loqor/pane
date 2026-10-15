// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QPainterPath>
#include <QRect>
#include <cstdint>
#include <vector>

namespace pane {

// Traces the pixel-boundary edges of `mask` (row-major, `stride` bytes per row;
// a pixel is inside when its value >= 128) within `bounds` into closed loops.
// The result uses odd-even fill and reproduces the mask exactly.
QPainterPath maskToPath(const std::vector<uint8_t> &mask, int stride, const QRect &bounds);

} // namespace pane
