#include "core/BlendMode.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Paint.h"
#include "core/Resample.h"

#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace pane;

TEST_CASE("premultiply round trip is stable for opaque and transparent")
{
    REQUIRE(premultiply(0xFF102030) == 0xFF102030);
    REQUIRE(premultiply(0x00102030) == 0);
    Pixel p = makePixel(200, 100, 50, 128);
    Pixel back = unpremultiply(premultiply(p));
    REQUIRE(std::abs(int(pxR(back)) - 200) <= 2);
    REQUIRE(pxA(back) == 128);
}

TEST_CASE("normal blend matches source-over")
{
    Pixel dst = makePixel(0, 0, 255, 255);
    Pixel src = makePixel(255, 0, 0, 255);
    REQUIRE(blendPixel(BlendMode::Normal, dst, src, 255) == src);
    Pixel half = blendPixel(BlendMode::Normal, dst, src, 128);
    REQUIRE(pxA(half) == 255);
    REQUIRE(std::abs(int(pxR(half)) - 128) <= 1);
    REQUIRE(std::abs(int(pxB(half)) - 127) <= 1);
    // Over transparent: result is the source with scaled alpha.
    REQUIRE(blendPixel(BlendMode::Normal, 0, src, 255) == src);
}

TEST_CASE("blend channel functions match reference formulas")
{
    REQUIRE(blendChannel(BlendMode::Multiply, 255, 128) == 128);
    REQUIRE(blendChannel(BlendMode::Multiply, 0, 200) == 0);
    REQUIRE(blendChannel(BlendMode::Screen, 0, 100) == 100);
    REQUIRE(blendChannel(BlendMode::Screen, 255, 100) == 255);
    REQUIRE(blendChannel(BlendMode::Difference, 10, 200) == 190);
    REQUIRE(blendChannel(BlendMode::Additive, 200, 100) == 255);
    REQUIRE(blendChannel(BlendMode::Lighten, 10, 20) == 20);
    REQUIRE(blendChannel(BlendMode::Darken, 10, 20) == 10);
    REQUIRE(blendChannel(BlendMode::Xor, 0xF0, 0x0F) == 0xFF);
    REQUIRE(blendChannel(BlendMode::Negation, 200, 200) == 110);
    REQUIRE(blendChannel(BlendMode::Negation, 200, 100) == 210);
    REQUIRE(blendChannel(BlendMode::ColorDodge, 100, 255) == 255);
    REQUIRE(blendChannel(BlendMode::ColorBurn, 100, 0) == 0);
    REQUIRE(blendChannel(BlendMode::Overlay, 0, 200) == 0);
    REQUIRE(blendChannel(BlendMode::Overlay, 255, 200) == 255);
}

TEST_CASE("every blend mode leaves lhs unchanged when rhs is transparent, and is rhs over transparent")
{
    for (int m = 0; m < BlendModeCount; ++m) {
        auto mode = BlendMode(m);
        Pixel l = makePixel(12, 34, 56, 200), r = makePixel(200, 150, 100, 255);
        REQUIRE(blendPixel(mode, l, 0, 255) == l);
        REQUIRE(blendPixel(mode, 0, r, 255) == r);
        Pixel full = blendPixel(mode, withAlpha(l, 255), r, 255);
        REQUIRE(pxA(full) == 255);
        REQUIRE(pxR(full) == blendChannel(mode, 12, 200));
    }
}

TEST_CASE("surface tiles are lazy and copy-on-write")
{
    Surface s(600, 300);
    REQUIRE(s.tilesX() == 3);
    REQUIRE(s.tilesY() == 2);
    REQUIRE(s.allocatedBytes() == 0);
    s.setPixel(300, 10, 0xFF112233);
    REQUIRE(s.pixel(300, 10) == 0xFF112233);
    REQUIRE(s.allocatedBytes() == sizeof(Tile));
    Surface copy = s;
    copy.setPixel(300, 10, 0xFF000000);
    REQUIRE(s.pixel(300, 10) == 0xFF112233);
    REQUIRE(copy.pixel(300, 10) == 0xFF000000);
    QImage img = s.toImage();
    REQUIRE(img.pixel(300, 10) == 0xFF112233);
    Surface back = Surface::fromImage(img);
    REQUIRE(back.pixel(300, 10) == 0xFF112233);
    REQUIRE(back.allocatedBytes() == sizeof(Tile));
}

TEST_CASE("random strokes undo and redo pixel-identically")
{
    auto doc = Document::createBlank(QSize(700, 500));
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> px(0, 700), py(0, 500);
    std::vector<QImage> states{doc->flattenToImage()};
    for (int i = 0; i < 40; ++i) {
        if (i % 7 == 3) commands::addNewLayer(*doc);
        else {
            CoverageStroke stroke(*doc, doc->activeLayerIndex(), makePixel(rng() & 255, rng() & 255, rng() & 255, 200),
                                  i % 5 == 0 ? PaintMode::Erase : PaintMode::Blend);
            stroke.addLine({px(rng), py(rng)}, {px(rng), py(rng)}, 12, 0.75, true, 2);
            auto item = stroke.finish("Paintbrush", "");
            if (item) doc->history().push(std::move(item));
        }
        states.push_back(doc->flattenToImage());
    }
    // Some strokes may have produced no change; compare by history index.
    int n = doc->history().currentIndex();
    std::vector<QImage> byIndex(size_t(n + 1));
    byIndex[size_t(n)] = doc->flattenToImage();
    for (int i = n; i > 0; --i) {
        doc->history().undo();
        byIndex[size_t(i - 1)] = doc->flattenToImage();
    }
    REQUIRE(byIndex[0] == states[0]);
    for (int i = 1; i <= n; ++i) {
        doc->history().redo();
        REQUIRE(doc->flattenToImage() == byIndex[size_t(i)]);
    }
}

TEST_CASE("stroke coverage does not accumulate within a stroke")
{
    auto doc = Document::createBlank(QSize(64, 64), 0xFFFFFFFF);
    CoverageStroke stroke(*doc, 0, makePixel(0, 0, 0, 128), PaintMode::Blend);
    for (int i = 0; i < 10; ++i) stroke.addDab({32, 32}, 8, 1.0, false);
    Pixel p = doc->layer(0).surface.pixel(32, 32);
    REQUIRE(std::abs(int(pxR(p)) - 127) <= 1);
}

TEST_CASE("selection clips painting")
{
    auto doc = Document::createBlank(QSize(100, 100), 0xFFFFFFFF);
    Selection sel = doc->selection();
    QPainterPath p;
    p.addRect(0, 0, 50, 100);
    sel.setPath(p, false);
    doc->setSelection(sel);
    commands::fillSelection(*doc, 0xFF000000);
    REQUIRE(doc->layer(0).surface.pixel(10, 10) == 0xFF000000);
    REQUIRE(doc->layer(0).surface.pixel(60, 10) == 0xFFFFFFFF);
    doc->history().undo();
    REQUIRE(doc->layer(0).surface.pixel(10, 10) == 0xFFFFFFFF);
}

TEST_CASE("flood fill respects contiguity and tolerance")
{
    Surface s(10, 10);
    s.fill(0xFFFFFFFF);
    for (int y = 0; y < 10; ++y) s.setPixel(5, y, 0xFF000000); // wall
    QRect b;
    auto m = floodMask(s, {1, 1}, 0.0, false, &b);
    REQUIRE(m[1 * 10 + 4] == 255);
    REQUIRE(m[1 * 10 + 5] == 0);
    REQUIRE(m[1 * 10 + 7] == 0);
    REQUIRE(b == QRect(0, 0, 5, 10));
    auto g = floodMask(s, {1, 1}, 0.0, true, &b);
    REQUIRE(g[1 * 10 + 7] == 255);
    auto all = floodMask(s, {1, 1}, 1.0, false, &b);
    REQUIRE(all[1 * 10 + 5] == 255);
}

TEST_CASE("structural commands round trip through history")
{
    auto doc = Document::createBlank(QSize(300, 200));
    doc->layer(0).surface.setPixel(0, 0, 0xFFFF0000);
    commands::rotateImage(*doc, 90);
    REQUIRE(doc->size() == QSize(200, 300));
    REQUIRE(doc->layer(0).surface.pixel(199, 0) == 0xFFFF0000);
    commands::resizeImage(*doc, QSize(100, 150), Resampling::BestQuality);
    REQUIRE(doc->size() == QSize(100, 150));
    commands::canvasSize(*doc, QSize(120, 150), 4, 0xFF00FF00);
    REQUIRE(doc->size() == QSize(120, 150));
    REQUIRE(doc->layer(0).surface.pixel(0, 75) == 0xFF00FF00);
    commands::duplicateLayer(*doc);
    commands::mergeLayerDown(*doc);
    REQUIRE(doc->layerCount() == 1);
    doc->history().jumpTo(0);
    REQUIRE(doc->size() == QSize(300, 200));
    REQUIRE(doc->layer(0).surface.pixel(0, 0) == 0xFFFF0000);
}

TEST_CASE("crop to selection")
{
    auto doc = Document::createBlank(QSize(100, 100));
    doc->layer(0).surface.setPixel(20, 30, 0xFF0000FF);
    QPainterPath p;
    p.addRect(20, 30, 10, 10);
    Selection s = doc->selection();
    s.setPath(p, false);
    doc->setSelection(s);
    commands::cropToSelection(*doc);
    REQUIRE(doc->size() == QSize(10, 10));
    REQUIRE(doc->layer(0).surface.pixel(0, 0) == 0xFF0000FF);
}

TEST_CASE("resampling preserves solid colors")
{
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(0xFF336699);
    for (auto mode : {Resampling::BestQuality, Resampling::Bicubic, Resampling::Bilinear, Resampling::NearestNeighbor}) {
        QImage a = resampleImage(img, QSize(17, 33), mode);
        QImage b = resampleImage(img, QSize(200, 90), mode);
        REQUIRE(a.size() == QSize(17, 33));
        REQUIRE(b.size() == QSize(200, 90));
        for (QImage *r : {&a, &b}) {
            QRgb c = r->pixel(r->width() / 2, r->height() / 2);
            REQUIRE(std::abs(qRed(c) - 0x33) <= 1);
            REQUIRE(std::abs(qBlue(c) - 0x99) <= 1);
            REQUIRE(qAlpha(c) == 255);
        }
    }
}
