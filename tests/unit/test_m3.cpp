#include "core/Document.h"
#include "effects/Adjustments.h"
#include "effects/EffectSession.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <catch2/catch_test_macros.hpp>

using namespace pane;

namespace {

Pixel runOnPixel(const QString &id, Pixel p, const QVariantMap &params = {})
{
    const Effect *fx = EffectRegistry::instance().byId(id);
    REQUIRE(fx);
    Surface s(1, 1);
    s.setPixel(0, 0, p);
    QVariantMap prm = fx->defaults();
    for (auto it = params.begin(); it != params.end(); ++it) prm[it.key()] = it.value();
    auto inst = fx->instance(prm, s, Selection());
    Pixel out = 0;
    inst->render(s, QRect(0, 0, 1, 1), &out, 1);
    return out;
}

} // namespace

TEST_CASE("all nine paint.net adjustments are registered in menu order")
{
    QStringList ids;
    for (const Effect *e : EffectRegistry::instance().effects())
        if (e->category() == QLatin1String("Adjustments")) ids << e->id();
    REQUIRE(ids == QStringList{"AutoLevel", "BlackAndWhite", "BrightnessContrast", "Curves", "HueSaturation",
                               "InvertColors", "Levels", "Posterize", "Sepia"});
}

TEST_CASE("invert, black and white, sepia")
{
    REQUIRE(runOnPixel("InvertColors", makePixel(10, 200, 30, 77)) == makePixel(245, 55, 225, 77));
    Pixel bw = runOnPixel("BlackAndWhite", makePixel(255, 0, 0, 255));
    REQUIRE(pxR(bw) == 76);
    REQUIRE(pxR(bw) == pxG(bw));
    REQUIRE(pxG(bw) == pxB(bw));
    Pixel sepia = runOnPixel("Sepia", makePixel(128, 128, 128, 255));
    REQUIRE(pxR(sepia) > pxG(sepia));
    REQUIRE(pxG(sepia) > pxB(sepia));
}

TEST_CASE("brightness and contrast follow paint.net's table")
{
    Pixel p = makePixel(100, 120, 140, 255);
    REQUIRE(brightnessContrast(p, 0, 0) == p);
    Pixel brighter = brightnessContrast(p, 20, 0);
    REQUIRE(pxR(brighter) == 120);
    Pixel full = brightnessContrast(makePixel(200, 200, 200, 255), 0, 100);
    REQUIRE(pxR(full) == 255);
    Pixel lowContrast = brightnessContrast(makePixel(255, 255, 255, 255), 0, -100);
    REQUIRE(pxR(lowContrast) == 127);
}

TEST_CASE("hue / saturation / lightness")
{
    Pixel red = makePixel(255, 0, 0, 200);
    REQUIRE(hueSaturationLightness(red, 0, 100, 0) == red);
    Pixel gray = hueSaturationLightness(red, 0, 0, 0);
    REQUIRE(pxR(gray) == pxG(gray));
    Pixel green = hueSaturationLightness(red, 120, 100, 0);
    REQUIRE(pxG(green) == 255);
    REQUIRE(pxR(green) == 0);
    REQUIRE(pxA(green) == 200);
    REQUIRE(pxR(hueSaturationLightness(makePixel(0, 0, 0, 255), 0, 100, 100)) == 255);
    REQUIRE(pxR(hueSaturationLightness(makePixel(255, 255, 255, 255), 0, 100, -100)) == 0);
}

TEST_CASE("posterize snaps to evenly spaced levels")
{
    Pixel out = runOnPixel("Posterize", makePixel(100, 200, 30, 255), {{"red", 2}, {"linked", true}});
    REQUIRE(pxR(out) == 0);
    REQUIRE(pxG(out) == 255);
    REQUIRE(pxB(out) == 0);
}

TEST_CASE("spline table interpolates through control points")
{
    auto id = splineTable({{0, 0}, {255, 255}});
    for (int i = 0; i < 256; ++i) REQUIRE(id[size_t(i)] == i);
    auto t = splineTable({{0, 0}, {128, 200}, {255, 255}});
    REQUIRE(t[128] == 200);
    REQUIRE(t[0] == 0);
    REQUIRE(t[255] == 255);
    REQUIRE(t[64] > 64);
}

TEST_CASE("levels and auto-level")
{
    LevelChannel l{50, 200, 1.0, 0, 255};
    REQUIRE(l.apply(50) == 0);
    REQUIRE(l.apply(200) == 255);
    REQUIRE(l.apply(125) == 128);
    // Auto-level stretches a narrow-range image.
    Surface s(100, 100);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) s.setPixel(x, y, makePixel(80 + x / 2, 80 + x / 2, 80 + x / 2, 255));
    auto levels = autoLevels(Histogram::compute(s, Selection()));
    REQUIRE(levels[0].inLo <= 81);
    REQUIRE(levels[0].inHi >= 128);
    REQUIRE(levels[0].apply(levels[0].inHi) == 255);
}

TEST_CASE("curves in luminosity mode shift all channels by the curve delta")
{
    QVariantMap p{{"mode", 0}, {"luminosity", pointsToVariant({{0, 0}, {128, 160}, {255, 255}})}};
    Pixel out = runOnPixel("Curves", makePixel(128, 128, 128, 255), p);
    REQUIRE(pxR(out) == 160);
    REQUIRE(pxB(out) == 160);
}

TEST_CASE("effect session previews, cancels and commits within the selection")
{
    auto doc = Document::createBlank(QSize(700, 400), makePixel(10, 20, 30, 255));
    Selection sel = doc->selection();
    QPainterPath path;
    path.addEllipse(QRectF(100, 100, 300, 200));
    sel.setPath(path, true);
    doc->setSelection(sel);
    QImage original = doc->flattenToImage();
    const Effect *invert = EffectRegistry::instance().byId("InvertColors");
    {
        EffectSession s(*doc, *invert);
        s.update({});
        s.waitForRender();
        REQUIRE(doc->layer(0).surface.pixel(250, 200) == makePixel(245, 235, 225, 255));
        REQUIRE(doc->layer(0).surface.pixel(5, 5) == makePixel(10, 20, 30, 255));
        s.cancel();
        REQUIRE(doc->flattenToImage() == original);
    }
    {
        EffectSession s(*doc, *invert);
        s.update({});
        s.update({}); // a second update cancels and restarts
        s.finish();
        REQUIRE(doc->layer(0).surface.pixel(250, 200) == makePixel(245, 235, 225, 255));
        REQUIRE(doc->history().count() == 2);
        REQUIRE(doc->history().item(1).name() == QStringLiteral("Invert Colors"));
        doc->history().undo();
        REQUIRE(doc->flattenToImage() == original);
    }
}
