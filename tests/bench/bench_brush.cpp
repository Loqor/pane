// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// Brush stroke benchmark: 6000x4000, 5 layers. Reports per-dab time for
// paint + composite of the dirty region (the work done per input event).
#include "core/Commands.h"
#include "core/Paint.h"
#include "render/Compositor.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace pane;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    auto doc = Document::createBlank(QSize(6000, 4000));
    for (int i = 0; i < 4; ++i) {
        commands::addNewLayer(*doc);
        doc->layer(doc->activeLayerIndex()).surface.fillRect(QRect(i * 1000, 0, 2000, 4000), makePixel(40 * i, 100, 200, 180));
        doc->layer(doc->activeLayerIndex()).props.blendMode = BlendMode(i * 3 % BlendModeCount);
    }
    Compositor comp(*doc);
    comp.update();
    CoverageStroke stroke(*doc, doc->activeLayerIndex(), 0xFF202020, PaintMode::Blend);
    std::vector<double> times;
    QPointF last(500, 500);
    for (int i = 0; i < 1000; ++i) {
        QPointF p(500 + 4500 * (i / 1000.0), 2000 + 1500 * std::sin(i / 40.0));
        QElapsedTimer t;
        t.start();
        stroke.addLine(last, p, 50, 0.75, true, 12);
        comp.invalidate(stroke.takeDirty());
        comp.update();
        times.push_back(t.nsecsElapsed() / 1e6);
        last = p;
    }
    std::sort(times.begin(), times.end());
    std::printf("dab events: %zu  median %.2f ms  p95 %.2f ms  max %.2f ms\n", times.size(), times[times.size() / 2],
                times[times.size() * 95 / 100], times.back());
    return times[times.size() * 95 / 100] < 16.0 ? 0 : 1;
}
