#pragma once

#include "core/LayerEdit.h"

#include <QPointF>
#include <functional>
#include <vector>

namespace pane {

enum class PaintMode {
    Blend,     // paint.net "Normal" alpha blending
    Overwrite, // paint.net "Overwrite" alpha blending: replaces color and alpha
    Erase,     // reduces alpha
};

// A paint operation on one layer defined by an accumulating 8-bit coverage map.
// Coverage combines with max(), so overlapping dabs within one stroke never
// build up opacity (matching paint.net). Each changed pixel is recomputed from
// the layer's original content, which also makes cancel exact.
class CoverageStroke {
public:
    CoverageStroke(Document &doc, int layer, Pixel color, PaintMode mode, bool clipToSelection = true);

    // Optional per-pixel color (replaces the fixed color): used by Clone Stamp and Recolor.
    using ColorFn = std::function<Pixel(int x, int y, Pixel before)>;
    void setColorFunction(ColorFn fn) { m_colorFn = std::move(fn); }

    // strength (0..1) scales the dab's coverage (tablet pressure → opacity).
    void addDab(QPointF center, double radius, double hardness, bool antialias, double strength = 1.0);
    // Dabs from `from` to `to` with the given spacing (in pixels).
    void addLine(QPointF from, QPointF to, double radius, double hardness, bool antialias, double spacing,
                 double strengthFrom = 1.0, double strengthTo = 1.0);
    void addPixel(int x, int y);
    void addPixelLine(QPoint from, QPoint to);
    void addRect(const QRect &r);
    // mask is width*height of the document; only `bounds` is read.
    void addMask(const std::vector<uint8_t> &mask, const QRect &bounds);

    // Region changed since the last call.
    QRect takeDirty();

    void cancel() { m_edit.cancel(); }
    std::unique_ptr<HistoryItem> finish(const QString &name, const QString &glyph) { return m_edit.finish(name, glyph); }

private:
    template <typename CovFn> void stamp(const QRect &area, CovFn &&cov);
    Pixel compose(Pixel before, uint32_t coverage, Pixel color) const;

    LayerEdit m_edit;
    Pixel m_color;
    ColorFn m_colorFn;
    PaintMode m_mode;
    bool m_clip;
    std::unordered_map<int, std::unique_ptr<uint8_t[]>> m_cov;
    QRect m_dirty;
};

// paint.net flood-fill similarity test. tolerance is 0..1 (UI percentage / 100).
bool colorsSimilar(Pixel a, Pixel b, double tolerance);

// Computes a fill mask (255 = filled) seeded at `seed` on `sample`.
// global = paint.net "Global" flood mode (all similar pixels, not just contiguous).
std::vector<uint8_t> floodMask(const Surface &sample, QPoint seed, double tolerance, bool global, QRect *bounds);

} // namespace pane
