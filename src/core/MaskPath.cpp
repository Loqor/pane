// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/MaskPath.h"

#include <unordered_map>

namespace pane {

QPainterPath maskToPath(const std::vector<uint8_t> &mask, int stride, const QRect &bounds)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    if (bounds.isEmpty()) return path;

    const int x0 = bounds.left(), y0 = bounds.top(), w = bounds.width(), h = bounds.height();
    auto inside = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        return mask[size_t(y + y0) * stride + (x + x0)] >= 128;
    };
    // Vertices are lattice points (0..w, 0..h). Directed edges keep "inside" on the right.
    const int vw = w + 1;
    auto key = [vw](int x, int y) { return y * vw + x; };
    std::unordered_multimap<int, int> next; // start vertex -> end vertex
    next.reserve(size_t(w + h) * 4);
    for (int y = 0; y <= h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Horizontal edge between (x, y) and (x + 1, y): pixel above vs below.
            bool above = inside(x, y - 1), below = inside(x, y);
            if (above == below) continue;
            if (below) next.emplace(key(x + 1, y), key(x, y)); // inside below → go left
            else next.emplace(key(x, y), key(x + 1, y));
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x <= w; ++x) {
            bool left = inside(x - 1, y), right = inside(x, y);
            if (left == right) continue;
            if (right) next.emplace(key(x, y), key(x, y + 1)); // inside right → go down
            else next.emplace(key(x, y + 1), key(x, y));
        }
    }

    while (!next.empty()) {
        auto it = next.begin();
        int start = it->first;
        int cur = start;
        QPolygonF poly;
        int prevDx = 0, prevDy = 0;
        bool first = true;
        for (;;) {
            auto e = next.find(cur);
            if (e == next.end()) break;
            int to = e->second;
            next.erase(e);
            int cx = cur % vw, cy = cur / vw, tx = to % vw, ty = to / vw;
            int dx = tx - cx, dy = ty - cy;
            if (first || dx != prevDx || dy != prevDy) poly << QPointF(cx + x0, cy + y0); // corner
            first = false;
            prevDx = dx;
            prevDy = dy;
            cur = to;
            if (cur == start) break;
        }
        if (poly.size() >= 3) {
            poly << poly.first();
            path.addPolygon(poly);
            path.closeSubpath();
        }
    }
    return path;
}

} // namespace pane
