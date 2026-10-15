// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Commands.h"

#include "core/Glyphs.h"
#include "core/Paint.h"
#include "core/Parallel.h"

#include <QPainter>
#include <QTransform>

namespace pane::commands {

namespace {

// Applies `mutate` to a copy of the document state, then swaps it in and records history.
template <typename F> void structural(Document &doc, const QString &name, const QString &glyph, F &&mutate)
{
    DocumentState before = doc.captureState();
    DocumentState after = before;
    mutate(after);
    doc.restoreState(after);
    doc.history().push(std::make_unique<StateHistoryItem>(name, glyph, std::move(before), std::move(after)));
}

enum class Remap { FlipH, FlipV, Rot90, Rot180, Rot270 };

// Exact pixel remap (flip / 90° rotations), parallel over destination tiles.
Surface remapSurface(const Surface &s, Remap op)
{
    const int W = s.width(), H = s.height();
    const bool swap = op == Remap::Rot90 || op == Remap::Rot270;
    Surface out(swap ? H : W, swap ? W : H);
    parallelFor(out.tileCount(), [&](int idx) {
        QRect tr = out.tileRect(idx);
        Tile *t = nullptr;
        for (int y = tr.top(); y <= tr.bottom(); ++y)
            for (int x = tr.left(); x <= tr.right(); ++x) {
                int sx = x, sy = y;
                switch (op) {
                case Remap::FlipH: sx = W - 1 - x; break;
                case Remap::FlipV: sy = H - 1 - y; break;
                case Remap::Rot90: sx = y; sy = H - 1 - x; break;
                case Remap::Rot180: sx = W - 1 - x; sy = H - 1 - y; break;
                case Remap::Rot270: sx = W - 1 - y; sy = x; break;
                }
                Pixel p = s.pixel(sx, sy);
                if (!p) continue;
                if (!t) t = out.mutableTile(idx);
                t->px[(y - tr.top()) * TileSize + (x - tr.left())] = p;
            }
    });
    return out;
}

Remap flipOp(Qt::Orientation o) { return o == Qt::Horizontal ? Remap::FlipH : Remap::FlipV; }

QTransform imageFlipMapping(Qt::Orientation o, QSize size)
{
    // Maps image coordinates for a flip (used for the selection path).
    return o == Qt::Horizontal ? QTransform(-1, 0, 0, 1, size.width(), 0) : QTransform(1, 0, 0, -1, 0, size.height());
}

} // namespace

void addNewLayer(Document &doc)
{
    structural(doc, QStringLiteral("Add New Layer"), glyphs::LayerAdd, [&](DocumentState &s) {
        Layer l;
        l.props.name = doc.nextLayerName();
        l.surface = Surface(s.size.width(), s.size.height());
        s.layers.insert(s.layers.begin() + s.activeLayer + 1, std::move(l));
        s.activeLayer += 1;
    });
}

void deleteLayer(Document &doc)
{
    if (doc.layerCount() <= 1) return;
    structural(doc, QStringLiteral("Delete Layer"), glyphs::Delete, [&](DocumentState &s) {
        s.layers.erase(s.layers.begin() + s.activeLayer);
        s.activeLayer = std::max(0, s.activeLayer - 1);
    });
}

void duplicateLayer(Document &doc)
{
    structural(doc, QStringLiteral("Duplicate Layer"), glyphs::Duplicate, [&](DocumentState &s) {
        Layer copy = s.layers[size_t(s.activeLayer)];
        copy.props.name = QStringLiteral("%1 copy").arg(copy.props.name);
        s.layers.insert(s.layers.begin() + s.activeLayer + 1, std::move(copy));
        s.activeLayer += 1;
    });
}

void mergeLayerDown(Document &doc)
{
    if (doc.activeLayerIndex() == 0) return;
    structural(doc, QStringLiteral("Merge Layer Down"), glyphs::Merge, [&](DocumentState &s) {
        const Layer &top = s.layers[size_t(s.activeLayer)];
        Layer &bottom = s.layers[size_t(s.activeLayer - 1)];
        if (top.props.visible) {
            for (int idx = 0; idx < top.surface.tileCount(); ++idx) {
                const Tile *src = top.surface.tile(idx);
                if (!src) continue;
                QRect tr = top.surface.tileRect(idx);
                Tile *dst = bottom.surface.mutableTile(idx);
                for (int y = 0; y < tr.height(); ++y)
                    blendRow(top.props.blendMode, &dst->px[y * TileSize], &src->px[y * TileSize], tr.width(),
                             top.props.opacity);
            }
        }
        s.layers.erase(s.layers.begin() + s.activeLayer);
        s.activeLayer -= 1;
    });
}

void moveLayerUp(Document &doc)
{
    if (doc.activeLayerIndex() >= doc.layerCount() - 1) return;
    structural(doc, QStringLiteral("Move Layer Up"), glyphs::ArrowUp, [&](DocumentState &s) {
        std::swap(s.layers[size_t(s.activeLayer)], s.layers[size_t(s.activeLayer + 1)]);
        s.activeLayer += 1;
    });
}

void moveLayerDown(Document &doc)
{
    if (doc.activeLayerIndex() == 0) return;
    structural(doc, QStringLiteral("Move Layer Down"), glyphs::ArrowDown, [&](DocumentState &s) {
        std::swap(s.layers[size_t(s.activeLayer)], s.layers[size_t(s.activeLayer - 1)]);
        s.activeLayer -= 1;
    });
}

void commitLayerProperties(Document &doc, int index, const LayerProperties &before, const LayerProperties &after,
                           const QString &name)
{
    if (before == after) return;
    DocumentState b = doc.captureState();
    b.layers[size_t(index)].props = before;
    doc.setLayerProperties(index, after);
    doc.history().push(std::make_unique<StateHistoryItem>(name, glyphs::Properties,
                                                          std::move(b), doc.captureState()));
}

void moveLayerTo(Document &doc, int from, int to)
{
    to = std::clamp(to, 0, doc.layerCount() - 1);
    if (from == to) return;
    structural(doc, QStringLiteral("Move Layer"), to > from ? glyphs::ArrowUp : glyphs::ArrowDown, [&](DocumentState &s) {
        Layer l = std::move(s.layers[size_t(from)]);
        s.layers.erase(s.layers.begin() + from);
        s.layers.insert(s.layers.begin() + to, std::move(l));
        s.activeLayer = to;
    });
}

void flatten(Document &doc)
{
    if (doc.layerCount() <= 1) return;
    structural(doc, QStringLiteral("Flatten"), glyphs::Flatten, [&](DocumentState &s) {
        Layer l;
        l.props.name = s.layers[0].props.name;
        l.surface = doc.flatten();
        s.layers.clear();
        s.layers.push_back(std::move(l));
        s.activeLayer = 0;
    });
}

void addLayerFromImage(Document &doc, const QImage &img, const QString &name, const QString &historyName, QPoint at)
{
    structural(doc, historyName, glyphs::LayerAdd, [&](DocumentState &s) {
        Layer l;
        l.props.name = name;
        l.surface = Surface(s.size.width(), s.size.height());
        l.surface.writeImage(img, at);
        l.surface.compact();
        s.layers.insert(s.layers.begin() + s.activeLayer + 1, std::move(l));
        s.activeLayer += 1;
    });
}

void resizeImage(Document &doc, QSize size, Resampling mode)
{
    if (size == doc.size()) return;
    structural(doc, QStringLiteral("Resize Image"), glyphs::Resize, [&](DocumentState &s) {
        for (Layer &l : s.layers) l.surface = Surface::fromImage(resampleImage(l.surface.toImage(), size, mode));
        QTransform t = QTransform::fromScale(double(size.width()) / s.size.width(), double(size.height()) / s.size.height());
        s.selection.setImageSize(size);
        s.selection.transform(t);
        s.size = size;
    });
}

void canvasSize(Document &doc, QSize size, int anchor, Pixel backgroundFill)
{
    if (size == doc.size()) return;
    structural(doc, QStringLiteral("Canvas Size"), glyphs::CanvasSize, [&](DocumentState &s) {
        int ax = anchor % 3, ay = anchor / 3;
        QPoint offset((size.width() - s.size.width()) * ax / 2, (size.height() - s.size.height()) * ay / 2);
        for (size_t i = 0; i < s.layers.size(); ++i) {
            Layer &l = s.layers[i];
            Surface ns(size.width(), size.height());
            if (i == 0 && pxA(backgroundFill)) ns.fill(backgroundFill);
            QImage img = l.surface.toImage();
            ns.writeImage(img.copy(QRect(-offset, size).intersected(img.rect())), QPoint(std::max(0, offset.x()), std::max(0, offset.y())));
            ns.compact();
            l.surface = std::move(ns);
        }
        s.selection.setImageSize(size);
        s.selection.transform(QTransform::fromTranslate(offset.x(), offset.y()));
        s.size = size;
    });
}

void flipImage(Document &doc, Qt::Orientation o)
{
    structural(doc, o == Qt::Horizontal ? QStringLiteral("Flip Horizontal") : QStringLiteral("Flip Vertical"),
               o == Qt::Horizontal ? glyphs::FlipH : glyphs::FlipV, [&](DocumentState &s) {
                   for (Layer &l : s.layers) l.surface = remapSurface(l.surface, flipOp(o));
                   s.selection.transform(imageFlipMapping(o, s.size));
               });
}

void rotateImage(Document &doc, int deg)
{
    deg = ((deg % 360) + 360) % 360;
    if (deg == 0) return;
    QString name = deg == 90 ? QStringLiteral("Rotate 90° Clockwise")
                 : deg == 270 ? QStringLiteral("Rotate 90° Counter-Clockwise")
                              : QStringLiteral("Rotate 180°");
    QString glyph = deg == 270 ? glyphs::RotateLeft : glyphs::RotateRight;
    structural(doc, name, glyph, [&](DocumentState &s) {
        QSize ns = deg == 180 ? s.size : s.size.transposed();
        Remap op = deg == 90 ? Remap::Rot90 : deg == 270 ? Remap::Rot270 : Remap::Rot180;
        parallelFor(int(s.layers.size()), [&](int i) { s.layers[size_t(i)].surface = remapSurface(s.layers[size_t(i)].surface, op); });
        // Selection: rotate about origin then translate back into the positive quadrant.
        QTransform m;
        if (deg == 90) m = QTransform(0, 1, -1, 0, s.size.height(), 0);
        else if (deg == 270) m = QTransform(0, -1, 1, 0, 0, s.size.width());
        else m = QTransform(-1, 0, 0, -1, s.size.width(), s.size.height());
        s.selection.setImageSize(ns);
        s.selection.transform(m);
        s.size = ns;
    });
}

void flipLayer(Document &doc, Qt::Orientation o)
{
    structural(doc, o == Qt::Horizontal ? QStringLiteral("Flip Layer Horizontal") : QStringLiteral("Flip Layer Vertical"),
               o == Qt::Horizontal ? glyphs::FlipH : glyphs::FlipV, [&](DocumentState &s) {
                   Layer &l = s.layers[size_t(s.activeLayer)];
                   l.surface = remapSurface(l.surface, flipOp(o));
               });
}

void rotateLayer180(Document &doc)
{
    structural(doc, QStringLiteral("Rotate Layer 180°"), glyphs::RotateRight, [&](DocumentState &s) {
        Layer &l = s.layers[size_t(s.activeLayer)];
        l.surface = remapSurface(l.surface, Remap::Rot180);
    });
}

void cropToSelection(Document &doc)
{
    if (doc.selection().isEmpty()) return;
    QRect r = doc.selection().bounds();
    if (r.isEmpty()) return;
    structural(doc, QStringLiteral("Crop to Selection"), glyphs::Crop, [&](DocumentState &s) {
        // Pixels outside a non-rectangular selection become transparent.
        QPainterPath clip = s.selection.path().translated(-r.topLeft());
        bool rectangular = s.selection.path().boundingRect() == QRectF(r) &&
                           s.selection.path().elementCount() <= 5;
        for (Layer &l : s.layers) {
            QImage img = l.surface.copyRect(r);
            if (!rectangular) {
                QImage masked(img.size(), QImage::Format_ARGB32);
                masked.fill(0);
                QPainter p(&masked);
                p.setRenderHint(QPainter::Antialiasing, s.selection.antialias());
                p.setClipPath(clip);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(0, 0, img);
                p.end();
                img = masked.convertToFormat(QImage::Format_ARGB32);
            }
            l.surface = Surface::fromImage(img);
        }
        s.size = r.size();
        s.selection.setImageSize(r.size());
        s.selection.transform(QTransform::fromTranslate(-r.left(), -r.top()));
        if (rectangular) s.selection.clear();
    });
}

void setSelection(Document &doc, const Selection &sel, const QString &name, const QString &glyph)
{
    Selection before = doc.selection();
    doc.setSelection(sel);
    doc.history().push(std::make_unique<SelectionHistoryItem>(name, glyph, std::move(before), doc.selection()));
}

void selectAll(Document &doc)
{
    Selection s = doc.selection();
    QPainterPath p;
    p.addRect(QRectF(doc.bounds()));
    s.setPath(p, false);
    setSelection(doc, s, QStringLiteral("Select All"), glyphs::SelectAll);
}

void deselect(Document &doc)
{
    if (doc.selection().isEmpty()) return;
    Selection s = doc.selection();
    s.clear();
    setSelection(doc, s, QStringLiteral("Deselect"), glyphs::Deselect);
}

void invertSelection(Document &doc)
{
    Selection s = doc.selection();
    if (s.isEmpty()) return;
    s.invert();
    setSelection(doc, s, QStringLiteral("Invert Selection"), glyphs::InvertSelection);
}

void eraseSelection(Document &doc)
{
    CoverageStroke stroke(doc, doc.activeLayerIndex(), 0xFF000000, PaintMode::Erase);
    stroke.addRect(doc.selection().boundsOrImage());
    doc.notifyPixelsChanged(stroke.takeDirty());
    if (auto item = stroke.finish(QStringLiteral("Erase Selection"), glyphs::Eraser)) doc.history().push(std::move(item));
}

void fillSelection(Document &doc, Pixel color)
{
    CoverageStroke stroke(doc, doc.activeLayerIndex(), color, PaintMode::Overwrite);
    stroke.addRect(doc.selection().boundsOrImage());
    doc.notifyPixelsChanged(stroke.takeDirty());
    if (auto item = stroke.finish(QStringLiteral("Fill Selection"), glyphs::Fill)) doc.history().push(std::move(item));
}

} // namespace pane::commands
