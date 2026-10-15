#include "tools/EditSession.h"

#include <QtConcurrent/QtConcurrentMap>
#include <cstring>
#include <unordered_set>

namespace pane {

void EditSession::render(const QRect &regionIn, const PixelFn &fn)
{
    Surface &surf = m_edit.surface();
    const QRect region = regionIn.intersected(surf.bounds());
    const QRect dirty = region | m_last;

    std::vector<int> regionTiles = surf.tilesIn(region);
    std::unordered_set<int> inRegion(regionTiles.begin(), regionTiles.end());
    // Tiles from the previous render that are no longer covered go back to their original state.
    for (int idx : surf.tilesIn(m_last))
        if (!inRegion.count(idx)) m_edit.restoreTile(idx);

    if (!region.isEmpty() && fn) {
        const Selection &sel = m_doc.selection();
        struct Job {
            int idx;
            Tile *dst;
            const Tile *before;
            TileCoverage cov;
        };
        std::vector<Job> jobs;
        jobs.reserve(regionTiles.size());
        for (int idx : regionTiles) {
            TileCoverage cov = sel.coverage(idx); // rasterize serially; cached for the workers
            if (cov.kind == TileCoverage::None) {
                m_edit.restoreTile(idx);
                continue;
            }
            Tile *dst = m_edit.writeTile(idx);
            jobs.push_back({idx, dst, m_edit.beforeTile(idx), cov});
        }
        auto work = [&](const Job &j) {
            QRect tr = surf.tileRect(j.idx);
            if (j.before) std::memcpy(j.dst->px, j.before->px, sizeof(Tile::px));
            else std::memset(j.dst->px, 0, sizeof(Tile::px));
            QRect part = tr.intersected(region);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                Pixel *row = &j.dst->px[(y - tr.top()) * TileSize];
                for (int x = part.left(); x <= part.right(); ++x) {
                    int lx = x - tr.left();
                    uint32_t c = j.cov.kind == TileCoverage::Full ? 255 : j.cov.mask[(y - tr.top()) * TileSize + lx];
                    if (!c) continue;
                    Pixel b = row[lx];
                    Pixel v = fn(x, y, b);
                    row[lx] = c == 255 ? v : lerpPixel(b, v, c);
                }
            }
        };
        if (jobs.size() <= 1) {
            for (auto &j : jobs) work(j);
        } else {
            QtConcurrent::blockingMap(jobs, work);
        }
    }
    m_last = region;
    if (!dirty.isEmpty()) m_doc.notifyPixelsChanged(dirty);
}

void EditSession::renderOverlay(const QImage &premulIn, QPoint at, PaintMode mode)
{
    if (premulIn.isNull()) return clear();
    const QImage premul = premulIn.format() == QImage::Format_ARGB32_Premultiplied
                              ? premulIn
                              : premulIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect region(at, premul.size());
    render(region, [&premul, at, mode](int x, int y, Pixel b) -> Pixel {
        Pixel s = reinterpret_cast<const Pixel *>(premul.constScanLine(y - at.y()))[x - at.x()];
        uint32_t a = pxA(s);
        if (!a) return b;
        Pixel straight = unpremultiply(s);
        if (mode == PaintMode::Overwrite) return lerpPixel(b, straight, a);
        if (mode == PaintMode::Erase) {
            uint32_t na = mul255(pxA(b), 255 - a);
            return na ? withAlpha(b, na) : 0;
        }
        return overPixel(b, straight, a);
    });
}

void EditSession::cancel()
{
    m_edit.cancel();
    m_last = {};
}

void EditSession::finish(const QString &name, const QString &glyph)
{
    if (auto item = m_edit.finish(name, glyph)) m_doc.history().push(std::move(item));
    m_last = {};
}

} // namespace pane
