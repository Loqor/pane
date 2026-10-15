// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Document.h"

#include <QByteArray>
#include <optional>
#include <unordered_map>

namespace pane {

// Records the "before" state of every tile of one layer that is written during an
// operation, so it can be cancelled or turned into a PixelHistoryItem.
class LayerEdit {
public:
    LayerEdit(Document &doc, int layerIndex);

    Document &document() { return m_doc; }
    int layerIndex() const { return m_layer; }
    Surface &surface() { return m_doc.layer(m_layer).surface; }

    // Snapshots the tile on first touch and returns it for writing.
    Tile *writeTile(int index);
    // The tile as it was before the edit began (nullptr = transparent).
    const Tile *beforeTile(int index) const;
    bool isTouched(int index) const { return m_before.count(index) != 0; }
    // Snapshots the tile on first touch, then replaces it.
    void setTile(int index, ConstTilePtr tile);
    // Restores a touched tile to its original content.
    void restoreTile(int index);
    QRect touchedRect() const;

    void cancel();
    // Produces a history item (or nullptr if nothing changed). The selection
    // before/after can be attached for operations that also move the selection.
    std::unique_ptr<HistoryItem> finish(const QString &name, const QString &glyph,
                                        std::optional<Selection> selBefore = {},
                                        std::optional<Selection> selAfter = {});

private:
    Document &m_doc;
    int m_layer;
    std::unordered_map<int, ConstTilePtr> m_before;
};

// Undo record for pixel changes on one layer. Tile data is LZ4-compressed.
class PixelHistoryItem final : public HistoryItem {
public:
    struct TileDelta {
        int index;
        QByteArray before, after; // empty = transparent tile
    };
    PixelHistoryItem(QString name, QString glyph, int layer, std::vector<TileDelta> tiles,
                     std::optional<Selection> selBefore, std::optional<Selection> selAfter);

    void undo(Document &doc) override { apply(doc, true); }
    void redo(Document &doc) override { apply(doc, false); }
    size_t memoryBytes() const override;

    static QByteArray compressTile(const Tile *t);
    static ConstTilePtr decompressTile(const QByteArray &data);

private:
    void apply(Document &doc, bool before);
    int m_layer;
    std::vector<TileDelta> m_tiles;
    std::optional<Selection> m_selBefore, m_selAfter;
};

} // namespace pane
