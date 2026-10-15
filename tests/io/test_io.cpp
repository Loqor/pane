// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "io/FileType.h"
#include "io/Formats.h"
#include "io/Nrbf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace pane;

namespace {

std::unique_ptr<Document> sampleDoc()
{
    auto doc = Document::createBlank(QSize(300, 280), 0xFFFFFFFF);
    for (int y = 0; y < 280; ++y)
        for (int x = 0; x < 300; ++x)
            doc->layer(0).surface.setPixel(x, y, makePixel(x % 256, y % 256, (x + y) % 256, 255));
    doc->setDpi(150, 150);
    return doc;
}

double psnr(const QImage &a, const QImage &b)
{
    double mse = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            for (int s : {0, 8, 16}) {
                double d = int((p >> s) & 255) - int((q >> s) & 255);
                mse += d * d;
            }
        }
    mse /= a.width() * a.height() * 3.0;
    return mse == 0 ? 100 : 10 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("PNG round trip is lossless, including alpha and DPI")
{
    QTemporaryDir dir;
    auto doc = sampleDoc();
    doc->layer(0).surface.setPixel(5, 5, makePixel(10, 20, 30, 77));
    QString path = dir.filePath("a.png");
    filetypes::save(*doc, path, pngFileType(), {});
    REQUIRE_FALSE(doc->isDirty());
    auto back = filetypes::load(path);
    REQUIRE(back->size() == doc->size());
    REQUIRE(back->flattenToImage() == doc->flattenToImage());
    REQUIRE(std::abs(back->dpiX() - 150) < 0.5);
}

TEST_CASE("JPEG round trip keeps quality")
{
    QTemporaryDir dir;
    auto doc = sampleDoc();
    QString path = dir.filePath("a.jpg");
    SaveOptions o;
    o["quality"] = 95;
    filetypes::save(*doc, path, jpegFileType(), o);
    auto back = filetypes::load(path);
    REQUIRE(back->size() == doc->size());
    REQUIRE(psnr(back->flattenToImage(), doc->flattenToImage()) > 35);
}

TEST_CASE("BMP round trip is lossless for opaque and transparent images")
{
    QTemporaryDir dir;
    auto doc = sampleDoc();
    QString path = dir.filePath("a.bmp");
    filetypes::save(*doc, path, bmpFileType(), {});
    REQUIRE(filetypes::load(path)->flattenToImage() == doc->flattenToImage());

    doc->layer(0).surface.setPixel(1, 1, makePixel(1, 2, 3, 100));
    filetypes::save(*doc, path, bmpFileType(), {});
    QImage back = filetypes::load(path)->flattenToImage();
    REQUIRE(qAlpha(back.pixel(1, 1)) == 100);
}

TEST_CASE("atomic save leaves the original intact when the directory is missing")
{
    QTemporaryDir dir;
    auto doc = sampleDoc();
    REQUIRE_THROWS_AS(filetypes::save(*doc, dir.filePath("missing/a.png"), pngFileType(), {}), FileError);
    REQUIRE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("loading garbage fails cleanly")
{
    QTemporaryDir dir;
    QString path = dir.filePath("bad.png");
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("\x89PNG\r\n\x1a\n garbage");
    f.close();
    REQUIRE_THROWS_AS(filetypes::load(path), FileError);
    QFile pdn(dir.filePath("bad.pdn"));
    REQUIRE(pdn.open(QIODevice::WriteOnly));
    pdn.write("PDN3\x05\x00\x00<a/>\x00\x01\x63");
    pdn.close();
    REQUIRE_THROWS_AS(filetypes::load(dir.filePath("bad.pdn")), FileError);
}

TEST_CASE("NRBF reader parses classes, strings, arrays and references")
{
    QByteArray d;
    auto i32 = [&](qint32 v) { char b[4]; qToLittleEndian(v, b); d.append(b, 4); };
    auto str = [&](const QByteArray &s) { d.append(char(s.size())); d.append(s); };
    d.append(char(0)); i32(1); i32(-1); i32(1); i32(0);          // header
    d.append(char(12)); i32(2); str("Lib");                     // library
    d.append(char(5)); i32(1); str("PaintDotNet.Document");     // ClassWithMembersAndTypes
    i32(3); str("width"); str("name"); str("data");
    d.append(char(0)); d.append(char(1)); d.append(char(7));    // Primitive, String, PrimitiveArray
    d.append(char(8)); d.append(char(2));                       // Int32, Byte
    i32(2);                                                     // library id
    i32(640);                                                   // width
    d.append(char(6)); i32(3); str("hello");                    // name: BinaryObjectString
    d.append(char(9)); i32(4);                                  // data: MemberReference
    d.append(char(15)); i32(4); i32(3); d.append(char(2)); d.append("\x01\x02\x03", 3);
    d.append(char(11));                                         // MessageEnd

    nrbf::Reader r;
    qsizetype pos = 0;
    r.parse(d, pos);
    REQUIRE(pos == d.size());
    const nrbf::Object *doc = r.object(1);
    REQUIRE(doc);
    REQUIRE(r.memberPrim(*doc, "width").toInt() == 640);
    REQUIRE(r.memberString(*doc, "name") == "hello");
    const nrbf::Object *arr = r.memberObject(*doc, "data");
    REQUIRE(arr);
    REQUIRE(arr->bytes == QByteArray("\x01\x02\x03", 3));
}
