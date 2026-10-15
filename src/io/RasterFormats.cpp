// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// PNG (libpng), JPEG (libjpeg-turbo) and BMP file types.
#include "io/Formats.h"
#include "io/PngWriter.h"

#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <jpeglib.h>
#include <png.h>

namespace pane {

namespace {

bool isOpaque(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        auto *r = reinterpret_cast<const Pixel *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (pxA(r[x]) != 255) return false;
    }
    return true;
}

// ---------------------------------------------------------------- PNG

struct PngReadState {
    const QByteArray *data;
    size_t pos;
};

void pngRead(png_structp png, png_bytep out, png_size_t len)
{
    auto *st = static_cast<PngReadState *>(png_get_io_ptr(png));
    if (st->pos + len > size_t(st->data->size())) png_error(png, "unexpected end of file");
    std::memcpy(out, st->data->constData() + st->pos, len);
    st->pos += len;
}

void pngWrite(png_structp png, png_bytep in, png_size_t len)
{
    static_cast<QByteArray *>(png_get_io_ptr(png))->append(reinterpret_cast<const char *>(in), qsizetype(len));
}

void pngFlush(png_structp) {}

char g_pngError[256];
void pngErrorFn(png_structp png, png_const_charp msg)
{
    std::snprintf(g_pngError, sizeof(g_pngError), "%s", msg);
    png_longjmp(png, 1);
}
void pngWarnFn(png_structp, png_const_charp) {}

class PngFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("png"); }
    QString name() const override { return QStringLiteral("PNG"); }
    QStringList extensions() const override { return {QStringLiteral("png")}; }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, pngErrorFn, pngWarnFn);
        png_infop info = png ? png_create_info_struct(png) : nullptr;
        if (!info) throw FileError("Out of memory reading PNG.");
        PngReadState st{&data, 0};
        QImage img;
        std::vector<png_bytep> rows;
        png_uint_32 w = 0, h = 0, resX = 0, resY = 0;
        int unit = 0;
        if (setjmp(png_jmpbuf(png))) {
            png_destroy_read_struct(&png, &info, nullptr);
            throw FileError(std::string("This PNG file could not be read: ") + g_pngError);
        }
        png_set_read_fn(png, &st, pngRead);
        png_set_user_limits(png, 65535, 65535);
        png_read_info(png, info);
        w = png_get_image_width(png, info);
        h = png_get_image_height(png, info);
        int color = png_get_color_type(png, info);
        int depth = png_get_bit_depth(png, info);
        if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
        if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
        if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
        if (depth == 16) png_set_strip_16(png);
        if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
        png_set_bgr(png);
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
        png_set_interlace_handling(png);
        png_read_update_info(png, info);
        img = QImage(int(w), int(h), QImage::Format_ARGB32);
        if (img.isNull()) png_error(png, "image too large");
        rows.resize(h);
        for (png_uint_32 y = 0; y < h; ++y) rows[y] = img.scanLine(int(y));
        png_read_image(png, rows.data());
        png_read_end(png, nullptr);
        if (png_get_pHYs(png, info, &resX, &resY, &unit) && unit == PNG_RESOLUTION_METER && resX && resY) {
            // stored as pixels per meter
        } else {
            resX = resY = 3780; // 96 dpi
        }
        png_destroy_read_struct(&png, &info, nullptr);
        return documentFromImage(img, resX * 0.0254, resY * 0.0254);
    }

    QByteArray save(const Document &doc, const QImage &flat, const SaveOptions &) const override
    {
        return encodePngParallel(flat, !isOpaque(flat), doc.dpiX(), doc.dpiY());
    }

    // Single-threaded libpng encoder (kept as a reference for tests).
    static QByteArray saveLibpng(const Document &doc, const QImage &flat)
    {
        QByteArray out;
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, pngErrorFn, pngWarnFn);
        png_infop info = png ? png_create_info_struct(png) : nullptr;
        if (!info) throw FileError("Out of memory writing PNG.");
        std::vector<png_const_bytep> rows(size_t(flat.height()));
        volatile const bool opaque = isOpaque(flat); // volatile: survives longjmp
        if (setjmp(png_jmpbuf(png))) {
            png_destroy_write_struct(&png, &info);
            throw FileError(std::string("Could not encode PNG: ") + g_pngError);
        }
        png_set_write_fn(png, &out, pngWrite, pngFlush);
        png_set_IHDR(png, info, png_uint_32(flat.width()), png_uint_32(flat.height()), 8,
                     opaque ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_set_pHYs(png, info, png_uint_32(doc.dpiX() / 0.0254 + 0.5), png_uint_32(doc.dpiY() / 0.0254 + 0.5),
                     PNG_RESOLUTION_METER);
        png_write_info(png, info);
        png_set_bgr(png);
        if (opaque) png_set_filler(png, 0, PNG_FILLER_AFTER);
        for (int y = 0; y < flat.height(); ++y) rows[size_t(y)] = flat.constScanLine(y);
        png_write_image(png, const_cast<png_bytepp>(rows.data()));
        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        return out;
    }
};

// ---------------------------------------------------------------- JPEG

struct JpegErr {
    jpeg_error_mgr mgr;
    jmp_buf jmp;
    char msg[JMSG_LENGTH_MAX];
};

void jpegErrorExit(j_common_ptr cinfo)
{
    auto *e = reinterpret_cast<JpegErr *>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, e->msg);
    longjmp(e->jmp, 1);
}

class JpegFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("jpeg"); }
    QString name() const override { return QStringLiteral("JPEG"); }
    QStringList extensions() const override { return {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"), QStringLiteral("jfif")}; }
    bool isLossy() const override { return true; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::integer("quality", "Quality", 1, 100, 95),
                PropertySpec::choice("subsampling", "Chroma subsampling", {"4:4:4 (best quality)", "4:2:2", "4:2:0 (smallest file)"}, 0)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        jpeg_decompress_struct cinfo;
        JpegErr err;
        cinfo.err = jpeg_std_error(&err.mgr);
        err.mgr.error_exit = jpegErrorExit;
        QImage img;
        bool cmyk = false;
        double dpiX = 96, dpiY = 96;
        if (setjmp(err.jmp)) {
            jpeg_destroy_decompress(&cinfo);
            throw FileError(std::string("This JPEG file could not be read: ") + err.msg);
        }
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char *>(data.constData()), (unsigned long)data.size());
        jpeg_read_header(&cinfo, TRUE);
        cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
        if (!cmyk) {
            cinfo.out_color_space = JCS_EXT_BGRA;
            jpeg_start_decompress(&cinfo);
            img = QImage(int(cinfo.output_width), int(cinfo.output_height), QImage::Format_ARGB32);
            if (img.isNull()) {
                std::snprintf(err.msg, sizeof(err.msg), "image too large");
                longjmp(err.jmp, 1);
            }
            while (cinfo.output_scanline < cinfo.output_height) {
                JSAMPROW row = img.scanLine(int(cinfo.output_scanline));
                jpeg_read_scanlines(&cinfo, &row, 1);
            }
            jpeg_finish_decompress(&cinfo);
            if (cinfo.density_unit == 1 && cinfo.X_density) { dpiX = cinfo.X_density; dpiY = cinfo.Y_density; }
            else if (cinfo.density_unit == 2 && cinfo.X_density) { dpiX = cinfo.X_density * 2.54; dpiY = cinfo.Y_density * 2.54; }
        }
        jpeg_destroy_decompress(&cinfo);
        if (cmyk) {
            // libjpeg can't convert CMYK to RGB; Qt's reader handles Adobe CMYK.
            QBuffer buf(const_cast<QByteArray *>(&data));
            QImageReader r(&buf, "jpeg");
            img = r.read();
            if (img.isNull()) throw FileError("This CMYK JPEG file could not be read.");
            img = img.convertToFormat(QImage::Format_ARGB32);
        }
        // JPEG has no alpha.
        for (int y = 0; y < img.height(); ++y) {
            auto *r = reinterpret_cast<Pixel *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) r[x] |= 0xFF000000u;
        }
        return documentFromImage(img, dpiX, dpiY);
    }

    QByteArray save(const Document &doc, const QImage &flatIn, const SaveOptions &opts) const override
    {
        // Composite onto white: JPEG has no transparency.
        QImage flat = flatIn.copy();
        for (int y = 0; y < flat.height(); ++y) {
            auto *r = reinterpret_cast<Pixel *>(flat.scanLine(y));
            for (int x = 0; x < flat.width(); ++x) r[x] = overPixel(0xFFFFFFFF, r[x], pxA(r[x]));
        }
        jpeg_compress_struct cinfo;
        JpegErr err;
        cinfo.err = jpeg_std_error(&err.mgr);
        err.mgr.error_exit = jpegErrorExit;
        unsigned char *mem = nullptr;
        unsigned long memSize = 0;
        if (setjmp(err.jmp)) {
            jpeg_destroy_compress(&cinfo);
            free(mem);
            throw FileError(std::string("Could not encode JPEG: ") + err.msg);
        }
        jpeg_create_compress(&cinfo);
        jpeg_mem_dest(&cinfo, &mem, &memSize);
        cinfo.image_width = JDIMENSION(flat.width());
        cinfo.image_height = JDIMENSION(flat.height());
        cinfo.input_components = 4;
        cinfo.in_color_space = JCS_EXT_BGRA;
        jpeg_set_defaults(&cinfo);
        const int quality = std::clamp(opts.value(QStringLiteral("quality"), 95).toInt(), 1, 100);
        const int subsampling = opts.value(QStringLiteral("subsampling"), 0).toInt();
        jpeg_set_quality(&cinfo, quality, TRUE);
        cinfo.density_unit = 1;
        cinfo.X_density = UINT16(std::lround(doc.dpiX()));
        cinfo.Y_density = UINT16(std::lround(doc.dpiY()));
        cinfo.comp_info[0].h_samp_factor = subsampling == 0 ? 1 : 2;
        cinfo.comp_info[0].v_samp_factor = subsampling == 2 ? 2 : 1;
        jpeg_start_compress(&cinfo, TRUE);
        while (cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row = const_cast<JSAMPROW>(flat.constScanLine(int(cinfo.next_scanline)));
            jpeg_write_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        QByteArray out(reinterpret_cast<const char *>(mem), qsizetype(memSize));
        free(mem);
        return out;
    }
};

// ---------------------------------------------------------------- BMP

class BmpFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("bmp"); }
    QString name() const override { return QStringLiteral("BMP"); }
    QStringList extensions() const override { return {QStringLiteral("bmp"), QStringLiteral("dib")}; }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        QImage img;
        if (!img.loadFromData(data, "BMP")) throw FileError("This BMP file could not be read.");
        double dpiX = img.dotsPerMeterX() ? img.dotsPerMeterX() * 0.0254 : 96;
        double dpiY = img.dotsPerMeterY() ? img.dotsPerMeterY() * 0.0254 : 96;
        return documentFromImage(img.convertToFormat(QImage::Format_ARGB32), dpiX, dpiY);
    }

    QByteArray save(const Document &doc, const QImage &flat, const SaveOptions &) const override
    {
        const bool opaque = isOpaque(flat);
        const int w = flat.width(), h = flat.height();
        const int bpp = opaque ? 24 : 32;
        const int stride = ((w * bpp / 8) + 3) & ~3;
        const int headerSize = opaque ? 40 : 108; // BITMAPINFOHEADER or BITMAPV4HEADER
        const int offset = 14 + headerSize;
        QByteArray out(offset + qsizetype(stride) * h, '\0');
        auto *d = reinterpret_cast<uchar *>(out.data());
        auto put16 = [&](int at, quint16 v) { qToLittleEndian(v, d + at); };
        auto put32 = [&](int at, quint32 v) { qToLittleEndian(v, d + at); };
        d[0] = 'B'; d[1] = 'M';
        put32(2, quint32(out.size()));
        put32(10, quint32(offset));
        put32(14, quint32(headerSize));
        put32(18, quint32(w));
        put32(22, quint32(h)); // bottom-up
        put16(26, 1);
        put16(28, quint16(bpp));
        put32(30, opaque ? 0 : 3); // BI_RGB or BI_BITFIELDS
        put32(34, quint32(stride * h));
        put32(38, quint32(std::lround(doc.dpiX() / 0.0254)));
        put32(42, quint32(std::lround(doc.dpiY() / 0.0254)));
        if (!opaque) {
            put32(54, 0x00FF0000); put32(58, 0x0000FF00); put32(62, 0x000000FF); put32(66, 0xFF000000);
            put32(70, 0x73524742); // 'sRGB'
        }
        for (int y = 0; y < h; ++y) {
            auto *src = reinterpret_cast<const Pixel *>(flat.constScanLine(h - 1 - y));
            uchar *row = d + offset + qsizetype(y) * stride;
            if (opaque) {
                for (int x = 0; x < w; ++x) {
                    row[x * 3] = pxB(src[x]); row[x * 3 + 1] = pxG(src[x]); row[x * 3 + 2] = pxR(src[x]);
                }
            } else {
                std::memcpy(row, src, size_t(w) * 4);
            }
        }
        return out;
    }
};

} // namespace

const FileType *pngFileType() { static PngFileType t; return &t; }
const FileType *jpegFileType() { static JpegFileType t; return &t; }
const FileType *bmpFileType() { static BmpFileType t; return &t; }

} // namespace pane
