// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Document.h"

#include <QImage>
#include <QObject>
#include <array>

namespace pane {

constexpr int MipLevels = 6; // 256, 128, 64, 32, 16, 8

// Maintains premultiplied display tiles (plus a mip chain per tile) of the
// flattened document. Dirty tiles are re-composited in parallel.
class Compositor : public QObject {
    Q_OBJECT
public:
    explicit Compositor(Document &doc, QObject *parent = nullptr);

    Document &document() { return m_doc; }

    // Premultiplied ARGB32 tile image at mip level (0 = full resolution).
    // Null image = fully transparent tile.
    const QImage &displayTile(int index, int level) const { return m_tiles[size_t(index)].mips[size_t(level)]; }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }

    void invalidate(const QRect &region);
    void invalidateAll();
    // Re-composites dirty tiles now. Returns the region updated.
    QRect update();
    bool hasPendingWork() const { return m_anyDirty; }

    // Flattened thumbnail fitting in maxSide x maxSide (premultiplied).
    QImage thumbnail(int maxSide) const;
    static QImage layerThumbnail(const Surface &s, int maxSide);

signals:
    void updated(const QRect &region);

private:
    void rebuildGrid();
    void compositeTile(int index);

    struct DisplayTile {
        std::array<QImage, MipLevels> mips;
        bool dirty = true;
    };
    Document &m_doc;
    int m_tilesX = 0, m_tilesY = 0;
    std::vector<DisplayTile> m_tiles;
    bool m_anyDirty = true;
    bool m_updateScheduled = false;
};

} // namespace pane
