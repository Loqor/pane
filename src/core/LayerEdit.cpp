// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/LayerEdit.h"

#include "core/Parallel.h"

#include <lz4.h>
#include <cstring>
#include <stdexcept>

namespace pane {

LayerEdit::LayerEdit(Document &doc, int layerIndex) : m_doc(doc), m_layer(layerIndex) {}

Tile *LayerEdit::writeTile(int index)
{
    if (!m_before.count(index)) m_before.emplace(index, surface().sharedTile(index));
    return surface().mutableTile(index);
}

const Tile *LayerEdit::beforeTile(int index) const
{
    auto it = m_before.find(index);
    if (it != m_before.end()) return it->second.get();
    return m_doc.layer(m_layer).surface.tile(index);
}

void LayerEdit::setTile(int index, ConstTilePtr tile)
{
    if (!m_before.count(index)) m_before.emplace(index, surface().sharedTile(index));
    surface().setTile(index, std::move(tile));
}

void LayerEdit::restoreTile(int index)
{
    auto it = m_before.find(index);
    if (it != m_before.end()) surface().setTile(index, it->second);
}

QRect LayerEdit::touchedRect() const
{
    QRect r;
    const Surface &s = m_doc.layer(m_layer).surface;
    for (auto &[idx, _] : m_before) r |= s.tileRect(idx);
    return r;
}

void LayerEdit::cancel()
{
    QRect r = touchedRect();
    for (auto &[idx, tile] : m_before) surface().setTile(idx, tile);
    m_before.clear();
    m_doc.notifyPixelsChanged(r);
}

std::unique_ptr<HistoryItem> LayerEdit::finish(const QString &name, const QString &glyph,
                                               std::optional<Selection> selBefore, std::optional<Selection> selAfter)
{
    Surface &s = surface();
    std::vector<std::pair<int, ConstTilePtr>> touched(m_before.begin(), m_before.end());
    // Compare and compress tiles in parallel (LZ4 dominates the cost of big edits).
    std::vector<std::optional<PixelHistoryItem::TileDelta>> results(touched.size());
    parallelFor(int(touched.size()), [&](int i) {
        auto &[idx, before] = touched[size_t(i)];
        const Tile *after = s.tile(idx);
        if (before.get() == after) return;
        if (before && after && std::memcmp(before->px, after->px, sizeof(Tile::px)) == 0) return;
        results[size_t(i)] = PixelHistoryItem::TileDelta{idx, PixelHistoryItem::compressTile(before.get()), PixelHistoryItem::compressTile(after)};
    });
    std::vector<PixelHistoryItem::TileDelta> deltas;
    for (auto &d : results)
        if (d) deltas.push_back(std::move(*d));
    m_before.clear();
    if (deltas.empty() && !selBefore) return nullptr;
    return std::make_unique<PixelHistoryItem>(name, glyph, m_layer, std::move(deltas), std::move(selBefore),
                                              std::move(selAfter));
}

PixelHistoryItem::PixelHistoryItem(QString name, QString glyph, int layer, std::vector<TileDelta> tiles,
                                   std::optional<Selection> selBefore, std::optional<Selection> selAfter)
    : HistoryItem(std::move(name), std::move(glyph)), m_layer(layer), m_tiles(std::move(tiles)),
      m_selBefore(std::move(selBefore)), m_selAfter(std::move(selAfter))
{
}

size_t PixelHistoryItem::memoryBytes() const
{
    size_t n = 0;
    for (auto &t : m_tiles) n += size_t(t.before.size() + t.after.size());
    return n;
}

QByteArray PixelHistoryItem::compressTile(const Tile *t)
{
    if (!t) return {};
    const int srcSize = int(sizeof(Tile::px));
    QByteArray out(LZ4_compressBound(srcSize), Qt::Uninitialized);
    int n = LZ4_compress_default(reinterpret_cast<const char *>(t->px), out.data(), srcSize, int(out.size()));
    if (n <= 0) throw std::runtime_error("LZ4 compression failed");
    out.resize(n);
    return out;
}

ConstTilePtr PixelHistoryItem::decompressTile(const QByteArray &data)
{
    if (data.isEmpty()) return nullptr;
    auto t = std::make_shared<Tile>();
    int n = LZ4_decompress_safe(data.constData(), reinterpret_cast<char *>(t->px), int(data.size()), int(sizeof(Tile::px)));
    if (n != int(sizeof(Tile::px))) throw std::runtime_error("Corrupt history tile");
    return t;
}

void PixelHistoryItem::apply(Document &doc, bool before)
{
    if (m_layer < doc.layerCount()) {
        Surface &s = doc.layer(m_layer).surface;
        std::vector<ConstTilePtr> tiles(m_tiles.size());
        parallelFor(int(m_tiles.size()), [&](int i) { tiles[size_t(i)] = decompressTile(before ? m_tiles[size_t(i)].before : m_tiles[size_t(i)].after); });
        QRect dirty;
        for (size_t i = 0; i < m_tiles.size(); ++i) {
            s.setTile(m_tiles[i].index, std::move(tiles[i]));
            dirty |= s.tileRect(m_tiles[i].index);
        }
        doc.notifyPixelsChanged(dirty);
    }
    const auto &sel = before ? m_selBefore : m_selAfter;
    if (sel) doc.setSelection(*sel);
}

} // namespace pane
