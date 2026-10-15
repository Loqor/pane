// End-to-end timings of user-visible operations on a 6000x4000 document.
#include "core/Commands.h"
#include "core/Paint.h"
#include "io/FileType.h"
#include "io/Formats.h"
#include "render/Compositor.h"
#include "tools/EditSession.h"
#include "tools/Tools.h"
#include "ui/CanvasView.h"
#include "ui/Workspace.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cstdio>

using namespace pane;

template <typename F> void timeIt(const char *name, F &&f)
{
    QElapsedTimer t;
    t.start();
    f();
    std::printf("%-34s %8.1f ms\n", name, t.nsecsElapsed() / 1e6);
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const QSize size(6000, 4000);
    auto doc = Document::createBlank(size);
    {
        Surface &s = doc->layer(0).surface;
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                s.setPixel(x, y, makePixel(uint32_t((x * 7) & 255), uint32_t((y * 5) & 255), uint32_t(((x ^ y) * 3) & 255), 255));
    }
    for (int i = 0; i < 4; ++i) {
        commands::addNewLayer(*doc);
        doc->activeLayer().surface.fillRect(QRect(i * 1000, 0, 2500, 4000), makePixel(40 * i, 100, 200, 180));
        doc->activeLayer().props.blendMode = BlendMode(i * 3 % BlendModeCount);
    }

    {
        std::vector<Pixel> a(1 << 22, makePixel(10, 200, 30, 255)), b(1 << 22, makePixel(200, 20, 90, 180));
        for (BlendMode m : {BlendMode::Normal, BlendMode::Overlay, BlendMode::ColorDodge}) {
            char name[64];
            std::snprintf(name, sizeof name, "blendRow %s 4M px 1 thread", qPrintable(blendModeName(m)));
            timeIt(name, [&] { blendRow(m, a.data(), b.data(), int(a.size()), 255); });
        }
        QImage big(6000, 4000, QImage::Format_ARGB32);
        big.fill(0x80336699);
        timeIt("QImage ARGB32 -> RGBA32FPx4P", [&] { (void)big.convertToFormat(QImage::Format_RGBA32FPx4_Premultiplied); });
    }
    {
        auto plain = Document::createBlank(size, 0xFF808080);
        Compositor pc(*plain);
        timeIt("composite full (1 layer, normal)", [&] { pc.invalidateAll(); pc.update(); });
        commands::addNewLayer(*plain);
        plain->activeLayer().surface.fill(0x80FF0000);
        timeIt("composite full (2 layers, normal)", [&] { pc.invalidateAll(); pc.update(); });
    }
    Compositor comp(*doc);
    timeIt("composite full (5 layers)", [&] { comp.invalidateAll(); comp.update(); });
    timeIt("flatten", [&] { doc->flatten(); });
    timeIt("thumbnail 64", [&] { comp.thumbnail(64); });
    timeIt("layer thumbnail 36", [&] { Compositor::layerThumbnail(doc->layer(0).surface, 36); });

    QString png = dir.filePath("a.png"), jpg = dir.filePath("a.jpg");
    timeIt("save PNG (flatten+encode+fsync)", [&] { filetypes::save(*doc, png, pngFileType(), {}); });
    timeIt("save JPEG q95", [&] { filetypes::save(*doc, jpg, jpegFileType(), {}); });
    timeIt("open PNG", [&] { filetypes::load(png); });
    timeIt("open JPEG", [&] { filetypes::load(jpg); });

    doc->setActiveLayerIndex(0);
    timeIt("fill selection (whole image)", [&] { commands::fillSelection(*doc, 0xFF00FF00); });
    timeIt("undo fill", [&] { doc->history().undo(); });
    timeIt("redo fill", [&] { doc->history().redo(); });
    timeIt("flood fill global", [&] {
        QRect b;
        floodMask(doc->layer(0).surface, {10, 10}, 0.5, true, &b);
    });
    timeIt("flood fill contiguous", [&] {
        QRect b;
        floodMask(doc->layer(0).surface, {10, 10}, 0.5, false, &b);
    });
    timeIt("rotate 90", [&] { commands::rotateImage(*doc, 90); });
    timeIt("resize to 50% best quality", [&] { commands::resizeImage(*doc, QSize(2000, 3000), Resampling::BestQuality); });
    timeIt("undo resize", [&] { doc->history().undo(); });
    timeIt("flip horizontal", [&] { commands::flipImage(*doc, Qt::Horizontal); });

    // Interactive per-mouse-move paths.
    doc->history().jumpTo(4); // after the four "Add New Layer" items
    doc->setActiveLayerIndex(1);
    commands::selectAll(*doc);
    {
        auto mp = MovePixelsSession::lift(*doc, 1, false);
        timeIt("move pixels: one drag step (24MP)", [&] { mp->transform().translate({3, 2}); mp->render(true); });
        timeIt("move pixels: one drag step again", [&] { mp->transform().translate({3, 2}); mp->render(true); });
        mp->finish();
    }
    {
        EditSession es(*doc, 1);
        timeIt("gradient: one drag step (24MP)", [&] {
            es.render(doc->bounds(), [](int x, int y, Pixel b) { return overPixel(b, lerpPixel(0xFF000000, 0xFFFFFFFF, uint32_t(x * 255 / 6000)), 255); });
        });
        es.cancel();
    }
    commands::deselect(*doc);

    Workspace ws(std::move(doc));
    CanvasView view;
    view.resize(1600, 1000);
    view.show();
    view.setWorkspace(&ws);
    QCoreApplication::processEvents();
    for (double z : {0.1, 0.25, 0.5, 1.0, 4.0}) {
        view.setZoom(z);
        char name[64];
        std::snprintf(name, sizeof name, "canvas paint zoom %.0f%%", z * 100);
        timeIt(name, [&] { view.viewport()->grab(); });
    }
    return 0;
}
