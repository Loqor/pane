#include "effects/EffectSession.h"
#include "effects/EffectUtil.h"

#include <QElapsedTimer>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace pane;

namespace {

Surface testImage(int w, int h)
{
    Surface s(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t a = (x > 40 && x < w - 40 && y > 30 && y < h - 30) ? 255 : ((x + y) % 3 ? 0 : 128);
            s.setPixel(x, y, makePixel(uint32_t((x * 7) & 255), uint32_t((y * 5) & 255), uint32_t(((x ^ y) * 3) & 255), a));
        }
    return s;
}

std::vector<Pixel> renderRects(const EffectInstance &inst, const Surface &src, const std::vector<QRect> &rects)
{
    std::vector<Pixel> out(size_t(src.width()) * src.height(), 0xDEADBEEF);
    for (const QRect &r : rects) {
        std::vector<Pixel> buf(size_t(r.width()) * r.height());
        inst.render(src, r, buf.data(), r.width());
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x)
                out[size_t(y) * src.width() + x] = buf[size_t(y - r.top()) * r.width() + (x - r.left())];
    }
    return out;
}

} // namespace

TEST_CASE("paint.net effect menu is complete")
{
    std::map<QString, int> perCategory;
    for (const Effect *e : EffectRegistry::instance().effects()) perCategory[e->category()]++;
    REQUIRE(perCategory["Artistic"] == 3);
    REQUIRE(perCategory["Blurs"] == 8);
    REQUIRE(perCategory["Color"] == 2);
    REQUIRE(perCategory["Distort"] == 9);
    REQUIRE(perCategory["Noise"] == 3);
    REQUIRE(perCategory["Object"] == 3);
    REQUIRE(perCategory["Photo"] == 5);
    REQUIRE(perCategory["Render"] == 4);
    REQUIRE(perCategory["Stylize"] == 4);
}

TEST_CASE("every effect is tile-independent at its defaults")
{
    const int w = 300, h = 260;
    Surface src = testImage(w, h);
    Selection sel;
    sel.setImageSize(src.size());
    // Split into uneven rectangles that don't align with tiles.
    std::vector<QRect> pieces;
    for (int y = 0; y < h; y += 97)
        for (int x = 0; x < w; x += 113) pieces.push_back(QRect(x, y, std::min(113, w - x), std::min(97, h - y)));
    for (const Effect *e : EffectRegistry::instance().effects()) {
        QVariantMap p = e->defaults();
        if (e->id() == QLatin1String("Curves")) p = {{"mode", 0}, {"luminosity", QVariantList{QPointF(0, 0), QPointF(255, 200)}}};
        if (e->id() == QLatin1String("Levels")) p = {{"levels", QVariantList{}}};
        p["_primary"] = 0xFF102030u;
        p["_secondary"] = 0xFFE0D0C0u;
        INFO("effect " << e->id().toStdString());
        auto inst = e->instance(p, src, sel);
        QElapsedTimer t;
        t.start();
        auto whole = renderRects(*inst, src, {src.bounds()});
        auto tiled = renderRects(*inst, src, pieces);
        REQUIRE(whole == tiled);
        REQUIRE(std::count(whole.begin(), whole.end(), Pixel(0xDEADBEEF)) == 0);
        CHECK(t.elapsed() < 20000);
    }
}

TEST_CASE("gaussian blur keeps flat colors flat and blurs edges")
{
    const Effect *e = EffectRegistry::instance().byId("GaussianBlur");
    Surface flat(64, 64);
    flat.fill(0xFF336699);
    auto inst = e->instance({{"radius", 10}}, flat, Selection());
    std::vector<Pixel> out(64 * 64);
    inst->render(flat, QRect(0, 0, 64, 64), out.data(), 64);
    REQUIRE(std::all_of(out.begin(), out.end(), [](Pixel p) { return p == 0xFF336699u; }));

    Surface edge(64, 64);
    edge.fillRect(QRect(0, 0, 32, 64), 0xFFFFFFFF);
    edge.fillRect(QRect(32, 0, 32, 64), 0xFF000000);
    inst->render(edge, QRect(0, 0, 64, 64), out.data(), 64);
    REQUIRE(pxR(out[32 * 64 + 31]) < 230);
    REQUIRE(pxR(out[32 * 64 + 31]) > 25);
}

TEST_CASE("pixelate makes uniform cells; quantize limits colors; median removes salt noise")
{
    Surface src = testImage(100, 100);
    std::vector<Pixel> out(100 * 100);
    auto pix = EffectRegistry::instance().byId("Pixelate")->instance({{"size", 10}}, src, Selection());
    pix->render(src, QRect(0, 0, 100, 100), out.data(), 100);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) REQUIRE(out[size_t(y) * 100 + x] == out[0]);

    Selection all;
    all.setImageSize(src.size());
    auto q = EffectRegistry::instance().byId("Quantize")->instance({{"colors", 8}, {"strength", 0}}, src, all);
    q->render(src, QRect(0, 0, 100, 100), out.data(), 100);
    std::set<Pixel> colors;
    for (Pixel p : out) colors.insert(p | 0xFF000000u);
    REQUIRE(colors.size() <= 8);

    Surface salt(50, 50);
    salt.fill(0xFF808080);
    salt.setPixel(25, 25, 0xFFFFFFFF);
    auto med = EffectRegistry::instance().byId("Median")->instance({{"radius", 2}, {"percentile", 50}}, salt, Selection());
    std::vector<Pixel> m(50 * 50);
    med->render(salt, QRect(0, 0, 50, 50), m.data(), 50);
    REQUIRE(m[25 * 50 + 25] == 0xFF808080u);
}

TEST_CASE("drop shadow appears outside the object")
{
    Surface src(80, 80);
    src.fillRect(QRect(20, 20, 20, 20), 0xFFFF0000);
    auto ds = EffectRegistry::instance().byId("DropShadow")->instance(
        {{"offsetX", 10}, {"offsetY", 10}, {"radius", 0}, {"spread", 0}, {"opacity", 100}, {"color", 0xFF000000u}, {"keep", true}}, src,
        Selection());
    std::vector<Pixel> out(80 * 80);
    ds->render(src, QRect(0, 0, 80, 80), out.data(), 80);
    REQUIRE(out[size_t(45) * 80 + 45] == 0xFF000000u); // shadow
    REQUIRE(out[size_t(25) * 80 + 25] == 0xFFFF0000u); // object kept on top
    REQUIRE(out[size_t(5) * 80 + 5] == 0u);            // untouched background
}

TEST_CASE("align object moves the object's bounds")
{
    Surface src(100, 100);
    src.fillRect(QRect(10, 10, 20, 20), 0xFF00FF00);
    Selection sel;
    sel.setImageSize(src.size());
    auto al = EffectRegistry::instance().byId("AlignObject")->instance({{"align", 8}}, src, sel);
    std::vector<Pixel> out(100 * 100);
    al->render(src, QRect(0, 0, 100, 100), out.data(), 100);
    REQUIRE(out[size_t(99) * 100 + 99] == 0xFF00FF00u);
    REQUIRE(out[size_t(15) * 100 + 15] == 0u);
}
