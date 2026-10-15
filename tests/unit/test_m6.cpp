#include "core/Document.h"
#include "core/Paint.h"
#include "core/PaletteFile.h"
#include "tools/Tool.h"
#include "ui/PrintLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pane;

TEST_CASE("paint.net palette files parse and format")
{
    auto pal = parsePalette(QStringLiteral("; comment line\nFFFF0000 ; red\n\n  ff00ff00\n0000FF\nnot-a-color\n"));
    REQUIRE(pal.size() == size_t(PaletteSize));
    REQUIRE(pal[0] == 0xFFFF0000u);
    REQUIRE(pal[1] == 0xFF00FF00u);
    REQUIRE(pal[2] == 0xFF0000FFu); // 6 digits: opaque
    REQUIRE(pal[3] == 0xFFFFFFFFu); // padded with white
    auto again = parsePalette(formatPalette(pal));
    REQUIRE(again == pal);
}

TEST_CASE("pressure curve, minimum size and opacity")
{
    ToolSettings s;
    s.brushWidth = 100;
    s.pressureSize = true;
    s.pressureOpacity = false;
    s.pressureGamma = 1.0;
    s.pressureMin = 10;
    REQUIRE(pressureDab(s, 1.0).radius == Catch::Approx(50));
    REQUIRE(pressureDab(s, 0.0).radius == Catch::Approx(5));
    REQUIRE(pressureDab(s, 0.5).radius == Catch::Approx(27.5));
    REQUIRE(pressureDab(s, 0.5).strength == 1.0);
    s.pressureGamma = 2.0; // firm: half pressure -> quarter effect
    REQUIRE(pressureDab(s, 0.5).radius == Catch::Approx(5 + 45 * 0.25));
    s.pressureSize = false;
    s.pressureOpacity = true;
    REQUIRE(pressureDab(s, 0.5).radius == Catch::Approx(50));
    REQUIRE(pressureDab(s, 0.5).strength == Catch::Approx(0.25));
}

TEST_CASE("dab strength scales coverage")
{
    auto doc = Document::createBlank(QSize(64, 64), 0xFFFFFFFF);
    CoverageStroke stroke(*doc, 0, 0xFF000000, PaintMode::Blend);
    stroke.addDab({32, 32}, 10, 1.0, false, 0.5);
    REQUIRE(std::abs(int(pxR(doc->layer(0).surface.pixel(32, 32))) - 128) <= 1);
}

TEST_CASE("print layout uses physical size when it fits, otherwise fits the page")
{
    QRectF page(0, 0, 2400, 3300); // 8x11 in at 300 dpi
    // 600x300 px at 100 dpi = 6x3 in = 1800x900 device px: fits, centered.
    QRectF r = printTargetRect(QSize(600, 300), 100, 100, page, 300, 300);
    REQUIRE(r.size() == QSizeF(1800, 900));
    REQUIRE(r.center() == page.center());
    // 6000x3000 px at 100 dpi = 60x30 in: scaled to the page width.
    QRectF big = printTargetRect(QSize(6000, 3000), 100, 100, page, 300, 300);
    REQUIRE(big.width() == Catch::Approx(2400));
    REQUIRE(big.height() == Catch::Approx(1200));
}
