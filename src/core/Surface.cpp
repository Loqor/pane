#include "core/Surface.h"

#include <atomic>
#include <cstring>
#include <stdexcept>

namespace pane {

uint64_t nextGeneration()
{
    static std::atomic<uint64_t> g{1};
    return g.fetch_add(1, std::memory_order_relaxed);
}

Surface::Surface(int width, int height)
    : m_width(width), m_height(height),
      m_tilesX((width + TileSize - 1) >> TileShift),
      m_tilesY((height + TileSize - 1) >> TileShift)
{
    if (width < 0 || height < 0 || width > 65535 || height > 65535)
        throw std::invalid_argument("Surface size out of range");
    m_tiles.resize(size_t(m_tilesX) * m_tilesY);
    m_gens.assign(m_tiles.size(), nextGeneration());
}

QRect Surface::tileRect(int tx, int ty) const
{
    return QRect(tx * TileSize, ty * TileSize, TileSize, TileSize).intersected(bounds());
}

std::vector<int> Surface::tilesIn(const QRect &rect) const
{
    std::vector<int> out;
    QRect r = rect.intersected(bounds());
    if (r.isEmpty()) return out;
    int x0 = r.left() >> TileShift, x1 = r.right() >> TileShift;
    int y0 = r.top() >> TileShift, y1 = r.bottom() >> TileShift;
    out.reserve(size_t(x1 - x0 + 1) * (y1 - y0 + 1));
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx) out.push_back(tileIndex(tx, ty));
    return out;
}

Tile *Surface::mutableTile(int index)
{
    auto &slot = m_tiles[index];
    if (!slot) {
        auto t = std::make_shared<Tile>();
        std::memset(t->px, 0, sizeof(t->px));
        slot = t;
    } else if (slot.use_count() > 1) {
        slot = std::make_shared<Tile>(*slot);
    }
    m_gens[index] = nextGeneration();
    return const_cast<Tile *>(slot.get());
}

void Surface::setTile(int index, ConstTilePtr tile)
{
    m_tiles[index] = std::move(tile);
    m_gens[index] = nextGeneration();
}

Pixel Surface::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return 0;
    const Tile *t = m_tiles[tileIndex(x >> TileShift, y >> TileShift)].get();
    return t ? t->px[(y & (TileSize - 1)) * TileSize + (x & (TileSize - 1))] : 0;
}

Pixel Surface::pixelClamped(int x, int y) const
{
    return pixel(std::clamp(x, 0, m_width - 1), std::clamp(y, 0, m_height - 1));
}

void Surface::setPixel(int x, int y, Pixel p)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return;
    Tile *t = mutableTile(tileIndex(x >> TileShift, y >> TileShift));
    t->px[(y & (TileSize - 1)) * TileSize + (x & (TileSize - 1))] = p;
}

void Surface::fill(Pixel p)
{
    if (p == 0) {
        for (size_t i = 0; i < m_tiles.size(); ++i) {
            m_tiles[i].reset();
            m_gens[i] = nextGeneration();
        }
        return;
    }
    auto t = std::make_shared<Tile>();
    std::fill(std::begin(t->px), std::end(t->px), p);
    for (size_t i = 0; i < m_tiles.size(); ++i) {
        m_tiles[i] = t;
        m_gens[i] = nextGeneration();
    }
}

void Surface::fillRect(const QRect &rect, Pixel p)
{
    QRect r = rect.intersected(bounds());
    for (int idx : tilesIn(r)) {
        QRect tr = tileRect(idx);
        QRect part = tr.intersected(r);
        Tile *t = mutableTile(idx);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            Pixel *row = tilePixel(t, part.left() - tr.left(), y - tr.top());
            std::fill(row, row + part.width(), p);
        }
    }
}

QImage Surface::toImage() const { return copyRect(bounds()); }

QImage Surface::copyRect(const QRect &rect) const
{
    QImage img(rect.size(), QImage::Format_ARGB32);
    if (img.isNull()) throw std::bad_alloc();
    img.fill(0);
    QRect r = rect.intersected(bounds());
    for (int idx : tilesIn(r)) {
        const Tile *t = tile(idx);
        if (!t) continue;
        QRect tr = tileRect(idx);
        QRect part = tr.intersected(r);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            const Pixel *src = &t->px[(y - tr.top()) * TileSize + (part.left() - tr.left())];
            auto *dst = reinterpret_cast<Pixel *>(img.scanLine(y - rect.top())) + (part.left() - rect.left());
            std::memcpy(dst, src, size_t(part.width()) * 4);
        }
    }
    return img;
}

void Surface::writeImage(const QImage &source, QPoint at)
{
    QImage img = source.format() == QImage::Format_ARGB32 ? source : source.convertToFormat(QImage::Format_ARGB32);
    QRect r = QRect(at, img.size()).intersected(bounds());
    for (int idx : tilesIn(r)) {
        QRect tr = tileRect(idx);
        QRect part = tr.intersected(r);
        Tile *t = mutableTile(idx);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            auto *src = reinterpret_cast<const Pixel *>(img.constScanLine(y - at.y())) + (part.left() - at.x());
            std::memcpy(tilePixel(t, part.left() - tr.left(), y - tr.top()), src, size_t(part.width()) * 4);
        }
    }
}

Surface Surface::fromImage(const QImage &img)
{
    Surface s(img.width(), img.height());
    s.writeImage(img);
    s.compact();
    return s;
}

void Surface::compact()
{
    for (auto &slot : m_tiles) {
        if (!slot) continue;
        bool empty = true;
        for (Pixel p : slot->px) {
            if (p) { empty = false; break; }
        }
        if (empty) slot.reset();
    }
}

size_t Surface::allocatedBytes() const
{
    size_t n = 0;
    for (auto &t : m_tiles) n += t ? sizeof(Tile) : 0;
    return n;
}

} // namespace pane
