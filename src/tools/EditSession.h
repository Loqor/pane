#pragma once

#include "core/LayerEdit.h"
#include "core/Paint.h"

#include <functional>

namespace pane {

// An editable render on one layer: every render() first restores what the
// previous render touched, so the result can be adjusted until finish().
// Rendering is clipped by the selection and runs in parallel per tile.
class EditSession {
public:
    EditSession(Document &doc, int layer) : m_doc(doc), m_edit(doc, layer) {}

    // fn must be thread-safe. For pixels in `region`:
    //   out = lerp(before, fn(x, y, before), selectionCoverage)
    using PixelFn = std::function<Pixel(int x, int y, Pixel before)>;
    void render(const QRect &region, const PixelFn &fn);
    // Composites a premultiplied overlay whose top-left is at `at` (image coords).
    void renderOverlay(const QImage &premul, QPoint at, PaintMode mode);
    void clear() { render({}, {}); }

    void cancel();
    // Pushes one history item (if anything changed).
    void finish(const QString &name, const QString &glyph);
    Document &document() { return m_doc; }

private:
    Document &m_doc;
    LayerEdit m_edit;
    QRect m_last;
};

} // namespace pane
