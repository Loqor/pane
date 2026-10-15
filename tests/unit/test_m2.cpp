// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Commands.h"
#include "core/MaskPath.h"
#include "tools/DrawTools.h"
#include "tools/EditSession.h"
#include "tools/Gradient.h"

#include <QPainter>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace pane;

namespace {

// Rasterizes a path without antialiasing, sampling pixel centers.
std::vector<uint8_t> rasterize(const QPainterPath &p, int w, int h)
{
    std::vector<uint8_t> out(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) out[size_t(y) * w + x] = p.contains(QPointF(x + 0.5, y + 0.5)) ? 255 : 0;
    return out;
}

} // namespace

TEST_CASE("maskToPath reproduces masks exactly, including holes and diagonal touches")
{
    const int w = 40, h = 30;
    std::mt19937 rng(7);
    for (int trial = 0; trial < 20; ++trial) {
        std::vector<uint8_t> mask(size_t(w) * h);
        for (auto &m : mask) m = (rng() % 3 == 0) ? 255 : 0;
        // Some solid blocks with holes.
        for (int y = 5; y < 20; ++y)
            for (int x = 5; x < 25; ++x) mask[size_t(y) * w + x] = (x > 10 && x < 15 && y > 9 && y < 14) ? 0 : 255;
        QPainterPath p = maskToPath(mask, w, QRect(0, 0, w, h));
        REQUIRE(rasterize(p, w, h) == mask);
    }
}

TEST_CASE("maskToPath honors bounds offset")
{
    const int w = 20, h = 20;
    std::vector<uint8_t> mask(size_t(w) * h, 0);
    for (int y = 8; y < 12; ++y)
        for (int x = 3; x < 9; ++x) mask[size_t(y) * w + x] = 255;
    QPainterPath p = maskToPath(mask, w, QRect(2, 7, 10, 6));
    REQUIRE(p.boundingRect() == QRectF(3, 8, 6, 4));
}

TEST_CASE("gradient parameter for each type")
{
    QPointF a(0, 0), b(100, 0);
    auto t = [&](GradientType ty, QPointF p, GradientRepeat r = GradientRepeat::None) { return gradientT(ty, r, a, b, p); };
    REQUIRE(t(GradientType::Linear, {50, 20}) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Linear, {-50, 0}) == Catch::Approx(0.0));
    REQUIRE(t(GradientType::Linear, {150, 0}) == Catch::Approx(1.0));
    REQUIRE(t(GradientType::LinearReflected, {-50, 0}) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::LinearDiamond, {25, 25}) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Radial, {0, 50}) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Conical, {0, 50}) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Conical, {-50, 0}) == Catch::Approx(1.0));
    REQUIRE(t(GradientType::Linear, {150, 0}, GradientRepeat::Sawtooth) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Linear, {150, 0}, GradientRepeat::Triangular) == Catch::Approx(0.5));
    REQUIRE(t(GradientType::Linear, {175, 0}, GradientRepeat::Triangular) == Catch::Approx(0.25));
    double s = t(GradientType::SpiralCW, {30, 40});
    REQUIRE(s >= 0.0);
    REQUIRE(s < 1.0);
}

TEST_CASE("premultiplied color interpolation avoids dark fringes")
{
    Pixel c = lerpColorPremul(0x00000000, 0xFFFFFFFF, 0.5);
    REQUIRE(pxR(c) == 255);
    REQUIRE(std::abs(int(pxA(c)) - 128) <= 1);
}

TEST_CASE("recolor keeps the difference from the basis color")
{
    Pixel basis = makePixel(100, 100, 100, 255), repl = makePixel(200, 50, 50, 255);
    Pixel out = RecolorTool::recolorPixel(makePixel(110, 90, 100, 200), basis, repl);
    REQUIRE(pxR(out) == 210);
    REQUIRE(pxG(out) == 40);
    REQUIRE(pxB(out) == 50);
    REQUIRE(pxA(out) == 200);
}

TEST_CASE("spline curve passes through its control points")
{
    QPolygonF pts{{0, 0}, {50, 80}, {100, 0}};
    QPainterPath p = LineCurveTool::curvePath(pts, false);
    bool hitsMiddle = false;
    for (int i = 0; i <= 200; ++i)
        if (QLineF(p.pointAtPercent(i / 200.0), QPointF(50, 80)).length() < 1.0) hitsMiddle = true;
    REQUIRE(hitsMiddle);
    REQUIRE(LineCurveTool::curvePath({{0, 0}, {10, 10}}, true).elementCount() == 2);
}

TEST_CASE("edit session re-renders, cancels and commits cleanly")
{
    auto doc = Document::createBlank(QSize(600, 300), 0xFFFFFFFF);
    QImage original = doc->flattenToImage();
    {
        EditSession s(*doc, 0);
        s.render(QRect(0, 0, 300, 300), [](int, int, Pixel) { return Pixel(0xFFFF0000); });
        REQUIRE(doc->layer(0).surface.pixel(10, 10) == 0xFFFF0000);
        // Moving the region restores what is no longer covered.
        s.render(QRect(300, 0, 300, 300), [](int, int, Pixel) { return Pixel(0xFF00FF00); });
        REQUIRE(doc->layer(0).surface.pixel(10, 10) == 0xFFFFFFFF);
        REQUIRE(doc->layer(0).surface.pixel(310, 10) == 0xFF00FF00);
        s.cancel();
        REQUIRE(doc->flattenToImage() == original);
    }
    {
        EditSession s(*doc, 0);
        QImage overlay(10, 10, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(0xFF0000FF);
        s.renderOverlay(overlay, QPoint(5, 5), PaintMode::Blend);
        s.finish("Shapes", "");
        REQUIRE(doc->layer(0).surface.pixel(7, 7) == 0xFF0000FF);
        REQUIRE(doc->history().count() == 2);
        doc->history().undo();
        REQUIRE(doc->flattenToImage() == original);
    }
}

TEST_CASE("edit session respects the selection")
{
    auto doc = Document::createBlank(QSize(100, 100), 0xFFFFFFFF);
    Selection sel = doc->selection();
    QPainterPath p;
    p.addRect(0, 0, 50, 100);
    sel.setPath(p, false);
    doc->setSelection(sel);
    EditSession s(*doc, 0);
    s.render(doc->bounds(), [](int, int, Pixel) { return Pixel(0xFF000000); });
    REQUIRE(doc->layer(0).surface.pixel(10, 10) == 0xFF000000);
    REQUIRE(doc->layer(0).surface.pixel(80, 10) == 0xFFFFFFFF);
}

TEST_CASE("every shape type produces a closed path inside its box")
{
    QRectF r(10, 10, 100, 60);
    for (int i = 0; i < int(shapeTypeNames().size()); ++i) {
        QPainterPath p = shapePath(ShapeType(i), r);
        REQUIRE_FALSE(p.isEmpty());
        REQUIRE(r.adjusted(-1, -1, 1, 1).contains(p.boundingRect()));
    }
}

TEST_CASE("combining big traced selections stays exact")
{
    auto doc = Document::createBlank(QSize(200, 200));
    std::vector<uint8_t> a(200 * 200, 0);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x) a[size_t(y) * 200 + x] = ((x / 2 + y / 2) % 2) ? 255 : 0; // checker → many edges
    Selection s = doc->selection();
    s.setPath(maskToPath(a, 200, QRect(0, 0, 200, 200)), false);
    QPainterPath rect;
    rect.addRect(0, 0, 100, 200);
    s.combine(rect, CombineMode::Intersect, false);
    REQUIRE(s.coverageAt(1, 1) == 0);
    REQUIRE(s.coverageAt(2, 0) == 255);
    REQUIRE(s.coverageAt(150, 1) == 0);
}
