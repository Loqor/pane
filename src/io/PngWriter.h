// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QByteArray>
#include <QImage>

namespace pane {

// Multithreaded PNG encoder (8-bit RGB/RGBA). Rows are filtered in parallel and
// the IDAT stream is deflated in parallel strips chained with a 32 KB preset
// dictionary (the pigz technique), producing a single standard zlib stream.
// `img` must be Format_ARGB32 (straight alpha).
QByteArray encodePngParallel(const QImage &img, bool withAlpha, double dpiX, double dpiY, int level = 6);

} // namespace pane
