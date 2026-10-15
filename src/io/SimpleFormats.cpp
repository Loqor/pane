// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// TGA, DDS, GIF (giflib) and ICO file types.
#include "core/Glyphs.h"
#include "core/Parallel.h"
#include "core/Quantize.h"
#include "core/Resample.h"
#include "io/Formats.h"
#include "io/PngWriter.h"

#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include <bit>
#include <climits>
#include <cstring>
#include <gif_lib.h>

namespace pane {

namespace {

bool imageHasAlpha(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        auto *r = reinterpret_cast<const Pixel *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (pxA(r[x]) != 255) return true;
    }
    return false;
}

struct ByteReader {
    const QByteArray &d;
    qsizetype pos = 0;
    void need(qsizetype n) const
    {
        if (n < 0 || pos + n > d.size()) throw FileError("The file is truncated or corrupt.");
    }
    uint8_t u8() { need(1); return uint8_t(d[pos++]); }
    uint16_t u16() { need(2); uint16_t v = qFromLittleEndian<quint16>(d.constData() + pos); pos += 2; return v; }
    uint32_t u32() { need(4); uint32_t v = qFromLittleEndian<quint32>(d.constData() + pos); pos += 4; return v; }
};

void put16(QByteArray &o, uint16_t v) { char b[2]; qToLittleEndian(v, b); o.append(b, 2); }
void put32(QByteArray &o, uint32_t v) { char b[4]; qToLittleEndian(v, b); o.append(b, 4); }

// ------------------------------------------------------------ TGA

class TgaFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("tga"); }
    QString name() const override { return QStringLiteral("TGA"); }
    QStringList extensions() const override { return {QStringLiteral("tga")}; }
    QList<PropertySpec> saveOptions() const override { return {PropertySpec::boolean("rle", "RLE compression", true)}; }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        ByteReader r{data};
        uint8_t idLen = r.u8(), cmapType = r.u8(), type = r.u8();
        uint16_t cmapFirst = r.u16(), cmapLen = r.u16();
        uint8_t cmapBits = r.u8();
        r.u16();
        r.u16(); // origin
        int w = r.u16(), h = r.u16();
        uint8_t bpp = r.u8(), desc = r.u8();
        r.need(idLen);
        r.pos += idLen;
        const bool rle = type >= 9;
        const int base = type & 7; // 1 colormapped, 2 truecolor, 3 grayscale
        if (!w || !h || (base != 1 && base != 2 && base != 3)) throw FileError("Unsupported TGA image type.");
        std::vector<Pixel> cmap;
        if (cmapType == 1) {
            int cb = (cmapBits + 7) / 8;
            cmap.resize(size_t(cmapFirst + cmapLen));
            for (int i = 0; i < cmapLen; ++i) {
                r.need(cb);
                const uint8_t *p = reinterpret_cast<const uint8_t *>(data.constData() + r.pos);
                r.pos += cb;
                cmap[size_t(cmapFirst + i)] = cb == 4 ? makePixel(p[2], p[1], p[0], p[3]) : cb == 3 ? makePixel(p[2], p[1], p[0], 255)
                                                                                                  : makePixel(((p[1] >> 2) & 31) * 255 / 31, (((p[1] & 3) << 3) | (p[0] >> 5)) * 255 / 31, (p[0] & 31) * 255 / 31, 255);
            }
        }
        const int bytes = (bpp + 7) / 8;
        const bool alphaBits = (desc & 15) > 0;
        auto decode = [&](const uint8_t *p) -> Pixel {
            switch (base) {
            case 1: {
                uint32_t idx = bytes == 2 ? p[0] | (p[1] << 8) : p[0];
                return idx < cmap.size() ? cmap[idx] : 0xFF000000u;
            }
            case 3: return makePixel(p[0], p[0], p[0], bytes == 2 ? p[1] : 255);
            default:
                if (bytes == 4) return makePixel(p[2], p[1], p[0], alphaBits ? p[3] : 255);
                if (bytes == 3) return makePixel(p[2], p[1], p[0], 255);
                if (bytes == 2) {
                    uint16_t v = uint16_t(p[0] | (p[1] << 8));
                    return makePixel(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31,
                                     alphaBits && !(v & 0x8000) ? 0 : 255);
                }
                return 0;
            }
        };
        std::vector<Pixel> px(size_t(w) * h);
        size_t n = 0;
        while (n < px.size()) {
            if (!rle) {
                r.need(bytes);
                px[n++] = decode(reinterpret_cast<const uint8_t *>(data.constData() + r.pos));
                r.pos += bytes;
                continue;
            }
            uint8_t hdr = r.u8();
            int count = (hdr & 127) + 1;
            if (hdr & 128) {
                r.need(bytes);
                Pixel v = decode(reinterpret_cast<const uint8_t *>(data.constData() + r.pos));
                r.pos += bytes;
                for (int i = 0; i < count && n < px.size(); ++i) px[n++] = v;
            } else {
                for (int i = 0; i < count && n < px.size(); ++i) {
                    r.need(bytes);
                    px[n++] = decode(reinterpret_cast<const uint8_t *>(data.constData() + r.pos));
                    r.pos += bytes;
                }
            }
        }
        QImage img(w, h, QImage::Format_ARGB32);
        const bool topDown = desc & 0x20, rightToLeft = desc & 0x10;
        for (int y = 0; y < h; ++y) {
            auto *row = reinterpret_cast<Pixel *>(img.scanLine(topDown ? y : h - 1 - y));
            for (int x = 0; x < w; ++x) row[rightToLeft ? w - 1 - x : x] = px[size_t(y) * w + x];
        }
        return documentFromImage(img);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        const bool alpha = imageHasAlpha(flat), rle = o.value("rle", true).toBool();
        const int bytes = alpha ? 4 : 3, w = flat.width(), h = flat.height();
        QByteArray out;
        out.append(char(0)); // id length
        out.append(char(0)); // no color map
        out.append(char(rle ? 10 : 2));
        out.append(5, '\0'); // color map spec
        put16(out, 0);
        put16(out, 0);
        put16(out, uint16_t(w));
        put16(out, uint16_t(h));
        out.append(char(bytes * 8));
        out.append(char(0x20 | (alpha ? 8 : 0))); // top-left origin, alpha bits
        auto pix = [&](Pixel p) {
            char b[4] = {char(pxB(p)), char(pxG(p)), char(pxR(p)), char(pxA(p))};
            out.append(b, bytes);
        };
        for (int y = 0; y < h; ++y) {
            auto *row = reinterpret_cast<const Pixel *>(flat.constScanLine(y));
            if (!rle) {
                for (int x = 0; x < w; ++x) pix(row[x]);
                continue;
            }
            int x = 0;
            while (x < w) {
                int run = 1;
                while (x + run < w && run < 128 && row[x + run] == row[x]) ++run;
                if (run > 1) {
                    out.append(char(0x80 | (run - 1)));
                    pix(row[x]);
                    x += run;
                    continue;
                }
                int lit = 1;
                while (x + lit < w && lit < 128 && !(x + lit + 1 < w && row[x + lit] == row[x + lit + 1])) ++lit;
                out.append(char(lit - 1));
                for (int i = 0; i < lit; ++i) pix(row[x + i]);
                x += lit;
            }
        }
        out.append("\0\0\0\0\0\0\0\0TRUEVISION-XFILE.\0", 26);
        return out;
    }
};

// ------------------------------------------------------------ DDS

Pixel from565(uint16_t c)
{
    return makePixel(((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31, 255);
}
uint16_t to565(int r, int g, int b)
{
    return uint16_t(((std::clamp(r, 0, 255) * 31 + 127) / 255) << 11 | ((std::clamp(g, 0, 255) * 63 + 127) / 255) << 5 |
                    ((std::clamp(b, 0, 255) * 31 + 127) / 255));
}

// Decodes a BC1 color block into 16 pixels.
void decodeBc1(const uint8_t *b, Pixel *out, bool forceFour)
{
    uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
    Pixel p[4] = {from565(c0), from565(c1), 0, 0};
    auto mix = [](Pixel a, Pixel c, int wa, int wc, int d) {
        return makePixel((pxR(a) * wa + pxR(c) * wc) / d, (pxG(a) * wa + pxG(c) * wc) / d, (pxB(a) * wa + pxB(c) * wc) / d, 255);
    };
    if (c0 > c1 || forceFour) {
        p[2] = mix(p[0], p[1], 2, 1, 3);
        p[3] = mix(p[0], p[1], 1, 2, 3);
    } else {
        p[2] = mix(p[0], p[1], 1, 1, 2);
        p[3] = 0;
    }
    uint32_t idx = uint32_t(b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24));
    for (int i = 0; i < 16; ++i) out[i] = p[(idx >> (2 * i)) & 3];
}

void decodeBc3Alpha(const uint8_t *b, uint8_t *alpha)
{
    int a0 = b[0], a1 = b[1];
    int pal[8] = {a0, a1};
    if (a0 > a1)
        for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else {
        for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

// Encodes 16 pixels as a BC1 block (4-color mode, or 3-color + transparent).
void encodeBc1(const Pixel *px, uint8_t *out, bool allowTransparent)
{
    bool anyTransparent = false;
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        if (allowTransparent && pxA(px[i]) < 128) {
            anyTransparent = true;
            continue;
        }
        int c[3] = {pxR(px[i]), pxG(px[i]), pxB(px[i])};
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], c[k]);
            hi[k] = std::max(hi[k], c[k]);
        }
    }
    if (hi[0] < lo[0]) lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;
    // Inset the bounding box slightly (reduces error, like common fast encoders).
    for (int k = 0; k < 3; ++k) {
        int inset = (hi[k] - lo[k]) / 16;
        lo[k] += inset;
        hi[k] -= inset;
    }
    uint16_t c0 = to565(hi[0], hi[1], hi[2]), c1 = to565(lo[0], lo[1], lo[2]);
    if (anyTransparent) {
        if (c0 > c1) std::swap(c0, c1); // c0 <= c1 selects 3-color mode
    } else {
        if (c0 < c1) std::swap(c0, c1);
        if (c0 == c1 && c0 > 0) --c1; // keep 4-color mode
    }
    out[0] = uint8_t(c0);
    out[1] = uint8_t(c0 >> 8);
    out[2] = uint8_t(c1);
    out[3] = uint8_t(c1 >> 8);
    Pixel p4[4];
    {
        uint8_t tmp[8] = {out[0], out[1], out[2], out[3], 0xE4, 0, 0, 0}; // indices 0,1,2,3
        Pixel d[16];
        decodeBc1(tmp, d, false);
        for (int i = 0; i < 4; ++i) p4[i] = d[i];
    }
    uint32_t idx = 0;
    const int colors = anyTransparent ? 3 : 4;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        if (anyTransparent && pxA(px[i]) < 128) best = 3;
        else {
            long bestD = LONG_MAX;
            for (int k = 0; k < colors; ++k) {
                long dr = long(pxR(px[i])) - pxR(p4[k]), dg = long(pxG(px[i])) - pxG(p4[k]), db = long(pxB(px[i])) - pxB(p4[k]);
                long d = dr * dr + dg * dg + db * db;
                if (d < bestD) {
                    bestD = d;
                    best = k;
                }
            }
        }
        idx |= uint32_t(best) << (2 * i);
    }
    out[4] = uint8_t(idx);
    out[5] = uint8_t(idx >> 8);
    out[6] = uint8_t(idx >> 16);
    out[7] = uint8_t(idx >> 24);
}

void encodeBc3Alpha(const Pixel *px, uint8_t *out)
{
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) {
        lo = std::min(lo, int(pxA(px[i])));
        hi = std::max(hi, int(pxA(px[i])));
    }
    out[0] = uint8_t(hi);
    out[1] = uint8_t(lo);
    int pal[8] = {hi, lo};
    for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * hi + i * lo) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestD = 1 << 30;
        for (int k = 0; k < 8; ++k) {
            int d = std::abs(int(pxA(px[i])) - pal[k]);
            if (d < bestD) {
                bestD = d;
                best = k;
            }
        }
        bits |= uint64_t(best) << (3 * i);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t(bits >> (8 * i));
}

class DdsFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("dds"); }
    QString name() const override { return QStringLiteral("DirectDraw Surface (DDS)"); }
    QStringList extensions() const override { return {QStringLiteral("dds")}; }
    bool isLossy() const override { return true; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::choice("format", "Format",
                                     {"BC1 / DXT1 (1-bit alpha)", "BC2 / DXT3 (explicit alpha)", "BC3 / DXT5 (interpolated alpha)",
                                      "B8G8R8A8 (uncompressed)"},
                                     2),
                PropertySpec::boolean("mipmaps", "Generate mip maps", false)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        ByteReader r{data};
        if (data.left(4) != "DDS ") throw FileError("This is not a DDS file.");
        r.pos = 4;
        r.u32(); // header size
        r.u32(); // flags
        int h = int(r.u32()), w = int(r.u32());
        r.pos = 4 + 72; // DDS_PIXELFORMAT is at offset 72 of the 124-byte header
        r.u32();
        uint32_t pfFlags = r.u32(), fourCC = r.u32(), bitCount = r.u32();
        uint32_t rm = r.u32(), gm = r.u32(), bm = r.u32(), am = r.u32();
        r.pos = 4 + 124;
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535) throw FileError("Invalid DDS dimensions.");
        auto cc = [](const char *s) { return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24; };
        int bc = 0; // 1, 2, 3 for compressed formats
        bool bgra32 = false, rgba32 = false;
        if (pfFlags & 4) {
            if (fourCC == cc("DXT1")) bc = 1;
            else if (fourCC == cc("DXT2") || fourCC == cc("DXT3")) bc = 2;
            else if (fourCC == cc("DXT4") || fourCC == cc("DXT5")) bc = 3;
            else if (fourCC == cc("DX10")) {
                uint32_t dxgi = r.u32();
                r.pos += 16;
                if (dxgi == 71 || dxgi == 72) bc = 1;
                else if (dxgi == 74 || dxgi == 75) bc = 2;
                else if (dxgi == 77 || dxgi == 78) bc = 3;
                else if (dxgi == 87 || dxgi == 91) bgra32 = true;
                else if (dxgi == 28 || dxgi == 29) rgba32 = true;
                else throw FileError("This DDS pixel format (e.g. BC7 or HDR) is not supported yet.");
            } else throw FileError("This DDS compression format is not supported yet.");
        }
        QImage img(w, h, QImage::Format_ARGB32);
        if (img.isNull()) throw FileError("This DDS image is too large.");
        if (bc) {
            const int bw = (w + 3) / 4, bh = (h + 3) / 4, blockBytes = bc == 1 ? 8 : 16;
            r.need(qsizetype(bw) * bh * blockBytes);
            const uint8_t *src = reinterpret_cast<const uint8_t *>(data.constData() + r.pos);
            parallelFor(bh, [&](int by) {
                for (int bx = 0; bx < bw; ++bx) {
                    const uint8_t *blk = src + (size_t(by) * bw + bx) * blockBytes;
                    Pixel px[16];
                    uint8_t alpha[16];
                    if (bc == 1) decodeBc1(blk, px, false);
                    else {
                        decodeBc1(blk + 8, px, true);
                        if (bc == 2)
                            for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((blk[i / 2] >> ((i & 1) * 4)) & 15) * 17);
                        else decodeBc3Alpha(blk, alpha);
                        for (int i = 0; i < 16; ++i) px[i] = withAlpha(px[i], alpha[i]);
                    }
                    for (int j = 0; j < 4; ++j)
                        for (int i = 0; i < 4; ++i) {
                            int x = bx * 4 + i, y = by * 4 + j;
                            if (x < w && y < h) reinterpret_cast<Pixel *>(img.scanLine(y))[x] = px[j * 4 + i];
                        }
                }
            });
        } else {
            // Uncompressed with channel masks.
            if (bgra32 || rgba32) {
                bitCount = 32;
                rm = rgba32 ? 0xFF : 0xFF0000;
                gm = 0xFF00;
                bm = rgba32 ? 0xFF0000 : 0xFF;
                am = 0xFF000000;
                pfFlags |= 1;
            }
            const int bpp = int(bitCount / 8);
            if (bpp < 1 || bpp > 4) throw FileError("Unsupported DDS pixel format.");
            const int pitch = w * bpp;
            r.need(qsizetype(pitch) * h);
            auto chan = [](uint32_t v, uint32_t mask) -> uint32_t {
                if (!mask) return 255;
                int shift = std::countr_zero(mask);
                uint32_t max = mask >> shift;
                return ((v & mask) >> shift) * 255 / max;
            };
            const bool hasA = (pfFlags & 1) && am;
            for (int y = 0; y < h; ++y) {
                const uint8_t *row = reinterpret_cast<const uint8_t *>(data.constData() + r.pos + qsizetype(y) * pitch);
                auto *out = reinterpret_cast<Pixel *>(img.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    uint32_t v = 0;
                    std::memcpy(&v, row + x * bpp, size_t(bpp));
                    if (pfFlags & 0x20000) { // luminance
                        uint32_t l = chan(v, rm);
                        out[x] = makePixel(l, l, l, hasA ? chan(v, am) : 255);
                    } else {
                        out[x] = makePixel(chan(v, rm), chan(v, gm), chan(v, bm), hasA ? chan(v, am) : 255);
                    }
                }
            }
        }
        return documentFromImage(img);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        const int format = o.value("format", 2).toInt();
        const bool mips = o.value("mipmaps", false).toBool();
        std::vector<QImage> levels{flat};
        if (mips)
            while (levels.back().width() > 1 || levels.back().height() > 1)
                levels.push_back(resampleImage(levels.back(), QSize(std::max(1, levels.back().width() / 2), std::max(1, levels.back().height() / 2)),
                                               Resampling::BestQuality));
        const int w = flat.width(), h = flat.height();
        QByteArray out("DDS ", 4);
        put32(out, 124);
        uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000 | (mips ? 0x20000 : 0) | (format == 3 ? 0x8 : 0x80000);
        put32(out, flags);
        put32(out, uint32_t(h));
        put32(out, uint32_t(w));
        put32(out, format == 3 ? uint32_t(w * 4) : uint32_t(((w + 3) / 4) * ((h + 3) / 4) * (format == 0 ? 8 : 16)));
        put32(out, 0);
        put32(out, uint32_t(levels.size()));
        out.append(44, '\0'); // reserved
        put32(out, 32);       // pixel format size
        if (format == 3) {
            put32(out, 0x41); // RGB | ALPHAPIXELS
            put32(out, 0);
            put32(out, 32);
            put32(out, 0xFF0000);
            put32(out, 0xFF00);
            put32(out, 0xFF);
            put32(out, 0xFF000000);
        } else {
            put32(out, 0x4);
            out.append(format == 0 ? "DXT1" : format == 1 ? "DXT3" : "DXT5", 4);
            out.append(20, '\0');
        }
        put32(out, 0x1000 | (mips ? 0x400008 : 0)); // caps
        out.append(16, '\0');
        for (const QImage &lv : levels) {
            const int lw = lv.width(), lh = lv.height();
            if (format == 3) {
                for (int y = 0; y < lh; ++y) out.append(reinterpret_cast<const char *>(lv.constScanLine(y)), lw * 4);
                continue;
            }
            const int bw = (lw + 3) / 4, bh = (lh + 3) / 4, blockBytes = format == 0 ? 8 : 16;
            QByteArray blocks(qsizetype(bw) * bh * blockBytes, '\0');
            parallelFor(bh, [&](int by) {
                for (int bx = 0; bx < bw; ++bx) {
                    Pixel px[16];
                    for (int j = 0; j < 4; ++j)
                        for (int i = 0; i < 4; ++i)
                            px[j * 4 + i] = reinterpret_cast<const Pixel *>(lv.constScanLine(std::min(by * 4 + j, lh - 1)))[std::min(bx * 4 + i, lw - 1)];
                    auto *dst = reinterpret_cast<uint8_t *>(blocks.data()) + (size_t(by) * bw + bx) * blockBytes;
                    if (format == 0) encodeBc1(px, dst, true);
                    else {
                        if (format == 1)
                            for (int i = 0; i < 8; ++i) dst[i] = uint8_t((pxA(px[2 * i]) * 15 + 127) / 255 | ((pxA(px[2 * i + 1]) * 15 + 127) / 255) << 4);
                        else encodeBc3Alpha(px, dst);
                        encodeBc1(px, dst + 8, false);
                    }
                }
            });
            out.append(blocks);
        }
        return out;
    }
};

// ------------------------------------------------------------ GIF

int gifRead(GifFileType *gif, GifByteType *buf, int len)
{
    auto *r = static_cast<ByteReader *>(gif->UserData);
    int n = int(std::min<qsizetype>(len, r->d.size() - r->pos));
    std::memcpy(buf, r->d.constData() + r->pos, size_t(n));
    r->pos += n;
    return n;
}

int gifWrite(GifFileType *gif, const GifByteType *buf, int len)
{
    static_cast<QByteArray *>(gif->UserData)->append(reinterpret_cast<const char *>(buf), len);
    return len;
}

class GifFileTypeImpl final : public FileType {
public:
    QString id() const override { return QStringLiteral("gif"); }
    QString name() const override { return QStringLiteral("GIF"); }
    QStringList extensions() const override { return {QStringLiteral("gif")}; }
    bool isLossy() const override { return true; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::integer("dithering", "Dithering level", 0, 100, 50),
                PropertySpec::integer("threshold", "Transparency threshold", 0, 255, 128)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        ByteReader r{data};
        int err = 0;
        GifFileType *gif = DGifOpen(&r, gifRead, &err);
        if (!gif) throw FileError(std::string("This GIF file could not be read: ") + GifErrorString(err));
        if (DGifSlurp(gif) != GIF_OK || gif->ImageCount < 1) {
            DGifCloseFile(gif, &err);
            throw FileError("This GIF file could not be decoded.");
        }
        const int w = gif->SWidth, h = gif->SHeight;
        SavedImage &frame = gif->SavedImages[0];
        ColorMapObject *cmap = frame.ImageDesc.ColorMap ? frame.ImageDesc.ColorMap : gif->SColorMap;
        GraphicsControlBlock gcb{};
        gcb.TransparentColor = NO_TRANSPARENT_COLOR;
        DGifSavedExtensionToGCB(gif, 0, &gcb);
        QImage img(std::max(1, w), std::max(1, h), QImage::Format_ARGB32);
        img.fill(0);
        const GifImageDesc &d = frame.ImageDesc;
        for (int y = 0; y < d.Height; ++y)
            for (int x = 0; x < d.Width; ++x) {
                int cx = d.Left + x, cy = d.Top + y;
                if (cx >= img.width() || cy >= img.height()) continue;
                int idx = frame.RasterBits[size_t(y) * d.Width + x];
                if (idx == gcb.TransparentColor || !cmap || idx >= cmap->ColorCount) continue;
                const GifColorType &c = cmap->Colors[idx];
                reinterpret_cast<Pixel *>(img.scanLine(cy))[cx] = makePixel(c.Red, c.Green, c.Blue, 255);
            }
        DGifCloseFile(gif, &err);
        return documentFromImage(img);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        const int w = flat.width(), h = flat.height();
        const int threshold = o.value("threshold", 128).toInt();
        const double dither = o.value("dithering", 50).toInt() / 100.0;
        bool anyTransparent = false;
        std::vector<Pixel> sample;
        int step = std::max(1, int(std::sqrt(double(w) * h / 200000.0)));
        for (int y = 0; y < h; y += step)
            for (int x = 0; x < w; x += step) {
                Pixel p = reinterpret_cast<const Pixel *>(flat.constScanLine(y))[x];
                if (pxA(p) < threshold) anyTransparent = true;
                else sample.push_back(p | 0xFF000000u);
            }
        for (int y = 0; y < h && !anyTransparent; ++y)
            for (int x = 0; x < w; ++x)
                if (pxA(reinterpret_cast<const Pixel *>(flat.constScanLine(y))[x]) < threshold) {
                    anyTransparent = true;
                    break;
                }
        std::vector<Pixel> pal = medianCutPalette(std::move(sample), anyTransparent ? 255 : 256);
        if (pal.empty()) pal.push_back(0xFF000000u);
        PaletteMap pm(pal);
        const int transparentIndex = anyTransparent ? int(pal.size()) : -1;
        int colors = int(pal.size()) + (anyTransparent ? 1 : 0);
        int bits = 1;
        while ((1 << bits) < colors) ++bits;
        ColorMapObject *cmap = GifMakeMapObject(1 << bits, nullptr);
        for (int i = 0; i < (1 << bits); ++i) {
            Pixel c = i < int(pal.size()) ? pal[size_t(i)] : 0;
            cmap->Colors[i] = {GifByteType(pxR(c)), GifByteType(pxG(c)), GifByteType(pxB(c))};
        }
        const double spread = 255.0 / std::cbrt(double(std::max<size_t>(2, pal.size())));
        std::vector<GifByteType> indices(size_t(w) * h);
        parallelFor(h, [&](int y) {
            auto *row = reinterpret_cast<const Pixel *>(flat.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                Pixel p = row[x];
                if (anyTransparent && pxA(p) < threshold) {
                    indices[size_t(y) * w + x] = GifByteType(transparentIndex);
                    continue;
                }
                double d = bayer8(x, y) * dither * spread;
                indices[size_t(y) * w + x] = GifByteType(pm.index(int(pxR(p) + d), int(pxG(p) + d), int(pxB(p) + d)));
            }
        });
        QByteArray out;
        int err = 0;
        GifFileType *gif = EGifOpen(&out, gifWrite, &err);
        if (!gif) {
            GifFreeMapObject(cmap);
            throw FileError("Could not create GIF.");
        }
        EGifSetGifVersion(gif, true);
        bool ok = EGifPutScreenDesc(gif, w, h, bits, 0, cmap) == GIF_OK;
        if (ok && anyTransparent) {
            GraphicsControlBlock gcb{};
            gcb.DisposalMode = DISPOSAL_UNSPECIFIED;
            gcb.TransparentColor = transparentIndex;
            GifByteType ext[4];
            EGifGCBToExtension(&gcb, ext);
            ok = EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, ext) == GIF_OK;
        }
        if (ok) ok = EGifPutImageDesc(gif, 0, 0, w, h, false, nullptr) == GIF_OK;
        for (int y = 0; ok && y < h; ++y) ok = EGifPutLine(gif, &indices[size_t(y) * w], w) == GIF_OK;
        EGifCloseFile(gif, &err);
        GifFreeMapObject(cmap);
        if (!ok) throw FileError("GIF encoding failed.");
        return out;
    }
};

// ------------------------------------------------------------ ICO

class IcoFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("ico"); }
    QString name() const override { return QStringLiteral("Icon (ICO)"); }
    QStringList extensions() const override { return {QStringLiteral("ico")}; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::choice("sizes", "Sizes", {"All standard sizes (16 – 256 px)", "Small sizes (16 – 48 px)", "Image size only"}, 0)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        ByteReader r{data};
        if (r.u16() != 0 || r.u16() != 1) throw FileError("This is not an icon file.");
        int count = r.u16();
        // Pick the largest (then deepest) entry.
        int best = -1, bestArea = -1, bestBpp = -1;
        uint32_t bestSize = 0, bestOffset = 0;
        for (int i = 0; i < count; ++i) {
            int w = r.u8(), h = r.u8();
            r.u8();
            r.u8();
            r.u16();
            int bpp = r.u16();
            uint32_t size = r.u32(), off = r.u32();
            int area = (w ? w : 256) * (h ? h : 256);
            if (area > bestArea || (area == bestArea && bpp > bestBpp)) {
                best = i;
                bestArea = area;
                bestBpp = bpp;
                bestSize = size;
                bestOffset = off;
            }
        }
        if (best < 0 || qsizetype(bestOffset) + qsizetype(bestSize) > data.size()) throw FileError("This icon file is corrupt.");
        QByteArray entry = data.mid(bestOffset, bestSize);
        if (entry.startsWith("\x89PNG")) return pngFileType()->load(entry);
        // BMP DIB: let Qt's ICO reader decode the whole file and choose the same size.
        QBuffer buf(const_cast<QByteArray *>(&data));
        QImageReader reader(&buf, "ico");
        QImage bestImg;
        do {
            QImage im = reader.read();
            if (!im.isNull() && im.width() * im.height() >= bestImg.width() * bestImg.height()) bestImg = im;
        } while (reader.jumpToNextImage());
        if (bestImg.isNull()) throw FileError("This icon file could not be decoded.");
        return documentFromImage(bestImg.convertToFormat(QImage::Format_ARGB32));
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        std::vector<int> sizes;
        switch (o.value("sizes", 0).toInt()) {
        case 0: sizes = {256, 128, 64, 48, 32, 24, 16}; break;
        case 1: sizes = {48, 32, 24, 16}; break;
        default:
            if (flat.width() > 256 || flat.height() > 256) throw FileError("Icons can be at most 256 × 256 pixels.");
            sizes = {std::max(flat.width(), flat.height())};
            break;
        }
        std::vector<QByteArray> images;
        for (int s : sizes) {
            // Fit the image into an s x s square, centered.
            QSize fit = flat.size().scaled(s, s, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
            QImage scaled = fit == flat.size() ? flat : resampleImage(flat, fit, Resampling::BestQuality);
            QImage square(s, s, QImage::Format_ARGB32);
            square.fill(0);
            for (int y = 0; y < fit.height(); ++y)
                std::memcpy(square.scanLine((s - fit.height()) / 2 + y) + size_t((s - fit.width()) / 2) * 4, scaled.constScanLine(y),
                            size_t(fit.width()) * 4);
            images.push_back(encodePngParallel(square, true, 96, 96));
        }
        QByteArray out;
        put16(out, 0);
        put16(out, 1);
        put16(out, uint16_t(sizes.size()));
        uint32_t offset = uint32_t(6 + 16 * sizes.size());
        for (size_t i = 0; i < sizes.size(); ++i) {
            out.append(char(sizes[i] >= 256 ? 0 : sizes[i]));
            out.append(char(sizes[i] >= 256 ? 0 : sizes[i]));
            out.append(char(0));
            out.append(char(0));
            put16(out, 1);
            put16(out, 32);
            put32(out, uint32_t(images[i].size()));
            put32(out, offset);
            offset += uint32_t(images[i].size());
        }
        for (const QByteArray &im : images) out.append(im);
        return out;
    }
};

} // namespace

const FileType *tgaFileType() { static TgaFileType t; return &t; }
const FileType *ddsFileType() { static DdsFileType t; return &t; }
const FileType *gifFileType() { static GifFileTypeImpl t; return &t; }
const FileType *icoFileType() { static IcoFileType t; return &t; }

} // namespace pane
