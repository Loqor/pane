// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Color.h"

#include <QString>
#include <vector>

namespace pane {

constexpr int PaletteSize = 96;

// paint.net palette text format: one AARRGGBB hex color per line, ';' starts a
// comment. Short palettes are padded with white; extra colors are ignored.
std::vector<Pixel> parsePalette(const QString &text);
QString formatPalette(const std::vector<Pixel> &colors);

} // namespace pane
