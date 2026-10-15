#pragma once

#include "core/Color.h"

#include <QImage>
#include <QRect>
#include <memory>
#include <vector>

namespace pane {

constexpr int TileShift = 8;
constexpr int TileSize = 1 << TileShift; // 256
constexpr int TilePixels = TileSize * TileSize;

struct Tile {
    Pixel px[TilePixels];
};

using TilePtr = std::shared_ptr<Tile>;
using ConstTilePtr = std::shared_ptr<const Tile>;

// A width x height image stored as lazily allocated 256x256 tiles.
// A null tile is fully transparent (all zero). Tiles are shared copy-on-write, so
// copying a Surface or snapshotting a tile for undo is O(1) until a write happens.
// Writes must only happen on one thread at a time; concurrent reads are fine.
class Surface {
public:
    Surface() = default;
    Surface(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    QSize size() const { return {m_width, m_height}; }
    QRect bounds() const { return {0, 0, m_width, m_height}; }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    int tileCount() const { return m_tilesX * m_tilesY; }
    bool isNull() const { return m_width == 0 || m_height == 0; }

    int tileIndex(int tx, int ty) const { return ty * m_tilesX + tx; }
    QRect tileRect(int tx, int ty) const;
    QRect tileRect(int index) const { return tileRect(index % m_tilesX, index / m_tilesX); }
    // Indices of all tiles overlapping rect (clipped to bounds).
    std::vector<int> tilesIn(const QRect &rect) const;

    // Read access: nullptr means the tile is fully transparent.
    const Tile *tile(int index) const { return m_tiles[index].get(); }
    ConstTilePtr sharedTile(int index) const { return m_tiles[index]; }
    uint64_t generation(int index) const { return m_gens[index]; }

    // Allocates or detaches the tile and bumps its generation. Safe to call
    // concurrently for different indices.
    Tile *mutableTile(int index);
    void setTile(int index, ConstTilePtr tile);

    Pixel pixel(int x, int y) const;
    void setPixel(int x, int y, Pixel p);
    Pixel pixelClamped(int x, int y) const;

    void fill(Pixel p);
    void fillRect(const QRect &r, Pixel p);
    void clear() { fill(0); }

    // Conversion to/from QImage (Format_ARGB32, straight alpha).
    QImage toImage() const;
    QImage copyRect(const QRect &rect) const;
    void writeImage(const QImage &img, QPoint at = {});
    static Surface fromImage(const QImage &img);

    // Drops tiles that are entirely transparent.
    void compact();
    size_t allocatedBytes() const;

private:
    int m_width = 0, m_height = 0, m_tilesX = 0, m_tilesY = 0;
    std::vector<std::shared_ptr<const Tile>> m_tiles;
    std::vector<uint64_t> m_gens;
};

// Pointer to the pixel at (x, y) within a tile.
inline Pixel *tilePixel(Tile *t, int x, int y) { return &t->px[y * TileSize + x]; }

uint64_t nextGeneration();

} // namespace pane
