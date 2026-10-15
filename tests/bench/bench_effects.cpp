// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// Times every effect at its defaults on a 3000x2000 layer through EffectSession
// (multithreaded, as in the app).
#include "core/Document.h"
#include "effects/EffectSession.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <cstdio>

using namespace pane;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    auto doc = Document::createBlank(QSize(3000, 2000));
    Surface &s = doc->layer(0).surface;
    for (int y = 0; y < 2000; ++y)
        for (int x = 0; x < 3000; ++x)
            s.setPixel(x, y, makePixel(uint32_t((x * 7) & 255), uint32_t((y * 5) & 255), uint32_t(((x ^ y) * 3) & 255), 255));
    for (const Effect *e : EffectRegistry::instance().effects()) {
        if (e->ui() == Effect::UI::Custom) continue;
        EffectSession session(*doc, *e);
        QElapsedTimer t;
        t.start();
        session.update(e->defaults());
        session.waitForRender();
        std::printf("%-22s %7.0f ms\n", qPrintable(e->id()), double(t.elapsed()));
        session.cancel();
    }
    return 0;
}
