#include "core/Selection.h"

#include "core/MaskPath.h"

#include <QPainter>
#include <cstring>

namespace pane {

Selection &Selection::operator=(const Selection &o)
{
    if (this == &o) return *this;
    std::lock_guard lock(m_mutex);
    m_path = o.m_path;
    m_antialias = o.m_antialias;
    m_imageSize = o.m_imageSize;
    m_masks.clear();
    return *this;
}

void Selection::setImageSize(QSize s)
{
    std::lock_guard lock(m_mutex);
    m_imageSize = s;
    m_masks.clear();
}

void Selection::setPath(const QPainterPath &p, bool antialias)
{
    std::lock_guard lock(m_mutex);
    m_path = p;
    m_path.setFillRule(Qt::OddEvenFill);
    m_antialias = antialias;
    m_masks.clear();
}

void Selection::combine(const QPainterPath &p, CombineMode mode, bool antialias)
{
    // Vector boolean ops get very slow on big pixel-traced paths (Magic Wand);
    // combine those as masks and trace the result instead.
    if (mode != CombineMode::Replace && !m_imageSize.isEmpty() &&
        (m_path.elementCount() + p.elementCount() > 4000)) {
        const int w = m_imageSize.width(), h = m_imageSize.height();
        auto raster = [&](const QPainterPath &path) {
            QImage img(w, h, QImage::Format_Alpha8);
            img.fill(0);
            QPainter painter(&img);
            painter.fillPath(path, Qt::black);
            return img;
        };
        QImage a = m_path.isEmpty() ? QImage() : raster(m_path);
        QImage b = raster(p);
        std::vector<uint8_t> mask(size_t(w) * h);
        for (int y = 0; y < h; ++y) {
            const uchar *ra = a.isNull() ? nullptr : a.constScanLine(y);
            const uchar *rb = b.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                bool ia = ra && ra[x] >= 128, ib = rb[x] >= 128, out = false;
                switch (mode) {
                case CombineMode::Union: out = ia || ib; break;
                case CombineMode::Exclude: out = ia && !ib; break;
                case CombineMode::Intersect: out = ia && ib; break;
                case CombineMode::Xor: out = ia != ib; break;
                case CombineMode::Replace: out = ib; break;
                }
                mask[size_t(y) * w + x] = out ? 255 : 0;
            }
        }
        setPath(maskToPath(mask, w, QRect(0, 0, w, h)), false);
        return;
    }
    QPainterPath result;
    switch (mode) {
    case CombineMode::Replace: result = p; break;
    case CombineMode::Union: result = m_path.united(p); break;
    case CombineMode::Exclude: result = m_path.subtracted(p); break;
    case CombineMode::Intersect: result = m_path.intersected(p); break;
    case CombineMode::Xor: {
        result = m_path;
        result.addPath(p); // odd-even fill gives xor
        break;
    }
    }
    setPath(result.simplified(), antialias);
}

void Selection::transform(const QTransform &t) { setPath(t.map(m_path), m_antialias); }

void Selection::invert()
{
    QPainterPath all;
    all.addRect(QRectF(QPointF(0, 0), QSizeF(m_imageSize)));
    if (m_path.isEmpty()) {
        setPath({}, m_antialias); // inverting "everything" selects nothing; paint.net treats it as empty
        return;
    }
    QPainterPath p = m_path;
    p.addRect(QRectF(QPointF(0, 0), QSizeF(m_imageSize)));
    p.setFillRule(Qt::OddEvenFill);
    setPath(p, m_antialias);
}

QRect Selection::bounds() const
{
    if (m_path.isEmpty()) return {};
    return m_path.boundingRect().toAlignedRect().intersected(QRect(QPoint(), m_imageSize));
}

QRect Selection::boundsOrImage() const
{
    return m_path.isEmpty() ? QRect(QPoint(), m_imageSize) : bounds();
}

TileCoverage Selection::coverage(int tileIndex) const
{
    if (m_path.isEmpty()) return {TileCoverage::Full, nullptr};
    int tilesX = (m_imageSize.width() + TileSize - 1) >> TileShift;
    QRect tr = QRect((tileIndex % tilesX) * TileSize, (tileIndex / tilesX) * TileSize, TileSize, TileSize)
                   .intersected(QRect(QPoint(), m_imageSize));
    QRectF pb = m_path.boundingRect();
    if (!pb.intersects(QRectF(tr))) return {TileCoverage::None, nullptr};

    std::lock_guard lock(m_mutex);
    auto it = m_masks.find(tileIndex);
    if (it == m_masks.end()) {
        auto buf = std::make_unique<uint8_t[]>(TilePixels);
        std::memset(buf.get(), 0, TilePixels);
        QImage img(buf.get(), TileSize, TileSize, TileSize, QImage::Format_Alpha8);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, m_antialias);
            p.translate(-tr.left(), -tr.top());
            p.fillPath(m_path, Qt::black);
        }
        const uint8_t *m = buf.get();
        // Classify for fast paths.
        bool all = true, none = true;
        for (int y = 0; y < tr.height() && (all || none); ++y) {
            const uint8_t *row = m + y * TileSize;
            for (int x = 0; x < tr.width(); ++x) {
                if (row[x] != 255) all = false;
                if (row[x] != 0) none = false;
            }
        }
        TileCoverage::Kind kind = all ? TileCoverage::Full : none ? TileCoverage::None : TileCoverage::Partial;
        if (kind != TileCoverage::Partial) buf.reset();
        it = m_masks.emplace(tileIndex, CachedMask{kind, std::move(buf)}).first;
    }
    return {it->second.kind, it->second.mask.get()};
}

uint8_t Selection::coverageAt(int x, int y) const
{
    if (m_path.isEmpty()) return 255;
    if (m_antialias) {
        // Approximate: sample the rasterised tile.
        int tilesX = (m_imageSize.width() + TileSize - 1) >> TileShift;
        TileCoverage c = coverage((y >> TileShift) * tilesX + (x >> TileShift));
        if (c.kind == TileCoverage::Full) return 255;
        if (c.kind == TileCoverage::None) return 0;
        return c.mask[(y & (TileSize - 1)) * TileSize + (x & (TileSize - 1))];
    }
    return m_path.contains(QPointF(x + 0.5, y + 0.5)) ? 255 : 0;
}

} // namespace pane
