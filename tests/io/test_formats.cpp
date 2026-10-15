// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Commands.h"
#include "io/FileType.h"
#include "io/Formats.h"
#include "io/Zip.h"

#include <QFile>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

using namespace pane;

namespace {

QImage testImage(int w, int h, bool alpha)
{
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t a = alpha ? uint32_t(std::clamp((x + y) * 255 / (w + h - 2), 0, 255)) : 255;
            // Smooth content (codecs behave realistically) with a sharp feature.
            uint32_t r = uint32_t(128 + 100 * std::sin(x / 17.0)), g = uint32_t(128 + 100 * std::cos(y / 13.0)),
                     b = uint32_t(128 + 100 * std::sin((x + y) / 23.0));
            if (x > w / 3 && x < w / 2 && y > h / 3 && y < h / 2) r = g = b = 20;
            reinterpret_cast<Pixel *>(img.scanLine(y))[x] = makePixel(r, g, b, a);
        }
    return img;
}

// PSNR over RGB of opaque-ish pixels plus alpha.
double psnr(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) return 0;
    double mse = 0;
    long n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            Pixel p = reinterpret_cast<const Pixel *>(a.constScanLine(y))[x], q = reinterpret_cast<const Pixel *>(b.constScanLine(y))[x];
            double da = int(pxA(p)) - int(pxA(q));
            mse += da * da;
            ++n;
            if (pxA(p) < 64) continue; // color under near-transparent pixels is unimportant
            for (int s : {0, 8, 16}) {
                double d = int((p >> s) & 255) - int((q >> s) & 255);
                mse += d * d;
                ++n;
            }
        }
    mse /= double(n);
    return mse == 0 ? 100 : 10 * std::log10(255.0 * 255.0 / mse);
}

std::unique_ptr<Document> roundTrip(const FileType *t, const Document &doc, SaveOptions opts = {})
{
    for (auto it = propertyDefaults(t->saveOptions()); auto [k, v] : it.asKeyValueRange())
        if (!opts.contains(k)) opts[k] = v;
    QByteArray data = t->save(doc, t->supportsLayers() ? QImage() : doc.flattenToImage(), opts);
    REQUIRE(data.size() > 0);
    // Load through content sniffing with a misleading extension.
    QTemporaryDir dir;
    QString path = dir.filePath("file.bin");
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
    auto back = filetypes::load(path);
    REQUIRE(back->fileTypeId() == t->id());
    return back;
}

std::unique_ptr<Document> docFrom(const QImage &img)
{
    auto doc = Document::createBlank(img.size(), 0);
    doc->layer(0).surface = Surface::fromImage(img);
    return doc;
}

} // namespace

TEST_CASE("lossless formats round-trip exactly")
{
    QImage opaque = testImage(301, 157, false), alpha = testImage(301, 157, true);
    // Formats that keep straight alpha exactly.
    for (const FileType *t : {tiffFileType(), tgaFileType()}) {
        INFO(t->id().toStdString());
        for (const QImage &img : {opaque, alpha}) {
            auto doc = docFrom(img);
            REQUIRE(roundTrip(t, *doc)->flattenToImage() == doc->flattenToImage());
        }
    }
    auto doc = docFrom(alpha);
    REQUIRE(roundTrip(tgaFileType(), *doc, {{"rle", false}})->flattenToImage() == doc->flattenToImage());
    REQUIRE(roundTrip(tiffFileType(), *doc, {{"compression", 0}})->flattenToImage() == doc->flattenToImage());
    REQUIRE(roundTrip(tiffFileType(), *doc, {{"compression", 2}})->flattenToImage() == doc->flattenToImage());
    REQUIRE(roundTrip(ddsFileType(), *doc, {{"format", 3}})->flattenToImage() == doc->flattenToImage());
    REQUIRE(psnr(roundTrip(webpFileType(), *docFrom(opaque), {{"lossless", true}})->flattenToImage(), opaque) == 100);
}

TEST_CASE("lossy formats round-trip with good quality")
{
    QImage opaque = testImage(256, 192, false), alpha = testImage(256, 192, true);
    struct Case { const FileType *t; SaveOptions o; double minPsnr; bool alphaOk; };
    std::vector<Case> cases = {
        {webpFileType(), {{"quality", 90}}, 32, true},
        {avifFileType(), {{"quality", 85}}, 32, true},
        {ddsFileType(), {{"format", 0}}, 24, false}, // BC1: 1-bit alpha
        {ddsFileType(), {{"format", 1}}, 24, true},
        {ddsFileType(), {{"format", 2}}, 24, true},
        {gifFileType(), {{"dithering", 0}}, 22, false},
    };
    if (heicFileType()->canSave()) cases.push_back({heicFileType(), {{"quality", 90}}, 30, true});
    for (const Case &c : cases) {
        INFO(c.t->id().toStdString() << " " << c.o.value("format").toInt());
        REQUIRE(psnr(roundTrip(c.t, *docFrom(opaque), c.o)->flattenToImage(), opaque) > c.minPsnr);
        if (c.alphaOk) REQUIRE(psnr(roundTrip(c.t, *docFrom(alpha), c.o)->flattenToImage(), alpha) > c.minPsnr - 4);
    }
}

TEST_CASE("GIF is palettized with a transparent index")
{
    QImage img = testImage(120, 80, true);
    auto back = roundTrip(gifFileType(), *docFrom(img), {{"threshold", 128}})->flattenToImage();
    std::set<Pixel> colors;
    for (int y = 0; y < back.height(); ++y)
        for (int x = 0; x < back.width(); ++x) {
            Pixel p = reinterpret_cast<const Pixel *>(back.constScanLine(y))[x];
            Pixel o = reinterpret_cast<const Pixel *>(img.constScanLine(y))[x];
            REQUIRE((pxA(p) == 0) == (pxA(o) < 128));
            colors.insert(p);
        }
    REQUIRE(colors.size() <= 256);
}

TEST_CASE("DDS mip maps and ICO sizes")
{
    QImage img = testImage(200, 120, true);
    auto dds = roundTrip(ddsFileType(), *docFrom(img), {{"format", 3}, {"mipmaps", true}});
    REQUIRE(dds->size() == img.size());
    auto ico = roundTrip(icoFileType(), *docFrom(img), {{"sizes", 0}});
    REQUIRE(ico->size() == QSize(256, 256));
    auto small = roundTrip(icoFileType(), *docFrom(img), {{"sizes", 1}});
    REQUIRE(small->size() == QSize(48, 48));
}

TEST_CASE("layered formats keep layers, names, opacity, visibility and blend modes")
{
    auto doc = docFrom(testImage(330, 270, false));
    commands::addNewLayer(*doc);
    doc->layer(1).surface.fillRect(QRect(30, 40, 100, 60), makePixel(10, 200, 30, 180));
    LayerProperties p = doc->layer(1).props;
    p.name = QStringLiteral("Ünïcode layer ✓");
    p.opacity = 128;
    p.blendMode = BlendMode::Multiply;
    doc->setLayerProperties(1, p);
    commands::addNewLayer(*doc);
    doc->layer(2).surface.fillRect(QRect(0, 0, 330, 10), 0xFF0000FF);
    LayerProperties hidden = doc->layer(2).props;
    hidden.visible = false;
    hidden.blendMode = BlendMode::Xor;
    doc->setLayerProperties(2, hidden);

    for (const FileType *t : {oraFileType(), pdnFileType()}) {
        INFO(t->id().toStdString());
        auto back = roundTrip(t, *doc);
        REQUIRE(back->layerCount() == 3);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(back->layer(i).props == doc->layer(i).props);
            REQUIRE(back->layer(i).surface.toImage() == doc->layer(i).surface.toImage());
        }
        REQUIRE(back->flattenToImage() == doc->flattenToImage());
    }
}

TEST_CASE("pdn files are larger than 4 MB per layer chunk and still round-trip")
{
    auto doc = docFrom(testImage(1500, 900, true)); // 5.4 MB of pixels -> 2 chunks
    auto back = roundTrip(pdnFileType(), *doc);
    REQUIRE(back->flattenToImage() == doc->flattenToImage());
}

TEST_CASE("zip writer output is readable")
{
    std::vector<zip::Entry> e{{"mimetype", "image/openraster", false}, {"a/b.txt", QByteArray(100000, 'x'), true}};
    auto files = zip::read(zip::write(e));
    REQUIRE(files.size() == 2);
    REQUIRE(files["a/b.txt"] == QByteArray(100000, 'x'));
    REQUIRE(files["mimetype"] == "image/openraster");
}
