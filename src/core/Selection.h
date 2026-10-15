// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Surface.h"

#include <QPainterPath>
#include <QTransform>
#include <mutex>
#include <unordered_map>

namespace pane {

enum class CombineMode { Replace, Union, Exclude, Intersect, Xor };

// Coverage of one tile by the selection.
struct TileCoverage {
    enum Kind { None, Full, Partial } kind = Full;
    const uint8_t *mask = nullptr; // TileSize*TileSize when Partial
};

// The selection is a vector path (odd-even fill) in image coordinates; an
// 8-bit coverage mask is rasterised lazily per tile. An empty selection means
// "the whole image" for the purpose of pixel operations.
class Selection {
public:
    Selection() = default;
    Selection(const Selection &o) : m_path(o.m_path), m_antialias(o.m_antialias), m_imageSize(o.m_imageSize) {}
    Selection &operator=(const Selection &o);

    bool isEmpty() const { return m_path.isEmpty(); }
    const QPainterPath &path() const { return m_path; }
    bool antialias() const { return m_antialias; }
    QRect bounds() const;           // pixel bounds, clipped to the image
    QRect boundsOrImage() const;    // bounds(), or the whole image if empty

    void setImageSize(QSize s);
    void setPath(const QPainterPath &p, bool antialias);
    void combine(const QPainterPath &p, CombineMode mode, bool antialias);
    void clear() { setPath({}, m_antialias); }
    void transform(const QTransform &t);
    void invert();

    // tileIndex uses the tile grid of a Surface the size of the image.
    TileCoverage coverage(int tileIndex) const;
    uint8_t coverageAt(int x, int y) const;

private:
    QPainterPath m_path;
    bool m_antialias = true;
    QSize m_imageSize;
    mutable std::mutex m_mutex;
    struct CachedMask {
        TileCoverage::Kind kind;
        std::unique_ptr<uint8_t[]> mask;
    };
    mutable std::unordered_map<int, CachedMask> m_masks;
};

} // namespace pane
