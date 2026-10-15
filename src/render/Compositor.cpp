#include "render/Compositor.h"

#include <QPainter>
#include <QTimer>
#include <QtConcurrent/QtConcurrentMap>
#include <cstring>

namespace pane {

Compositor::Compositor(Document &doc, QObject *parent) : QObject(parent), m_doc(doc)
{
    rebuildGrid();
    connect(&doc, &Document::pixelsChanged, this, &Compositor::invalidate);
    connect(&doc, &Document::sizeChanged, this, [this] {
        rebuildGrid();
        invalidateAll();
    });
}

void Compositor::rebuildGrid()
{
    m_tilesX = (m_doc.width() + TileSize - 1) >> TileShift;
    m_tilesY = (m_doc.height() + TileSize - 1) >> TileShift;
    m_tiles.assign(size_t(m_tilesX) * m_tilesY, DisplayTile{});
    m_anyDirty = true;
}

void Compositor::invalidate(const QRect &region)
{
    QRect r = region.intersected(m_doc.bounds());
    if (r.isEmpty()) return;
    for (int ty = r.top() >> TileShift; ty <= r.bottom() >> TileShift; ++ty)
        for (int tx = r.left() >> TileShift; tx <= r.right() >> TileShift; ++tx)
            m_tiles[size_t(ty * m_tilesX + tx)].dirty = true;
    m_anyDirty = true;
    if (!m_updateScheduled) {
        m_updateScheduled = true;
        QTimer::singleShot(0, this, [this] {
            m_updateScheduled = false;
            update();
        });
    }
}

void Compositor::invalidateAll() { invalidate(m_doc.bounds()); }

QRect Compositor::update()
{
    if (!m_anyDirty) return {};
    m_anyDirty = false;
    std::vector<int> dirty;
    QRect region;
    for (int i = 0; i < int(m_tiles.size()); ++i) {
        if (!m_tiles[size_t(i)].dirty) continue;
        dirty.push_back(i);
        region |= QRect((i % m_tilesX) * TileSize, (i / m_tilesX) * TileSize, TileSize, TileSize);
    }
    if (dirty.empty()) return {};
    if (dirty.size() == 1) compositeTile(dirty[0]);
    else QtConcurrent::blockingMap(dirty, [this](int idx) { compositeTile(idx); });
    region &= m_doc.bounds();
    emit updated(region);
    return region;
}

namespace {

// Reuses `img` when it already has the right size (avoids per-update allocations).
void ensureImage(QImage &img, int w, int h)
{
    if (img.width() != w || img.height() != h) img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
}

void downsample(const QImage &src, QImage &dst)
{
    ensureImage(dst, (src.width() + 1) / 2, (src.height() + 1) / 2);
    const int sw = src.width(), sh = src.height();
    for (int y = 0; y < dst.height(); ++y) {
        auto *r0 = reinterpret_cast<const Pixel *>(src.constScanLine(std::min(2 * y, sh - 1)));
        auto *r1 = reinterpret_cast<const Pixel *>(src.constScanLine(std::min(2 * y + 1, sh - 1)));
        auto *out = reinterpret_cast<Pixel *>(dst.scanLine(y));
        for (int x = 0; x < dst.width(); ++x) {
            int x0 = std::min(2 * x, sw - 1), x1 = std::min(2 * x + 1, sw - 1);
            Pixel a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
            auto avg = [&](int shift) {
                return ((((a >> shift) & 255) + ((b >> shift) & 255) + ((c >> shift) & 255) + ((d >> shift) & 255) + 2) >> 2);
            };
            out[x] = (avg(24) << 24) | (avg(16) << 16) | (avg(8) << 8) | avg(0);
        }
    }
}

} // namespace

void Compositor::compositeTile(int index)
{
    DisplayTile &dt = m_tiles[size_t(index)];
    dt.dirty = false;
    const int tx = index % m_tilesX, ty = index / m_tilesX;
    const int w = std::min(TileSize, m_doc.width() - tx * TileSize);
    const int h = std::min(TileSize, m_doc.height() - ty * TileSize);

    thread_local std::vector<Pixel> scratch(TilePixels);
    Pixel *buf = scratch.data();
    bool any = false;
    for (int li = 0; li < m_doc.layerCount(); ++li) {
        const Layer &l = m_doc.layer(li);
        if (!l.props.visible || l.props.opacity == 0) continue;
        const Tile *src = l.surface.tile(index);
        if (!src) continue;
        if (!any) {
            std::memset(buf, 0, TilePixels * sizeof(Pixel));
            any = true;
        }
        for (int y = 0; y < h; ++y)
            blendRow(l.props.blendMode, &buf[y * TileSize], &src->px[y * TileSize], w, l.props.opacity);
    }
    if (!any) {
        for (auto &m : dt.mips) m = QImage();
        return;
    }
    QImage &img = dt.mips[0];
    ensureImage(img, w, h);
    for (int y = 0; y < h; ++y) {
        auto *out = reinterpret_cast<Pixel *>(img.scanLine(y));
        const Pixel *in = &buf[y * TileSize];
        for (int x = 0; x < w; ++x) out[x] = premultiply(in[x]);
    }
    for (int l = 1; l < MipLevels; ++l) downsample(dt.mips[size_t(l - 1)], dt.mips[size_t(l)]);
}

QImage Compositor::thumbnail(int maxSide) const
{
    QSize s = m_doc.size().scaled(maxSide, maxSide, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    double scale = double(s.width()) / m_doc.width();
    int level = 0;
    while (level + 1 < MipLevels && scale * (1 << (level + 1)) <= 1.0) ++level;
    QImage out(s, QImage::Format_ARGB32_Premultiplied);
    out.fill(0);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.scale(scale, scale);
    for (int i = 0; i < int(m_tiles.size()); ++i) {
        const QImage &m = m_tiles[size_t(i)].mips[size_t(level)];
        if (m.isNull()) continue;
        QRectF target((i % m_tilesX) * TileSize, (i / m_tilesX) * TileSize, m.width() * (1 << level), m.height() * (1 << level));
        p.drawImage(target, m);
    }
    return out;
}

QImage Compositor::layerThumbnail(const Surface &s, int maxSide)
{
    QSize ts = s.size().scaled(maxSide, maxSide, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    QImage out(ts, QImage::Format_ARGB32_Premultiplied);
    const double sx = double(s.width()) / ts.width(), sy = double(s.height()) / ts.height();
    for (int y = 0; y < ts.height(); ++y) {
        auto *row = reinterpret_cast<Pixel *>(out.scanLine(y));
        for (int x = 0; x < ts.width(); ++x) {
            // 2x2 supersample.
            uint32_t acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    Pixel p = premultiply(s.pixel(int((x + 0.25 + 0.5 * i) * sx), int((y + 0.25 + 0.5 * j) * sy)));
                    acc[0] += pxB(p); acc[1] += pxG(p); acc[2] += pxR(p); acc[3] += pxA(p);
                }
            row[x] = makePixel(acc[2] / 4, acc[1] / 4, acc[0] / 4, acc[3] / 4);
        }
    }
    return out;
}

} // namespace pane
