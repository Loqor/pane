#include "effects/EffectUtil.h"

#include <cstring>

namespace pane::fx {

Region::Region(const Surface &src, const QRect &rect) : m_rect(rect)
{
    m_px.resize(size_t(rect.width()) * rect.height());
    const QRect ib = src.bounds();
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        int sy = std::clamp(y, 0, ib.bottom());
        Pixel *out = &m_px[size_t(y - rect.top()) * rect.width()];
        int x = rect.left();
        // Left clamp region.
        for (; x < 0 && x <= rect.right(); ++x) out[x - rect.left()] = src.pixel(0, sy);
        // Interior, copied by tile rows.
        while (x <= rect.right() && x <= ib.right()) {
            int tx = x >> TileShift;
            const Tile *t = src.tile(src.tileIndex(tx, sy >> TileShift));
            int tileEnd = std::min({(tx + 1) * TileSize - 1, rect.right(), ib.right()});
            int n = tileEnd - x + 1;
            if (t) std::memcpy(&out[x - rect.left()], &t->px[(sy & (TileSize - 1)) * TileSize + (x & (TileSize - 1))], size_t(n) * 4);
            else std::memset(&out[x - rect.left()], 0, size_t(n) * 4);
            x += n;
        }
        for (; x <= rect.right(); ++x) out[x - rect.left()] = src.pixel(ib.right(), sy);
    }
}

FBuf fetchF(const Surface &src, const QRect &rect)
{
    Region r(src, rect);
    FBuf b;
    b.rect = rect;
    b.px.resize(size_t(rect.width()) * rect.height());
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x) b.at(x, y) = toPF(r.at(x, y));
    return b;
}

namespace {

// Values are snapped to a 1/64 grid so sliding-window sums stay exact in float
// precision: results then never depend on where a buffer starts (tile independence).
inline float q64(float v) { return std::nearbyint(v * 64.0f) / 64.0f; }
inline PF q64(const PF &p) { return {q64(p.r), q64(p.g), q64(p.b), q64(p.a)}; }

void boxH(FBuf &b, int r)
{
    if (r <= 0) return;
    const int w = b.rect.width(), h = b.rect.height();
    std::vector<PF> row(static_cast<size_t>(w));
    const float inv = 1.0f / float(2 * r + 1);
    for (int y = 0; y < h; ++y) {
        PF *line = &b.px[size_t(y) * w];
        for (int x = 0; x < w; ++x) row[size_t(x)] = q64(line[x]);
        auto get = [&](int i) { return row[size_t(std::clamp(i, 0, w - 1))]; };
        PF acc;
        for (int i = -r; i <= r; ++i) acc += get(i);
        for (int x = 0; x < w; ++x) {
            line[x] = q64(acc * inv);
            acc += get(x + r + 1);
            acc -= get(x - r);
        }
    }
}

void boxV(FBuf &b, int r)
{
    if (r <= 0) return;
    const int w = b.rect.width(), h = b.rect.height();
    std::vector<PF> col(static_cast<size_t>(h));
    const float inv = 1.0f / float(2 * r + 1);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col[size_t(y)] = q64(b.px[size_t(y) * w + x]);
        auto get = [&](int i) { return col[size_t(std::clamp(i, 0, h - 1))]; };
        PF acc;
        for (int i = -r; i <= r; ++i) acc += get(i);
        for (int y = 0; y < h; ++y) {
            b.px[size_t(y) * w + x] = q64(acc * inv);
            acc += get(y + r + 1);
            acc -= get(y - r);
        }
    }
}

// Box radii approximating a Gaussian with three passes.
std::array<int, 3> boxesForGauss(double sigma)
{
    const int n = 3;
    double wIdeal = std::sqrt(12 * sigma * sigma / n + 1);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0) --wl;
    int wu = wl + 2;
    double mIdeal = (12 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4 * wl - 4);
    int m = int(std::lround(mIdeal));
    std::array<int, 3> r{};
    for (int i = 0; i < n; ++i) r[size_t(i)] = std::max(0, ((i < m ? wl : wu) - 1) / 2);
    return r;
}

} // namespace

PF quantize64(const PF &p) { return q64(p); }

void boxBlur(FBuf &b, int rx, int ry)
{
    boxH(b, rx);
    boxV(b, ry);
}

void gaussianBlur(FBuf &b, double sigma)
{
    if (sigma < 0.3) return;
    for (int r : boxesForGauss(sigma)) {
        boxH(b, r);
        boxV(b, r);
    }
}

int gaussianPad(double sigma)
{
    int s = 0;
    for (int r : boxesForGauss(sigma)) s += r + 1;
    return s;
}

PF sampleBilinearPF(const Surface &src, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = float(x - x0), fy = float(y - y0);
    PF a = toPF(src.pixelClamped(x0, y0)), b = toPF(src.pixelClamped(x0 + 1, y0));
    PF c = toPF(src.pixelClamped(x0, y0 + 1)), d = toPF(src.pixelClamped(x0 + 1, y0 + 1));
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

Pixel sampleBilinear(const Surface &src, double x, double y) { return fromPF(sampleBilinearPF(src, x, y)); }

double perlin(double x, double y, uint32_t seed)
{
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    auto grad = [&](int ix, int iy, double dx, double dy) {
        uint32_t h = hash3(uint32_t(ix), uint32_t(iy), seed);
        double ang = (h & 0xFFFF) / 65536.0 * 2 * M_PI;
        return std::cos(ang) * dx + std::sin(ang) * dy;
    };
    auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    double n00 = grad(x0, y0, fx, fy), n10 = grad(x0 + 1, y0, fx - 1, fy);
    double n01 = grad(x0, y0 + 1, fx, fy - 1), n11 = grad(x0 + 1, y0 + 1, fx - 1, fy - 1);
    double u = fade(fx), v = fade(fy);
    double nx0 = n00 + u * (n10 - n00), nx1 = n01 + u * (n11 - n01);
    return std::clamp((nx0 + v * (nx1 - nx0)) * 1.41, -1.0, 1.0);
}

double fbm(double x, double y, uint32_t seed, int octaves, double persistence)
{
    double sum = 0, amp = 1, norm = 0, freq = 1;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * perlin(x * freq, y * freq, seed + uint32_t(i) * 101u);
        norm += amp;
        amp *= persistence;
        freq *= 2;
    }
    return norm > 0 ? sum / norm : 0;
}

std::shared_ptr<EffectInstance> mapInstance(int quality, std::function<bool(double, double, double &, double &)> map)
{
    quality = std::clamp(quality, 1, 5);
    return std::make_shared<FnInstance>([quality, map](const Surface &src, const QRect &rect, Pixel *dst, int stride) {
        const float inv = 1.0f / float(quality * quality);
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            Pixel *row = dst + size_t(y - rect.top()) * stride;
            for (int x = rect.left(); x <= rect.right(); ++x) {
                PF acc;
                for (int j = 0; j < quality; ++j)
                    for (int i = 0; i < quality; ++i) {
                        double px = x + (i + 0.5) / quality, py = y + (j + 0.5) / quality, sx, sy;
                        if (map(px, py, sx, sy)) acc += sampleBilinearPF(src, sx, sy);
                    }
                row[x - rect.left()] = fromPF(acc * inv);
            }
        }
    });
}

} // namespace pane::fx
