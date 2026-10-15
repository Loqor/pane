// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Commands.h"
#include "core/Glyphs.h"
#include "core/MaskPath.h"
#include "tools/Tools.h"

#include <cmath>

namespace pane {

namespace {

CombineMode selectionModeFor(const PointerEvent &e)
{
    bool ctrl = e.modifiers & Qt::ControlModifier, alt = e.modifiers & Qt::AltModifier;
    if (e.button == Qt::RightButton) return alt ? CombineMode::Intersect : CombineMode::Xor;
    if (ctrl) return CombineMode::Union;
    if (alt) return CombineMode::Exclude;
    return AppState::instance().settings().selectionMode;
}

} // namespace

// ------------------------------------------------------------ Lasso

QString LassoSelectTool::glyph() const { return glyphs::Lasso; }

void LassoSelectTool::apply()
{
    QPainterPath p;
    if (m_points.size() >= 3) {
        p.addPolygon(m_points);
        p.closeSubpath();
    }
    bool aa = app().settings().antialias;
    Selection s = m_base;
    if (m_mode == CombineMode::Replace) s.setPath(p, aa);
    else s.combine(p, m_mode, aa);
    doc().setSelection(s);
}

void LassoSelectTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    const bool polygon = app().settings().lassoPolygon;
    if (!m_active) {
        m_before = doc().selection();
        m_mode = selectionModeFor(e);
        m_base = m_mode == CombineMode::Replace ? Selection() : doc().selection();
        m_points = QPolygonF{e.pos};
        m_active = true;
        m_dragging = !polygon;
        m_lastClick.start();
        host()->updateOverlay();
        return;
    }
    if (polygon) {
        QPointF first = host()->imageToView().map(m_points.first());
        bool nearFirst = QLineF(first, e.viewPos).length() < 8 && m_points.size() >= 3;
        bool dbl = m_lastClick.isValid() && m_lastClick.elapsed() < 400 &&
                   QLineF(host()->imageToView().map(m_points.last()), e.viewPos).length() < 6;
        if (nearFirst || dbl) {
            commit();
            return;
        }
        m_points << e.pos;
        m_lastClick.restart();
        if (m_mode == CombineMode::Replace) apply();
        host()->updateOverlay();
    }
}

void LassoSelectTool::pointerMove(const PointerEvent &e)
{
    m_hover = e.pos;
    if (!m_active || !m_dragging) return;
    if (QLineF(m_points.last(), e.pos).length() * host()->zoom() < 1.0) return;
    m_points << e.pos;
    if (m_mode == CombineMode::Replace) apply();
    host()->updateOverlay();
}

void LassoSelectTool::pointerRelease(const PointerEvent &)
{
    if (m_active && m_dragging) commit();
}

void LassoSelectTool::hover(const PointerEvent &e)
{
    m_hover = e.pos;
    if (m_active) host()->updateOverlay();
}

bool LassoSelectTool::keyPress(QKeyEvent *e)
{
    if (!m_active) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        doc().setSelection(m_before);
        m_active = m_dragging = false;
        m_points.clear();
        host()->updateOverlay();
        return true;
    }
    if (e->key() == Qt::Key_Backspace && m_points.size() > 1) {
        m_points.removeLast();
        if (m_mode == CombineMode::Replace) apply();
        host()->updateOverlay();
        return true;
    }
    return false;
}

void LassoSelectTool::commit()
{
    if (!m_active) return;
    m_active = m_dragging = false;
    if (m_points.size() < 3) {
        doc().setSelection(m_before);
        m_points.clear();
        if (host()) host()->updateOverlay();
        return;
    }
    apply();
    m_points.clear();
    doc().history().push(std::make_unique<SelectionHistoryItem>(name(), glyph(), m_before, doc().selection()));
    if (host()) host()->updateOverlay();
}

void LassoSelectTool::deactivate()
{
    commit();
    Tool::deactivate();
}

void LassoSelectTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_active || m_points.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPolygonF v = t.map(m_points);
    if (app().settings().lassoPolygon) v << t.map(m_hover);
    p.setPen(QPen(Qt::white, 1));
    p.drawPolyline(v);
    QPen dash(host()->accent(), 1, Qt::DashLine);
    p.setPen(dash);
    p.drawPolyline(v);
    if (app().settings().lassoPolygon) {
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(Qt::white);
        for (QPointF pt : t.map(m_points)) p.drawEllipse(pt, 3, 3);
    }
    p.restore();
}

// ------------------------------------------------------------ Magic Wand

QString MagicWandTool::glyph() const { return glyphs::MagicWand; }

void MagicWandTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    QPoint seed(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    if (!doc().bounds().contains(seed)) return;
    const ToolSettings &s = app().settings();
    Surface sample = s.sampleImage ? doc().flatten() : doc().activeLayer().surface;
    QRect bounds;
    auto mask = floodMask(sample, seed, s.tolerance / 100.0, s.floodGlobal, &bounds);
    QPainterPath path = maskToPath(mask, doc().width(), bounds);
    CombineMode mode = selectionModeFor(e);
    Selection sel = doc().selection();
    if (mode == CombineMode::Replace) sel.setPath(path, false);
    else sel.combine(path, mode, false);
    commands::setSelection(doc(), sel, name(), glyph());
    host()->updateOverlay();
}

} // namespace pane
