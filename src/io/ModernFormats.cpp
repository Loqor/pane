// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// WebP (libwebp), AVIF (libavif), HEIC (libheif) and TIFF (libtiff).
#include "core/Glyphs.h"
#include "io/Formats.h"

#include <avif/avif.h>
#include <cstring>
#include <libheif/heif.h>
#include <tiffio.h>
#include <webp/demux.h>
#include <webp/encode.h>

// libheif 1.20 added size_t-stride accessors and deprecated the int ones; older
// distros (Ubuntu 24.04, Debian 12) only have the int versions.
namespace {
#if LIBHEIF_HAVE_VERSION(1, 20, 0)
const uint8_t *heifPlaneRO(const heif_image *img, size_t *stride)
{
    return heif_image_get_plane_readonly2(img, heif_channel_interleaved, stride);
}
uint8_t *heifPlane(heif_image *img, size_t *stride) { return heif_image_get_plane2(img, heif_channel_interleaved, stride); }
#else
const uint8_t *heifPlaneRO(const heif_image *img, size_t *stride)
{
    int s = 0;
    const uint8_t *p = heif_image_get_plane_readonly(img, heif_channel_interleaved, &s);
    *stride = static_cast<size_t>(s);
    return p;
}
uint8_t *heifPlane(heif_image *img, size_t *stride)
{
    int s = 0;
    uint8_t *p = heif_image_get_plane(img, heif_channel_interleaved, &s);
    *stride = static_cast<size_t>(s);
    return p;
}
#endif
} // namespace

namespace pane {

namespace {

bool hasAlpha(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        auto *r = reinterpret_cast<const Pixel *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (pxA(r[x]) != 255) return true;
    }
    return false;
}

// RGBA byte order (memory R,G,B,A) <-> our BGRA pixels.
inline Pixel fromRgbaBytes(const uint8_t *p) { return makePixel(p[0], p[1], p[2], p[3]); }

// ------------------------------------------------------------ WebP

class WebPFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("webp"); }
    QString name() const override { return QStringLiteral("WebP"); }
    QStringList extensions() const override { return {QStringLiteral("webp")}; }
    bool isLossy() const override { return true; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::integer("quality", "Quality", 0, 100, 95), PropertySpec::boolean("lossless", "Lossless", false),
                PropertySpec::integer("effort", "Encoder effort", 0, 6, 4)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        // The animation decoder handles still and animated files (first frame).
        WebPData wd{reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size())};
        WebPAnimDecoderOptions opts;
        WebPAnimDecoderOptionsInit(&opts);
        opts.color_mode = MODE_BGRA; // straight alpha, BGRA memory order = our Pixel
        WebPAnimDecoder *dec = WebPAnimDecoderNew(&wd, &opts);
        if (!dec) throw FileError("This WebP file could not be read.");
        WebPAnimInfo info;
        uint8_t *buf = nullptr;
        int ts = 0;
        if (!WebPAnimDecoderGetInfo(dec, &info) || !WebPAnimDecoderGetNext(dec, &buf, &ts)) {
            WebPAnimDecoderDelete(dec);
            throw FileError("This WebP file could not be decoded.");
        }
        QImage img(int(info.canvas_width), int(info.canvas_height), QImage::Format_ARGB32);
        if (img.isNull()) {
            WebPAnimDecoderDelete(dec);
            throw FileError("This WebP image is too large.");
        }
        for (int y = 0; y < img.height(); ++y) std::memcpy(img.scanLine(y), buf + size_t(y) * info.canvas_width * 4, size_t(info.canvas_width) * 4);
        WebPAnimDecoderDelete(dec);
        return documentFromImage(img);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        WebPConfig config;
        if (!WebPConfigInit(&config)) throw FileError("WebP encoder initialization failed.");
        config.quality = float(o.value("quality", 95).toInt());
        config.lossless = o.value("lossless", false).toBool() ? 1 : 0;
        config.method = std::clamp(o.value("effort", 4).toInt(), 0, 6);
        config.exact = 0;
        config.thread_level = 1;
        WebPPicture pic;
        WebPPictureInit(&pic);
        pic.use_argb = 1;
        pic.width = flat.width();
        pic.height = flat.height();
        if (!WebPPictureImportBGRA(&pic, flat.constBits(), int(flat.bytesPerLine()))) throw FileError("Out of memory encoding WebP.");
        WebPMemoryWriter writer;
        WebPMemoryWriterInit(&writer);
        pic.writer = WebPMemoryWrite;
        pic.custom_ptr = &writer;
        int ok = WebPEncode(&config, &pic);
        WebPPictureFree(&pic);
        QByteArray out(reinterpret_cast<const char *>(writer.mem), qsizetype(writer.size));
        WebPMemoryWriterClear(&writer);
        if (!ok) throw FileError("WebP encoding failed (the image may be larger than 16383 pixels).");
        return out;
    }
};

// ------------------------------------------------------------ AVIF

class AvifFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("avif"); }
    QString name() const override { return QStringLiteral("AVIF"); }
    QStringList extensions() const override { return {QStringLiteral("avif")}; }
    bool isLossy() const override { return true; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::integer("quality", "Quality", 0, 100, 85),
                PropertySpec::choice("chroma", "Chroma subsampling", {"4:4:4 (best quality)", "4:2:2", "4:2:0 (smallest file)"}, 0),
                PropertySpec::integer("speed", "Encoder speed", 0, 10, 6)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        avifDecoder *dec = avifDecoderCreate();
        if (!dec) throw FileError("Out of memory reading AVIF.");
        avifResult r = avifDecoderSetIOMemory(dec, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
        if (r == AVIF_RESULT_OK) r = avifDecoderParse(dec);
        if (r == AVIF_RESULT_OK) r = avifDecoderNextImage(dec);
        if (r != AVIF_RESULT_OK) {
            std::string msg = std::string("This AVIF file could not be read: ") + avifResultToString(r);
            avifDecoderDestroy(dec);
            throw FileError(msg);
        }
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, dec->image);
        rgb.format = AVIF_RGB_FORMAT_BGRA;
        rgb.depth = 8;
        QImage img(int(dec->image->width), int(dec->image->height), QImage::Format_ARGB32);
        if (img.isNull()) {
            avifDecoderDestroy(dec);
            throw FileError("This AVIF image is too large.");
        }
        rgb.pixels = img.bits();
        rgb.rowBytes = uint32_t(img.bytesPerLine());
        r = avifImageYUVToRGB(dec->image, &rgb);
        avifDecoderDestroy(dec);
        if (r != AVIF_RESULT_OK) throw FileError("This AVIF file could not be decoded.");
        return documentFromImage(img);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        const int chroma = o.value("chroma", 0).toInt();
        avifPixelFormat fmt = chroma == 0 ? AVIF_PIXEL_FORMAT_YUV444 : chroma == 1 ? AVIF_PIXEL_FORMAT_YUV422 : AVIF_PIXEL_FORMAT_YUV420;
        avifImage *image = avifImageCreate(uint32_t(flat.width()), uint32_t(flat.height()), 8, fmt);
        if (!image) throw FileError("Out of memory encoding AVIF.");
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, image);
        rgb.format = AVIF_RGB_FORMAT_BGRA;
        rgb.depth = 8;
        rgb.pixels = const_cast<uint8_t *>(flat.constBits());
        rgb.rowBytes = uint32_t(flat.bytesPerLine());
        rgb.ignoreAlpha = hasAlpha(flat) ? AVIF_FALSE : AVIF_TRUE;
        avifResult r = avifImageRGBToYUV(image, &rgb);
        avifEncoder *enc = r == AVIF_RESULT_OK ? avifEncoderCreate() : nullptr;
        avifRWData out = AVIF_DATA_EMPTY;
        if (enc) {
            const int quality = std::clamp(o.value("quality", 85).toInt(), 0, 100);
#if AVIF_VERSION_MAJOR >= 1
            enc->quality = quality;
            enc->qualityAlpha = quality;
#else  // libavif 0.x (Debian 12): quantizers only, mapped the way libavif 1.x does
            const int q = ((100 - quality) * 63 + 50) / 100;
            enc->minQuantizer = enc->maxQuantizer = q;
            enc->minQuantizerAlpha = enc->maxQuantizerAlpha = q;
#endif
            enc->speed = std::clamp(o.value("speed", 6).toInt(), 0, 10);
            enc->maxThreads = 8;
            r = avifEncoderWrite(enc, image, &out);
            avifEncoderDestroy(enc);
        }
        avifImageDestroy(image);
        if (r != AVIF_RESULT_OK) {
            avifRWDataFree(&out);
            throw FileError(std::string("AVIF encoding failed: ") + avifResultToString(r));
        }
        QByteArray bytes(reinterpret_cast<const char *>(out.data), qsizetype(out.size));
        avifRWDataFree(&out);
        return bytes;
    }
};

// ------------------------------------------------------------ HEIC

struct HeifContext {
    heif_context *ctx = heif_context_alloc();
    ~HeifContext() { heif_context_free(ctx); }
};

class HeicFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("heic"); }
    QString name() const override { return QStringLiteral("HEIC"); }
    QStringList extensions() const override { return {QStringLiteral("heic"), QStringLiteral("heif")}; }
    bool isLossy() const override { return true; }
    bool canSave() const override
    {
        static const bool ok = [] {
            HeifContext c;
            heif_encoder *enc = nullptr;
            heif_error e = heif_context_get_encoder_for_format(c.ctx, heif_compression_HEVC, &enc);
            if (enc) heif_encoder_release(enc);
            return e.code == heif_error_Ok;
        }();
        return ok;
    }
    QList<PropertySpec> saveOptions() const override { return {PropertySpec::integer("quality", "Quality", 0, 100, 90)}; }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        HeifContext c;
        heif_error e = heif_context_read_from_memory_without_copy(c.ctx, data.constData(), size_t(data.size()), nullptr);
        heif_image_handle *handle = nullptr;
        if (e.code == heif_error_Ok) e = heif_context_get_primary_image_handle(c.ctx, &handle);
        heif_image *img = nullptr;
        if (e.code == heif_error_Ok) e = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
        if (e.code != heif_error_Ok) {
            if (handle) heif_image_handle_release(handle);
            throw FileError(std::string("This HEIC file could not be read: ") + e.message);
        }
        int w = heif_image_get_width(img, heif_channel_interleaved), h = heif_image_get_height(img, heif_channel_interleaved);
        size_t stride = 0;
        const uint8_t *px = heifPlaneRO(img, &stride);
        QImage out(w, h, QImage::Format_ARGB32);
        if (!out.isNull())
            for (int y = 0; y < h; ++y) {
                auto *row = reinterpret_cast<Pixel *>(out.scanLine(y));
                const uint8_t *in = px + size_t(y) * stride;
                for (int x = 0; x < w; ++x) row[x] = fromRgbaBytes(in + x * 4);
            }
        heif_image_release(img);
        heif_image_handle_release(handle);
        if (out.isNull()) throw FileError("This HEIC image is too large.");
        return documentFromImage(out);
    }

    QByteArray save(const Document &, const QImage &flat, const SaveOptions &o) const override
    {
        HeifContext c;
        heif_encoder *enc = nullptr;
        heif_error e = heif_context_get_encoder_for_format(c.ctx, heif_compression_HEVC, &enc);
        if (e.code != heif_error_Ok) throw FileError("No HEVC encoder is available for HEIC.");
        heif_encoder_set_lossy_quality(enc, std::clamp(o.value("quality", 90).toInt(), 0, 100));
        const bool alpha = hasAlpha(flat);
        heif_image *img = nullptr;
        e = heif_image_create(flat.width(), flat.height(), heif_colorspace_RGB,
                              alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, &img);
        if (e.code == heif_error_Ok) e = heif_image_add_plane(img, heif_channel_interleaved, flat.width(), flat.height(), 8);
        if (e.code != heif_error_Ok) {
            heif_encoder_release(enc);
            if (img) heif_image_release(img);
            throw FileError(std::string("HEIC encoding failed: ") + e.message);
        }
        size_t stride = 0;
        uint8_t *px = heifPlane(img, &stride);
        for (int y = 0; y < flat.height(); ++y) {
            auto *in = reinterpret_cast<const Pixel *>(flat.constScanLine(y));
            uint8_t *o2 = px + size_t(y) * stride;
            for (int x = 0; x < flat.width(); ++x) {
                *o2++ = pxR(in[x]);
                *o2++ = pxG(in[x]);
                *o2++ = pxB(in[x]);
                if (alpha) *o2++ = pxA(in[x]);
            }
        }
        heif_image_handle *handle = nullptr;
        e = heif_context_encode_image(c.ctx, img, enc, nullptr, &handle);
        heif_image_release(img);
        heif_encoder_release(enc);
        if (handle) heif_image_handle_release(handle);
        if (e.code != heif_error_Ok) throw FileError(std::string("HEIC encoding failed: ") + e.message);
        QByteArray out;
        heif_writer writer{};
        writer.writer_api_version = 1;
        writer.write = [](heif_context *, const void *data, size_t size, void *user) {
            static_cast<QByteArray *>(user)->append(static_cast<const char *>(data), qsizetype(size));
            return heif_error{heif_error_Ok, heif_suberror_Unspecified, ""};
        };
        e = heif_context_write(c.ctx, &writer, &out);
        if (e.code != heif_error_Ok) throw FileError(std::string("HEIC write failed: ") + e.message);
        return out;
    }
};

// ------------------------------------------------------------ TIFF

// In-memory libtiff I/O.
struct TiffMem {
    QByteArray data;
    toff_t pos = 0;
};

tsize_t tiffRead(thandle_t h, tdata_t buf, tsize_t size)
{
    auto *m = static_cast<TiffMem *>(h);
    tsize_t n = std::max<tsize_t>(0, std::min<tsize_t>(size, tsize_t(m->data.size()) - tsize_t(m->pos)));
    std::memcpy(buf, m->data.constData() + m->pos, size_t(n));
    m->pos += toff_t(n);
    return n;
}
tsize_t tiffWrite(thandle_t h, tdata_t buf, tsize_t size)
{
    auto *m = static_cast<TiffMem *>(h);
    if (m->pos + toff_t(size) > toff_t(m->data.size())) m->data.resize(qsizetype(m->pos + toff_t(size)));
    std::memcpy(m->data.data() + m->pos, buf, size_t(size));
    m->pos += toff_t(size);
    return size;
}
toff_t tiffSeek(thandle_t h, toff_t off, int whence)
{
    auto *m = static_cast<TiffMem *>(h);
    toff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? m->pos : toff_t(m->data.size());
    m->pos = base + off;
    return m->pos;
}
int tiffClose(thandle_t) { return 0; }
toff_t tiffSize(thandle_t h) { return toff_t(static_cast<TiffMem *>(h)->data.size()); }
int tiffMap(thandle_t, tdata_t *, toff_t *) { return 0; }
void tiffUnmap(thandle_t, tdata_t, toff_t) {}

class TiffFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("tiff"); }
    QString name() const override { return QStringLiteral("TIFF"); }
    QStringList extensions() const override { return {QStringLiteral("tif"), QStringLiteral("tiff")}; }
    QList<PropertySpec> saveOptions() const override
    {
        return {PropertySpec::choice("compression", "Compression", {"None", "LZW", "Deflate (ZIP)"}, 1)};
    }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        TiffMem mem{data, 0};
        TIFFSetWarningHandler(nullptr);
        TIFF *tif = TIFFClientOpen("mem", "rm", &mem, tiffRead, tiffWrite, tiffSeek, tiffClose, tiffSize, tiffMap, tiffUnmap);
        if (!tif) throw FileError("This TIFF file could not be read.");
        std::unique_ptr<Document> doc;
        int page = 0;
        // Every page with the same size as the first becomes a layer (bottom = first page).
        do {
            uint32_t w = 0, h = 0;
            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
            if (!w || !h || w > 65535 || h > 65535) break;
            if (doc && QSize(int(w), int(h)) != doc->size()) continue;
            QImage img(int(w), int(h), QImage::Format_ARGB32);
            if (img.isNull()) break;
            uint16_t bps = 0, spp = 0, planar = PLANARCONFIG_CONTIG, photometric = 0, extraCount = 0;
            uint16_t *extra = nullptr;
            TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
            TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
            TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
            TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
            TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extra);
            const bool assoc = extraCount > 0 && extra[0] == EXTRASAMPLE_ASSOCALPHA;
            // Fast exact path for plain 8-bit RGB(A) scanline images (what Pane writes).
            if (bps == 8 && planar == PLANARCONFIG_CONTIG && photometric == PHOTOMETRIC_RGB && (spp == 3 || spp == 4) && !TIFFIsTiled(tif)) {
                std::vector<uint8_t> line(size_t(TIFFScanlineSize(tif)));
                bool ok = true;
                for (uint32_t y = 0; y < h && ok; ++y) {
                    ok = TIFFReadScanline(tif, line.data(), y, 0) >= 0;
                    auto *row = reinterpret_cast<Pixel *>(img.scanLine(int(y)));
                    for (uint32_t x = 0; x < w; ++x) {
                        const uint8_t *p = &line[size_t(x) * spp];
                        Pixel px = makePixel(p[0], p[1], p[2], spp == 4 ? p[3] : 255);
                        row[x] = assoc ? unpremultiply(px) : px;
                    }
                }
                if (!ok) {
                    TIFFClose(tif);
                    throw FileError("This TIFF file is corrupt.");
                }
            } else {
            std::vector<uint32_t> raster(size_t(w) * h);
            if (!TIFFReadRGBAImageOriented(tif, w, h, raster.data(), ORIENTATION_TOPLEFT, 0)) {
                if (!doc) {
                    TIFFClose(tif);
                    throw FileError("This TIFF file uses an unsupported format.");
                }
                continue;
            }
            for (uint32_t y = 0; y < h; ++y) {
                auto *row = reinterpret_cast<Pixel *>(img.scanLine(int(y)));
                for (uint32_t x = 0; x < w; ++x) {
                    uint32_t v = raster[size_t(y) * w + x];
                    // The RGBA interface returns associated (premultiplied) alpha.
                    row[x] = unpremultiply(makePixel(TIFFGetR(v), TIFFGetG(v), TIFFGetB(v), TIFFGetA(v)));
                }
            }
            }
            float xres = 0, yres = 0;
            uint16_t unit = RESUNIT_INCH;
            TIFFGetField(tif, TIFFTAG_XRESOLUTION, &xres);
            TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres);
            TIFFGetFieldDefaulted(tif, TIFFTAG_RESOLUTIONUNIT, &unit);
            double k = unit == RESUNIT_CENTIMETER ? 2.54 : 1.0;
            if (!doc) {
                doc = documentFromImage(img, xres > 0 ? xres * k : 96, yres > 0 ? yres * k : 96);
            } else {
                Layer l;
                l.props.name = QStringLiteral("Page %1").arg(page + 1);
                l.surface = Surface::fromImage(img);
                doc->insertLayer(doc->layerCount(), std::move(l));
            }
            ++page;
        } while (TIFFReadDirectory(tif));
        TIFFClose(tif);
        if (!doc) throw FileError("This TIFF file contains no readable images.");
        doc->history().reset(std::make_unique<BaseHistoryItem>(QStringLiteral("Open Image"), glyphs::Image));
        return doc;
    }

    QByteArray save(const Document &doc, const QImage &flat, const SaveOptions &o) const override
    {
        TiffMem mem;
        TIFF *tif = TIFFClientOpen("mem", "w", &mem, tiffRead, tiffWrite, tiffSeek, tiffClose, tiffSize, tiffMap, tiffUnmap);
        if (!tif) throw FileError("Could not create TIFF.");
        const bool alpha = hasAlpha(flat);
        const int spp = alpha ? 4 : 3;
        const int w = flat.width(), h = flat.height();
        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uint32_t(w));
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uint32_t(h));
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, spp);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        if (alpha) {
            uint16_t extra = EXTRASAMPLE_UNASSALPHA;
            TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &extra);
        }
        int comp = o.value("compression", 1).toInt();
        TIFFSetField(tif, TIFFTAG_COMPRESSION, comp == 0 ? COMPRESSION_NONE : comp == 1 ? COMPRESSION_LZW : COMPRESSION_ADOBE_DEFLATE);
        if (comp != 0) TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
        TIFFSetField(tif, TIFFTAG_XRESOLUTION, float(doc.dpiX()));
        TIFFSetField(tif, TIFFTAG_YRESOLUTION, float(doc.dpiY()));
        TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
        TIFFSetField(tif, TIFFTAG_SOFTWARE, "Pane");
        std::vector<uint8_t> row(size_t(w) * spp);
        for (int y = 0; y < h; ++y) {
            auto *in = reinterpret_cast<const Pixel *>(flat.constScanLine(y));
            uint8_t *d = row.data();
            for (int x = 0; x < w; ++x) {
                *d++ = pxR(in[x]);
                *d++ = pxG(in[x]);
                *d++ = pxB(in[x]);
                if (alpha) *d++ = pxA(in[x]);
            }
            if (TIFFWriteScanline(tif, row.data(), uint32_t(y), 0) < 0) {
                TIFFClose(tif);
                throw FileError("TIFF encoding failed.");
            }
        }
        TIFFClose(tif);
        return mem.data;
    }
};

} // namespace

const FileType *webpFileType() { static WebPFileType t; return &t; }
const FileType *avifFileType() { static AvifFileType t; return &t; }
const FileType *heicFileType() { static HeicFileType t; return &t; }
const FileType *tiffFileType() { static TiffFileType t; return &t; }

} // namespace pane
