// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/BlendMode.h"
#include "core/Surface.h"

#include <QString>

namespace pane {

struct LayerProperties {
    QString name;
    bool visible = true;
    uint8_t opacity = 255;
    BlendMode blendMode = BlendMode::Normal;

    bool operator==(const LayerProperties &) const = default;
};

// Layers are cheap to copy: the Surface shares its tiles copy-on-write.
struct Layer {
    LayerProperties props;
    Surface surface;
};

} // namespace pane
