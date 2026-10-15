#include "core/Commands.h"
#include "core/Glyphs.h"
#include "core/Parallel.h"
#include "tools/Tools.h"

#include <QPainterPath>
#include <cmath>

namespace pane {

namespace {

CombineMode modeFor(const PointerEvent &e)
{
    bool ctrl = e.modifiers & Qt::ControlModifier, alt = e.modifiers & Qt::AltModifier;
    if (e.button == Qt::RightButton) return alt ? CombineMode::Intersect : CombineMode::Xor;
    if (ctrl) return CombineMode::Union;
    if (alt) return CombineMode::Exclude;
    return AppState::instance().settings().selectionMode;
}

QPolygonF toView(const QTransform &t, const QPolygonF &p) { return t.map(p); }

} // namespace

// ------------------------------------------------------------ MovePixelsSession

std::unique_ptr<MovePixelsSession> MovePixelsSession::lift(Document &doc, int layer, bool leaveCopy)
{
    std::unique_ptr<MovePixelsSession> s(new MovePixelsSession(doc, layer));
    const Selection &sel = doc.selection();
    QRect bounds = sel.boundsOrImage();
    s->m_selBefore = sel;
    s->m_name = QStringLiteral("Move Selected Pixels");
    if (sel.isEmpty()) {
        s->m_path.addRect(QRectF(bounds));
    } else {
        s->m_path = sel.path();
    }
    s->m_origin = bounds.topLeft();
    s->m_transform = BoxTransform(QRectF(bounds));
    s->m_lastBounds = bounds;

    // Copy out the selected pixels, masked by selection coverage, and erase the source.
    Surface &surf = doc.layer(layer).surface;
    QImage floating = surf.copyRect(bounds);
    for (int idx : surf.tilesIn(bounds)) {
        TileCoverage cov = sel.coverage(idx);
        QRect tr = surf.tileRect(idx);
        QRect part = tr.intersected(bounds);
        for (int y = part.top(); y <= part.bottom(); ++y) {
            auto *row = reinterpret_cast<Pixel *>(floating.scanLine(y - bounds.top()));
            for (int x = part.left(); x <= part.right(); ++x) {
                uint32_t c = cov.kind == TileCoverage::Full ? 255
                           : cov.kind == TileCoverage::None ? 0
                                                            : cov.mask[(y - tr.top()) * TileSize + (x - tr.left())];
                Pixel &p = row[x - bounds.left()];
                p = c == 255 ? p : withAlpha(p, mul255(pxA(p), c));
            }
        }
        if (leaveCopy || cov.kind == TileCoverage::None) continue;
        Tile *t = s->m_edit.writeTile(idx);
        for (int y = part.top(); y <= part.bottom(); ++y)
            for (int x = part.left(); x <= part.right(); ++x) {
                Pixel &p = t->px[(y - tr.top()) * TileSize + (x - tr.left())];
                uint32_t c = cov.kind == TileCoverage::Full ? 255 : cov.mask[(y - tr.top()) * TileSize + (x - tr.left())];
                uint32_t a = mul255(pxA(p), 255 - c);
                p = a ? withAlpha(p, a) : 0;
            }
    }
    s->m_floating = floating.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s->m_base = surf; // shares tiles
    return s;
}

std::unique_ptr<MovePixelsSession> MovePixelsSession::paste(Document &doc, int layer, const QImage &image, QPoint origin,
                                                            const QString &historyName)
{
    std::unique_ptr<MovePixelsSession> s(new MovePixelsSession(doc, layer));
    s->m_selBefore = doc.selection();
    s->m_name = historyName;
    QRect bounds(origin, image.size());
    s->m_path.addRect(QRectF(bounds));
    s->m_origin = origin;
    s->m_transform = BoxTransform(QRectF(bounds));
    s->m_lastBounds = bounds.intersected(doc.bounds());
    s->m_floating = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s->m_base = doc.layer(layer).surface;
    s->render(true);
    return s;
}

void MovePixelsSession::render(bool smooth)
{
    QTransform t = m_transform.matrix();
    // Pure moves snap to whole pixels (as paint.net does): exact and much faster.
    const bool translateOnly = t.type() <= QTransform::TxTranslate;
    if (translateOnly) t = QTransform::fromTranslate(std::round(t.dx()), std::round(t.dy()));
    QRect nb = t.mapRect(QRectF(QPointF(m_origin), QSizeF(m_floating.size()))).toAlignedRect().intersected(m_doc.bounds());
    Surface &surf = m_edit.surface();
    QRect dirty = nb | m_lastBounds;
    for (int idx : surf.tilesIn(dirty)) m_edit.setTile(idx, m_base.sharedTile(idx));
    if (!nb.isEmpty()) {
        // Snapshot + detach every destination tile serially, then composite tiles in parallel.
        const std::vector<int> tiles = surf.tilesIn(nb);
        std::vector<Tile *> dsts(tiles.size());
        for (size_t i = 0; i < tiles.size(); ++i) dsts[i] = m_edit.writeTile(tiles[i]);
        const QPoint off(int(t.dx()) + m_origin.x(), int(t.dy()) + m_origin.y());
        const QImage &fl = m_floating;
        parallelFor(int(tiles.size()), [&](int i) {
            QRect tr = surf.tileRect(tiles[size_t(i)]);
            QRect part = tr.intersected(nb);
            Tile *tile = dsts[size_t(i)];
            if (translateOnly) {
                for (int y = part.top(); y <= part.bottom(); ++y) {
                    int fy = y - off.y();
                    if (fy < 0 || fy >= fl.height()) continue;
                    auto *src = reinterpret_cast<const Pixel *>(fl.constScanLine(fy));
                    for (int x = part.left(); x <= part.right(); ++x) {
                        int fx = x - off.x();
                        if (fx < 0 || fx >= fl.width()) continue;
                        Pixel s = src[fx];
                        if (!pxA(s)) continue;
                        Pixel &d = tile->px[(y - tr.top()) * TileSize + (x - tr.left())];
                        d = overPixel(d, unpremultiply(s), pxA(s));
                    }
                }
                return;
            }
            QImage over(part.size(), QImage::Format_ARGB32_Premultiplied);
            over.fill(0);
            {
                QPainter p(&over);
                p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
                p.setRenderHint(QPainter::Antialiasing, smooth);
                p.setTransform(t * QTransform::fromTranslate(-part.left(), -part.top()));
                p.drawImage(m_origin, fl);
            }
            for (int y = part.top(); y <= part.bottom(); ++y) {
                auto *src = reinterpret_cast<const Pixel *>(over.constScanLine(y - part.top()));
                for (int x = part.left(); x <= part.right(); ++x) {
                    Pixel s = src[x - part.left()];
                    if (!pxA(s)) continue;
                    Pixel &d = tile->px[(y - tr.top()) * TileSize + (x - tr.left())];
                    d = overPixel(d, unpremultiply(s), pxA(s));
                }
            }
        });
    }
    m_lastBounds = nb;
    Selection sel = m_doc.selection();
    sel.setPath(t.map(m_path), m_selBefore.isEmpty() ? true : m_selBefore.antialias());
    m_doc.setSelection(sel);
    m_doc.notifyPixelsChanged(dirty);
}

void MovePixelsSession::finish()
{
    if (auto item = m_edit.finish(m_name, glyphs::MoveSelected, m_selBefore, m_doc.selection()))
        m_doc.history().push(std::move(item));
}

void MovePixelsSession::cancel()
{
    m_edit.cancel();
    m_doc.setSelection(m_selBefore);
}

// ------------------------------------------------------------ Rectangle Select

QString RectangleSelectTool::glyph() const { return glyphs::RectSelect; }
QString EllipseSelectTool::glyph() const { return glyphs::EllipseSelect; }

QPainterPath RectangleSelectTool::shapePath(const QRectF &r) const
{
    QPainterPath p;
    p.addRect(r);
    return p;
}

QPainterPath EllipseSelectTool::shapePath(const QRectF &r) const
{
    QPainterPath p;
    p.addEllipse(r);
    return p;
}

void BoxSelectTool::deactivate()
{
    m_hasRect = false;
    Tool::deactivate();
}

bool BoxSelectTool::rectStillApplies() const { return m_hasRect && doc().selection().path() == m_applied; }

void BoxSelectTool::apply()
{
    QPainterPath p;
    if (!m_rect.isEmpty()) p = shapePath(m_rect);
    Selection s = m_base;
    if (m_mode == CombineMode::Replace) s.setPath(p, antialiased());
    else s.combine(p, m_mode, antialiased());
    doc().setSelection(s);
    m_applied = doc().selection().path();
    host()->updateOverlay();
}

void BoxSelectTool::pointerPress(const PointerEvent &e)
{
    if (rectStillApplies()) {
        QPolygonF vc = toView(host()->imageToView(), QPolygonF(m_rect));
        vc.removeLast(); // QPolygonF(QRectF) is closed; keep 4 corners
        int h = handles::hitTest(vc, e.viewPos);
        if (h >= 0) {
            m_handle = h;
            m_before = doc().selection();
            return;
        }
    }
    m_before = doc().selection();
    m_mode = modeFor(e);
    m_base = m_mode == CombineMode::Replace ? Selection() : doc().selection();
    m_start = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    m_dragging = true;
    m_moved = false;
    m_hasRect = false;
}

void BoxSelectTool::pointerMove(const PointerEvent &e)
{
    QPointF p(std::round(e.pos.x()), std::round(e.pos.y()));
    if (m_handle >= 0) {
        static const int sx[8] = {-1, 0, 1, 1, 1, 0, -1, -1}, sy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
        double l = m_rect.left(), r = m_rect.right(), t = m_rect.top(), b = m_rect.bottom();
        if (sx[m_handle] < 0) l = p.x();
        if (sx[m_handle] > 0) r = p.x();
        if (sy[m_handle] < 0) t = p.y();
        if (sy[m_handle] > 0) b = p.y();
        m_rect = QRectF(QPointF(l, t), QPointF(r, b)).normalized();
        apply();
        return;
    }
    if (!m_dragging) return;
    QPointF d = p - m_start;
    if (e.modifiers & Qt::ShiftModifier) {
        double s = std::max(std::abs(d.x()), std::abs(d.y()));
        d = QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
    }
    if (d.x() == 0 && d.y() == 0 && !m_moved) return;
    m_moved = true;
    m_rect = QRectF(m_start, m_start + d).normalized();
    apply();
}

void BoxSelectTool::pointerRelease(const PointerEvent &)
{
    if (m_handle >= 0) {
        m_handle = -1;
        doc().history().push(std::make_unique<SelectionHistoryItem>(name(), glyph(),
                                                                    m_before, doc().selection()));
        return;
    }
    if (!m_dragging) return;
    m_dragging = false;
    if (!m_moved) {
        // A click without dragging deselects.
        if (!m_before.isEmpty()) {
            doc().setSelection(m_before);
            commands::deselect(doc());
        }
        host()->updateOverlay();
        return;
    }
    Selection after = doc().selection();
    doc().history().push(std::make_unique<SelectionHistoryItem>(name(), glyph(),
                                                                m_before, after));
    m_hasRect = !m_rect.isEmpty();
}

void BoxSelectTool::hover(const PointerEvent &e)
{
    Qt::CursorShape shape = Qt::CrossCursor;
    if (rectStillApplies()) {
        QPolygonF vc = toView(host()->imageToView(), QPolygonF(m_rect));
        vc.removeLast();
        int h = handles::hitTest(vc, e.viewPos);
        if (h >= 0) shape = handles::cursorFor(h);
    }
    host()->setToolCursor(shape);
}

void BoxSelectTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!rectStillApplies() || m_dragging) return;
    QPolygonF vc = toView(t, QPolygonF(m_rect));
    vc.removeLast();
    handles::paint(p, vc, host()->accent());
}

// ------------------------------------------------------------ Move tools (shared)

void MoveToolBase::pointerPress(const PointerEvent &e)
{
    if (!beginSession(e)) return;
    BoxTransform *bt = currentTransform();
    if (!bt) return;
    m_last = e.pos;
    QPolygonF vc = toView(host()->imageToView(), bt->corners());
    int h = handles::hitTest(vc, e.viewPos);
    if (e.button == Qt::RightButton) {
        m_drag = Drag::Rotate;
        QPointF c = bt->center();
        m_rotStart = e.pos - c;
        m_rotBase = bt->rotation();
    } else if (h >= 0) {
        m_drag = Drag::Handle;
        m_handle = h;
    } else {
        m_drag = Drag::Move;
    }
}

void MoveToolBase::pointerMove(const PointerEvent &e)
{
    BoxTransform *bt = currentTransform();
    if (m_drag == Drag::None || !bt) return;
    switch (m_drag) {
    case Drag::Move: {
        QPointF d = e.pos - m_last;
        bt->translate(d);
        m_last = e.pos;
        break;
    }
    case Drag::Handle: bt->dragHandle(m_handle, e.pos, e.modifiers & Qt::ShiftModifier); break;
    case Drag::Rotate: {
        QPointF v = e.pos - bt->center();
        double a = std::atan2(v.y(), v.x()) - std::atan2(m_rotStart.y(), m_rotStart.x());
        double deg = m_rotBase + a * 180.0 / M_PI;
        if (e.modifiers & Qt::ShiftModifier) deg = std::round(deg / 15.0) * 15.0;
        bt->setRotation(deg);
        break;
    }
    case Drag::None: break;
    }
    updateSession();
    host()->updateOverlay();
}

void MoveToolBase::pointerRelease(const PointerEvent &)
{
    if (m_drag == Drag::None) return;
    m_drag = Drag::None;
    m_handle = -1;
    endDrag();
    host()->updateOverlay();
}

void MoveToolBase::hover(const PointerEvent &e)
{
    BoxTransform *bt = currentTransform();
    Qt::CursorShape shape = Qt::SizeAllCursor;
    if (bt) {
        int h = handles::hitTest(toView(host()->imageToView(), bt->corners()), e.viewPos);
        if (h >= 0) shape = handles::cursorFor(h);
    }
    host()->setToolCursor(shape);
}

void MoveToolBase::paintOverlay(QPainter &p, const QTransform &t)
{
    BoxTransform *bt = currentTransform();
    if (!bt) {
        if (doc().selection().isEmpty()) return;
        // Show handles around the current selection even before the first drag.
        QRectF b = doc().selection().path().boundingRect();
        QPolygonF vc = t.map(QPolygonF(b));
        vc.removeLast();
        handles::paint(p, vc, host()->accent());
        return;
    }
    QPolygonF vc = toView(t, bt->corners());
    p.save();
    p.setPen(QPen(QColor(0, 0, 0, 120), 1, Qt::DashLine));
    p.drawPolygon(vc);
    p.restore();
    handles::paint(p, vc, host()->accent());
}

// ------------------------------------------------------------ Move Selection

QString MoveSelectionTool::glyph() const { return glyphs::MoveSelection; }

void MoveSelectionTool::deactivate()
{
    m_active = false;
    Tool::deactivate();
}

bool MoveSelectionTool::hasSession() const { return m_active && doc().selection().path() == m_applied; }

bool MoveSelectionTool::beginSession(const PointerEvent &)
{
    if (doc().selection().isEmpty()) return false;
    if (!hasSession()) {
        m_origPath = doc().selection().path();
        m_transform = BoxTransform(m_origPath.boundingRect());
        m_applied = m_origPath;
        m_active = true;
    }
    m_before = doc().selection();
    return true;
}

void MoveSelectionTool::updateSession()
{
    Selection s = doc().selection();
    s.setPath(m_transform.matrix().map(m_origPath), s.antialias());
    doc().setSelection(s);
    m_applied = doc().selection().path();
}

void MoveSelectionTool::endDrag()
{
    doc().history().push(std::make_unique<SelectionHistoryItem>(QStringLiteral("Move Selection"), glyphs::MoveSelection,
                                                                m_before, doc().selection()));
    m_applied = doc().selection().path();
}

// ------------------------------------------------------------ Move Selected Pixels

QString MoveSelectedPixelsTool::glyph() const { return glyphs::MoveSelected; }

bool MoveSelectedPixelsTool::beginSession(const PointerEvent &e)
{
    if (!m_session) m_session = MovePixelsSession::lift(doc(), doc().activeLayerIndex(), e.modifiers & Qt::ControlModifier);
    return true;
}

void MoveSelectedPixelsTool::updateSession()
{
    if (m_session) m_session->render(app().settings().moveSmooth);
}

void MoveSelectedPixelsTool::endDrag() {}

void MoveSelectedPixelsTool::commit()
{
    if (!m_session) return;
    auto s = std::move(m_session);
    s->finish();
    if (host()) host()->updateOverlay();
}

bool MoveSelectedPixelsTool::keyPress(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (e->key() == Qt::Key_Escape && m_session) {
        m_session->cancel();
        m_session.reset();
        host()->updateOverlay();
        return true;
    }
    return false;
}

void MoveSelectedPixelsTool::beginPaste(const QImage &img, QPoint origin, const QString &historyName)
{
    commit();
    m_session = MovePixelsSession::paste(doc(), doc().activeLayerIndex(), img, origin, historyName);
    host()->updateOverlay();
}

} // namespace pane
