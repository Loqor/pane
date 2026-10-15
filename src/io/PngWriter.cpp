// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "io/PngWriter.h"

#include "core/Color.h"
#include "core/Parallel.h"

#include <QtEndian>
#include <cstdlib>
#include <stdexcept>
#include <zlib.h>

namespace pane {

namespace {

void putChunk(QByteArray &out, const char type[4], const QByteArray &data)
{
    char len[4];
    qToBigEndian(quint32(data.size()), len);
    out.append(len, 4);
    QByteArray body(type, 4);
    body.append(data);
    out.append(body);
    char crc[4];
    qToBigEndian(quint32(crc32(0, reinterpret_cast<const Bytef *>(body.constData()), uInt(body.size()))), crc);
    out.append(crc, 4);
}

inline uint8_t paeth(int a, int b, int c)
{
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// Filters one scanline (bpp bytes per pixel); picks the filter with the
// minimum sum of absolute values, like libpng's heuristic.
void filterRow(const uint8_t *cur, const uint8_t *prev, int len, int bpp, uint8_t *out)
{
    static thread_local std::vector<uint8_t> cand[5];
    long best = -1;
    int bestF = 0;
    for (int f = 0; f < 5; ++f) {
        cand[f].resize(size_t(len));
        uint8_t *c = cand[f].data();
        long sum = 0;
        for (int i = 0; i < len; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, cc = (prev && i >= bpp) ? prev[i - bpp] : 0;
            uint8_t v;
            switch (f) {
            case 0: v = cur[i]; break;
            case 1: v = uint8_t(cur[i] - a); break;
            case 2: v = uint8_t(cur[i] - b); break;
            case 3: v = uint8_t(cur[i] - ((a + b) >> 1)); break;
            default: v = uint8_t(cur[i] - paeth(a, b, cc)); break;
            }
            c[i] = v;
            sum += v < 128 ? v : 256 - v;
        }
        if (best < 0 || sum < best) {
            best = sum;
            bestF = f;
        }
    }
    out[0] = uint8_t(bestF);
    std::memcpy(out + 1, cand[bestF].data(), size_t(len));
}

} // namespace

QByteArray encodePngParallel(const QImage &img, bool withAlpha, double dpiX, double dpiY, int level)
{
    const int w = img.width(), h = img.height();
    const int bpp = withAlpha ? 4 : 3;
    const size_t rowLen = size_t(w) * bpp;

    // 1. Filter every row in parallel (rows are unpacked to RGB(A) bytes on the fly).
    std::vector<uint8_t> filtered((rowLen + 1) * h);
    auto unpack = [&](int y, std::vector<uint8_t> &o) {
        o.resize(rowLen);
        auto *in = reinterpret_cast<const Pixel *>(img.constScanLine(y));
        uint8_t *d = o.data();
        for (int x = 0; x < w; ++x) {
            Pixel p = in[x];
            *d++ = pxR(p);
            *d++ = pxG(p);
            *d++ = pxB(p);
            if (withAlpha) *d++ = pxA(p);
        }
    };
    parallelFor(h, [&](int y) {
        thread_local std::vector<uint8_t> cur, prev;
        unpack(y, cur);
        if (y) unpack(y - 1, prev);
        filterRow(cur.data(), y ? prev.data() : nullptr, int(rowLen), bpp, &filtered[(rowLen + 1) * y]);
    });

    // 2. Deflate in parallel strips; each strip is primed with the previous
    //    32 KB of input as a dictionary and ends on a byte boundary (sync flush).
    const size_t total = filtered.size();
    const size_t strip = std::max<size_t>(1 << 20, total / 64);
    const int nStrips = int((total + strip - 1) / strip);
    std::vector<QByteArray> parts(static_cast<size_t>(nStrips));
    std::vector<uLong> adlers(static_cast<size_t>(nStrips));
    std::atomic<bool> failed{false};
    parallelFor(nStrips, [&](int i) {
        size_t begin = size_t(i) * strip, len = std::min(strip, total - begin);
        z_stream zs{};
        if (deflateInit2(&zs, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            failed = true; // never throw from a worker thread
            return;
        }
        if (begin > 0) {
            size_t dictLen = std::min<size_t>(32768, begin);
            deflateSetDictionary(&zs, &filtered[begin - dictLen], uInt(dictLen));
        }
        QByteArray out;
        out.resize(qsizetype(deflateBound(&zs, uLong(len)) + 16));
        zs.next_in = &filtered[begin];
        zs.avail_in = uInt(len);
        zs.next_out = reinterpret_cast<Bytef *>(out.data());
        zs.avail_out = uInt(out.size());
        deflate(&zs, i == nStrips - 1 ? Z_FINISH : Z_SYNC_FLUSH);
        out.resize(qsizetype(zs.total_out));
        deflateEnd(&zs);
        parts[size_t(i)] = std::move(out);
        adlers[size_t(i)] = adler32(adler32(0, nullptr, 0), &filtered[begin], uInt(len));
    });

    if (failed) throw std::bad_alloc();

    QByteArray idat;
    idat.reserve(qsizetype(total / 3));
    idat.append(char(0x78));
    idat.append(char(0x9C)); // zlib header: deflate, 32K window, default level
    uLong adler = adler32(0, nullptr, 0);
    for (int i = 0; i < nStrips; ++i) {
        idat.append(parts[size_t(i)]);
        size_t begin = size_t(i) * strip, len = std::min(strip, total - begin);
        adler = adler32_combine(adler, adlers[size_t(i)], z_off_t(len));
    }
    char ad[4];
    qToBigEndian(quint32(adler), ad);
    idat.append(ad, 4);

    // 3. Assemble the file.
    QByteArray out("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr(13, '\0');
    qToBigEndian(quint32(w), ihdr.data());
    qToBigEndian(quint32(h), ihdr.data() + 4);
    ihdr[8] = 8;                     // bit depth
    ihdr[9] = withAlpha ? 6 : 2;     // RGBA : RGB
    putChunk(out, "IHDR", ihdr);
    QByteArray phys(9, '\0');
    qToBigEndian(quint32(std::lround(dpiX / 0.0254)), phys.data());
    qToBigEndian(quint32(std::lround(dpiY / 0.0254)), phys.data() + 4);
    phys[8] = 1; // pixels per meter
    putChunk(out, "pHYs", phys);
    for (qsizetype off = 0; off < idat.size(); off += 1 << 24) putChunk(out, "IDAT", idat.mid(off, 1 << 24));
    putChunk(out, "IEND", {});
    return out;
}

} // namespace pane
