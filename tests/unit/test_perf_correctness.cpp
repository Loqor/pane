// Correctness checks for the performance work: every fast path must produce
// exactly what the straightforward implementation would.
#include "core/Commands.h"
#include "core/Paint.h"
#include "io/FileType.h"
#include "io/Formats.h"
#include "io/PngWriter.h"
#include "tools/Tools.h"

#include <QBuffer>
#include <QImageReader>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace pane;

TEST_CASE("reciprocal division is exact for all blend numerators")
{
    for (uint32_t d = 1; d < 256; ++d)
        for (uint32_t n = 0; n <= 65535; ++n)
            if (divSmall(n, d) != n / d) FAIL("divSmall(" << n << ", " << d << ")");
}

TEST_CASE("blend results match the reference formula for random pixels")
{
    std::mt19937 rng(1);
    for (int m = 0; m < BlendModeCount; ++m) {
        for (int i = 0; i < 20000; ++i) {
            Pixel l = rng(), r = rng();
            uint32_t op = rng() & 255;
            // Reference: straight division.
            uint32_t rA = op == 255 ? pxA(r) : mul255(pxA(r), op);
            Pixel expect;
            if (rA == 0) expect = l;
            else if (pxA(l) == 0) expect = withAlpha(r, rA);
            else {
                uint32_t lA = pxA(l), y = mul255(lA, 255 - rA), total = y + rA, x = mul255(lA, rA), z = rA - x;
                auto ch = [&](uint32_t a, uint32_t b) {
                    uint32_t f = BlendMode(m) == BlendMode::Normal ? b : blendChannel(BlendMode(m), a, b);
                    return (a * y + b * z + f * x + total / 2) / total;
                };
                if (BlendMode(m) == BlendMode::Normal && rA == 255) expect = withAlpha(r, 255);
                else expect = makePixel(ch(pxR(l), pxR(r)), ch(pxG(l), pxG(r)), ch(pxB(l), pxB(r)), total);
            }
            Pixel row = l;
            blendRow(BlendMode(m), &row, &r, 1, op);
            REQUIRE(blendPixel(BlendMode(m), l, r, op) == expect);
            REQUIRE(row == expect);
        }
    }
}

TEST_CASE("parallel PNG encoder output decodes identically with libpng and Qt")
{
    std::mt19937 rng(3);
    for (QSize s : {QSize(1, 1), QSize(37, 5), QSize(1500, 900), QSize(2100, 1300)}) {
        for (bool alpha : {false, true}) {
            QImage img(s, QImage::Format_ARGB32);
            for (int y = 0; y < s.height(); ++y)
                for (int x = 0; x < s.width(); ++x) {
                    uint32_t noise = rng() & 0x0F0F0F;
                    img.setPixel(x, y, (alpha ? (((x * 3) & 255) << 24) : 0xFF000000u) | (((x + y) & 255) << 16) | noise);
                }
            QByteArray png = encodePngParallel(img, alpha, 96, 96);
            QImage viaQt;
            REQUIRE(viaQt.loadFromData(png, "PNG"));
            viaQt = viaQt.convertToFormat(QImage::Format_ARGB32);
            QImage expect = alpha ? img : img.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_ARGB32);
            REQUIRE(viaQt == expect);
            auto doc = pngFileType()->load(png);
            REQUIRE(doc->layer(0).surface.toImage() == expect); // flatten() drops RGB of alpha-0 pixels
        }
    }
}

TEST_CASE("exact flip and rotate match QImage transforms")
{
    auto doc = Document::createBlank(QSize(301, 517), 0);
    std::mt19937 rng(5);
    for (int y = 0; y < 517; y += 3)
        for (int x = 0; x < 301; x += 2) doc->layer(0).surface.setPixel(x, y, rng() | 0xFF000000u);
    QImage orig = doc->flattenToImage();
    commands::flipImage(*doc, Qt::Horizontal);
    REQUIRE(doc->flattenToImage() == orig.flipped(Qt::Horizontal));
    doc->history().undo();
    commands::flipImage(*doc, Qt::Vertical);
    REQUIRE(doc->flattenToImage() == orig.flipped(Qt::Vertical));
    doc->history().undo();
    for (int deg : {90, 180, 270}) {
        commands::rotateImage(*doc, deg);
        QTransform t;
        t.rotate(deg);
        REQUIRE(doc->flattenToImage() == orig.transformed(t));
        doc->history().undo();
    }
}

TEST_CASE("moving selected pixels by a fractional drag snaps to whole pixels exactly")
{
    auto doc = Document::createBlank(QSize(600, 400), 0xFFFFFFFF);
    doc->layer(0).surface.fillRect(QRect(100, 100, 50, 40), 0xFFFF0000);
    QPainterPath p;
    p.addRect(100, 100, 50, 40);
    Selection sel = doc->selection();
    sel.setPath(p, false);
    doc->setSelection(sel);
    auto mp = MovePixelsSession::lift(*doc, 0, false);
    mp->transform().translate({300.4, 49.6});
    mp->render(true);
    mp->finish();
    const Surface &s = doc->layer(0).surface;
    REQUIRE(s.pixel(400, 150) == 0xFFFF0000u);
    REQUIRE(s.pixel(449, 189) == 0xFFFF0000u);
    REQUIRE(s.pixel(450, 150) == 0xFFFFFFFFu);
    REQUIRE(s.pixel(120, 120) == 0u); // the source area is left transparent, as in paint.net
}

TEST_CASE("parallel global and contiguous flood fills agree with expectations on big images")
{
    Surface s(3000, 700);
    s.fill(0xFFFFFFFF);
    for (int y = 0; y < 700; ++y) s.setPixel(1500, y, 0xFF000000);
    QRect b;
    auto c = floodMask(s, {10, 10}, 0.0, false, &b);
    REQUIRE(b == QRect(0, 0, 1500, 700));
    auto g = floodMask(s, {10, 10}, 0.0, true, &b);
    REQUIRE(b == QRect(0, 0, 3000, 700));
    REQUIRE(g[size_t(5) * 3000 + 1500] == 0);
    REQUIRE(g[size_t(5) * 3000 + 2000] == 255);
}
