#include "core/Paint.h"

#include "core/Parallel.h"

#include <cmath>
#include <cstring>

namespace pane {

CoverageStroke::CoverageStroke(Document &doc, int layer, Pixel color, PaintMode mode, bool clipToSelection)
    : m_edit(doc, layer), m_color(color), m_mode(mode), m_clip(clipToSelection)
{
}

Pixel CoverageStroke::compose(Pixel before, uint32_t c, Pixel color) const
{
    switch (m_mode) {
    case PaintMode::Blend: return overPixel(before, color, mul255(pxA(color), c));
    case PaintMode::Overwrite: return lerpPixel(before, color, c);
    case PaintMode::Erase: {
        uint32_t a = mul255(pxA(before), 255 - c);
        return a ? withAlpha(before, a) : 0;
    }
    }
    return before;
}

template <typename CovFn> void CoverageStroke::stamp(const QRect &area, CovFn &&covFn)
{
    Document &doc = m_edit.document();
    Surface &s = m_edit.surface();
    QRect r = area.intersected(s.bounds());
    if (r.isEmpty()) return;
    const Selection &sel = doc.selection();
    // Large stamps (fills, bucket) run per tile in parallel; shared state is
    // prepared serially first. Small dabs stay serial and lazy.
    struct Job {
        int idx;
        TileCoverage sc;
        uint8_t *cov;
        Tile *dst;
        const Tile *before;
    };
    std::vector<Job> jobs;
    const std::vector<int> tiles = s.tilesIn(r);
    const bool parallel = tiles.size() > 4;
    for (int idx : tiles) {
        TileCoverage sc = m_clip ? sel.coverage(idx) : TileCoverage{};
        if (sc.kind == TileCoverage::None) continue;
        auto &covBuf = m_cov[idx];
        if (!covBuf) {
            covBuf = std::make_unique<uint8_t[]>(TilePixels);
            std::memset(covBuf.get(), 0, TilePixels);
        }
        Job j{idx, sc, covBuf.get(), nullptr, nullptr};
        if (parallel) {
            j.dst = m_edit.writeTile(idx);
            j.before = m_edit.beforeTile(idx);
        }
        jobs.push_back(j);
    }
    auto work = [&](Job &j) {
        const int idx = j.idx;
        const TileCoverage sc = j.sc;
        uint8_t *covBuf = j.cov;
        QRect tr = s.tileRect(idx);
        QRect part = tr.intersected(r);
        Tile *dst = j.dst;
        const Tile *before = j.before;
        for (int y = part.top(); y <= part.bottom(); ++y) {
            int ly = y - tr.top();
            for (int x = part.left(); x <= part.right(); ++x) {
                int lx = x - tr.left();
                int i = ly * TileSize + lx;
                uint32_t c = covFn(x, y);
                if (c <= covBuf[i]) continue;
                covBuf[i] = uint8_t(c);
                if (sc.kind == TileCoverage::Partial) c = mul255(c, sc.mask[i]);
                if (!dst) {
                    dst = m_edit.writeTile(idx);
                    before = m_edit.beforeTile(idx);
                }
                Pixel b = before ? before->px[i] : 0;
                dst->px[i] = compose(b, c, m_colorFn ? m_colorFn(x, y, b) : m_color);
            }
        }
    };
    if (parallel) parallelFor(int(jobs.size()), [&](int i) { work(jobs[size_t(i)]); });
    else
        for (Job &j : jobs) work(j);
    m_dirty |= r;
}

void CoverageStroke::addDab(QPointF center, double radius, double hardness, bool antialias, double strength)
{
    strength = std::clamp(strength, 0.0, 1.0);
    radius = std::max(radius, 0.5);
    QRect area(int(std::floor(center.x() - radius - 1)), int(std::floor(center.y() - radius - 1)),
               int(std::ceil(radius * 2 + 3)), int(std::ceil(radius * 2 + 3)));
    const double inner = radius * std::clamp(hardness, 0.0, 1.0);
    const double r2 = radius * radius;
    stamp(area, [&](int x, int y) -> uint32_t {
        double dx = x + 0.5 - center.x(), dy = y + 0.5 - center.y();
        double d2 = dx * dx + dy * dy;
        if (!antialias) return d2 <= r2 ? uint32_t(std::lround(255 * strength)) : 0;
        double d = std::sqrt(d2);
        double edge = std::clamp(radius - d + 0.5, 0.0, 1.0);
        if (edge <= 0) return 0;
        double soft = 1.0;
        if (d > inner && radius > inner) {
            double t = std::clamp((d - inner) / (radius - inner), 0.0, 1.0);
            soft = 1.0 - t * t * (3 - 2 * t);
        }
        return uint32_t(std::lround(255.0 * edge * soft * strength));
    });
}

void CoverageStroke::addLine(QPointF from, QPointF to, double radius, double hardness, bool antialias, double spacing,
                             double strengthFrom, double strengthTo)
{
    double len = std::hypot(to.x() - from.x(), to.y() - from.y());
    spacing = std::max(spacing, 0.25);
    int steps = std::max(1, int(std::ceil(len / spacing)));
    for (int i = 1; i <= steps; ++i) {
        double t = double(i) / steps;
        addDab(from + (to - from) * t, radius, hardness, antialias, strengthFrom + (strengthTo - strengthFrom) * t);
    }
}

void CoverageStroke::addPixel(int x, int y)
{
    stamp(QRect(x, y, 1, 1), [](int, int) -> uint32_t { return 255; });
}

void CoverageStroke::addPixelLine(QPoint a, QPoint b)
{
    int dx = std::abs(b.x() - a.x()), sx = a.x() < b.x() ? 1 : -1;
    int dy = -std::abs(b.y() - a.y()), sy = a.y() < b.y() ? 1 : -1;
    int err = dx + dy;
    int x = a.x(), y = a.y();
    for (;;) {
        addPixel(x, y);
        if (x == b.x() && y == b.y()) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void CoverageStroke::addRect(const QRect &r)
{
    stamp(r, [](int, int) -> uint32_t { return 255; });
}

void CoverageStroke::addMask(const std::vector<uint8_t> &mask, const QRect &bounds)
{
    const int w = m_edit.document().width();
    stamp(bounds, [&](int x, int y) -> uint32_t { return mask[size_t(y) * w + x]; });
}

QRect CoverageStroke::takeDirty()
{
    QRect r = m_dirty;
    m_dirty = {};
    return r;
}

bool colorsSimilar(Pixel a, Pixel b, double tolerance)
{
    int tol = int(tolerance * tolerance * 256);
    int sum = 0, diff;
    diff = int(pxR(a)) - int(pxR(b));
    sum += (1 + diff * diff) * int(pxA(a)) / 256;
    diff = int(pxG(a)) - int(pxG(b));
    sum += (1 + diff * diff) * int(pxA(a)) / 256;
    diff = int(pxB(a)) - int(pxB(b));
    sum += (1 + diff * diff) * int(pxA(a)) / 256;
    diff = int(pxA(a)) - int(pxA(b));
    sum += diff * diff;
    return sum <= tol * tol * 4;
}

std::vector<uint8_t> floodMask(const Surface &sample, QPoint seed, double tolerance, bool global, QRect *bounds)
{
    const int w = sample.width(), h = sample.height();
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    QRect b;
    if (!sample.bounds().contains(seed)) {
        if (bounds) *bounds = b;
        return mask;
    }
    const Pixel target = sample.pixel(seed.x(), seed.y());

    // Similarity map for the whole image, computed row-parallel.
    std::vector<uint8_t> sim(static_cast<size_t>(w) * h);
    parallelFor(h, [&](int y) {
        uint8_t *out = &sim[size_t(y) * w];
        for (int tx = 0; tx < sample.tilesX(); ++tx) {
            const Tile *t = sample.tile(sample.tileIndex(tx, y >> TileShift));
            int x0 = tx * TileSize, n = std::min(TileSize, w - x0);
            if (!t) {
                std::memset(out + x0, colorsSimilar(target, 0, tolerance) ? 1 : 0, size_t(n));
                continue;
            }
            const Pixel *row = &t->px[(y & (TileSize - 1)) * TileSize];
            for (int i = 0; i < n; ++i) out[x0 + i] = colorsSimilar(target, row[i], tolerance) ? 1 : 0;
        }
    });

    if (global) {
        std::vector<int> rowMin(static_cast<size_t>(h), w), rowMax(static_cast<size_t>(h), -1);
        parallelFor(h, [&](int y) {
            const uint8_t *s = &sim[size_t(y) * w];
            uint8_t *m = &mask[size_t(y) * w];
            int lo = w, hi = -1;
            for (int x = 0; x < w; ++x) {
                m[x] = uint8_t(-int(s[x])); // 1 -> 255, branch-free
                if (s[x]) {
                    lo = std::min(lo, x);
                    hi = x;
                }
            }
            rowMin[size_t(y)] = lo;
            rowMax[size_t(y)] = hi;
        });
        int minX = w, minY = h, maxX = -1, maxY = -1;
        for (int y = 0; y < h; ++y)
            if (rowMax[size_t(y)] >= 0) {
                minX = std::min(minX, rowMin[size_t(y)]);
                maxX = std::max(maxX, rowMax[size_t(y)]);
                minY = std::min(minY, y);
                maxY = y;
            }
        if (maxX >= 0) b = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        if (bounds) *bounds = b;
        return mask;
    }

    auto similar = [&](int x, int y) { return sim[size_t(y) * w + x] != 0; };
    int minX = seed.x(), maxX = seed.x(), minY = seed.y(), maxY = seed.y();
    std::vector<QPoint> stack{seed};
    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();
        int y = p.y();
        int x = p.x();
        if (mask[size_t(y) * w + x] || !similar(x, y)) continue;
        int l = x, r = x;
        while (l > 0 && !mask[size_t(y) * w + l - 1] && similar(l - 1, y)) --l;
        while (r < w - 1 && !mask[size_t(y) * w + r + 1] && similar(r + 1, y)) ++r;
        std::memset(&mask[size_t(y) * w + l], 255, size_t(r - l + 1));
        minX = std::min(minX, l); maxX = std::max(maxX, r);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= h) continue;
            bool inRun = false;
            for (int xx = l; xx <= r; ++xx) {
                bool ok = !mask[size_t(ny) * w + xx] && similar(xx, ny);
                if (ok && !inRun) stack.push_back(QPoint(xx, ny));
                inRun = ok;
            }
        }
    }
    b = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    if (bounds) *bounds = b;
    return mask;
}

} // namespace pane
