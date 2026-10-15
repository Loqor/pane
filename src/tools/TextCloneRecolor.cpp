// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Glyphs.h"
#include "tools/DrawTools.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <cmath>

namespace pane {

// ------------------------------------------------------------ Text

QString TextTool::glyph() const { return glyphs::Text; }

TextTool::TextTool()
{
    m_blink.setInterval(530);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        if (host()) host()->updateOverlay();
    });
    auto rerender = [this] {
        if (m_session && host()) render();
    };
    connect(&app(), &AppState::settingsChanged, this, rerender);
    connect(&app(), &AppState::colorsChanged, this, rerender);
}

QFont TextTool::font() const
{
    const ToolSettings &s = app().settings();
    QFont f = s.fontFamily.isEmpty() ? QGuiApplication::font() : QFont(s.fontFamily);
    // paint.net sizes text in points at 96 dpi.
    f.setPixelSize(std::max(1, int(std::lround(s.fontSize * 96.0 / 72.0))));
    f.setBold(s.bold);
    f.setItalic(s.italic);
    f.setUnderline(s.underline);
    f.setStrikeOut(s.strikeout);
    f.setStyleStrategy(s.antialias ? QFont::PreferAntialias : QFont::NoAntialias);
    return f;
}

QRectF TextTool::textBounds() const
{
    QFontMetricsF fm(font());
    double w = 0;
    for (const QString &l : m_lines) w = std::max(w, fm.horizontalAdvance(l));
    return QRectF(m_origin, QSizeF(std::max(w, 1.0), fm.lineSpacing() * std::max<qsizetype>(1, m_lines.size())));
}

QRectF TextTool::moveNub() const
{
    QRectF b = textBounds();
    double s = 8 / (host() ? host()->zoom() : 1.0);
    return QRectF(b.right() + s * 0.5, b.bottom() + s * 0.5, s, s);
}

void TextTool::render()
{
    if (!m_session) return;
    QFont f = font();
    QFontMetricsF fm(f);
    QRectF b = textBounds();
    const int align = app().settings().textAlign;
    QColor color = qcolorFromPixel(app().primary());
    bool aa = app().settings().antialias;
    QRect r = b.toAlignedRect().adjusted(-4, -4, 4 + int(fm.maxWidth()), 4).intersected(doc().bounds());
    if (r.isEmpty() || (m_lines.size() == 1 && m_lines[0].isEmpty())) return m_session->clear();
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(0);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, aa);
        p.setRenderHint(QPainter::Antialiasing, aa);
        p.translate(-r.left(), -r.top());
        p.setFont(f);
        p.setPen(color);
        for (int i = 0; i < m_lines.size(); ++i) {
            double lw = fm.horizontalAdvance(m_lines[i]);
            double x = b.left() + (align == 1 ? (b.width() - lw) / 2 : align == 2 ? b.width() - lw : 0);
            p.drawText(QPointF(x, b.top() + fm.ascent() + i * fm.lineSpacing()), m_lines[i]);
        }
    }
    m_session->renderOverlay(img, r.topLeft(), app().settings().alphaBlending ? PaintMode::Blend : PaintMode::Overwrite);
}

void TextTool::commit()
{
    m_blink.stop();
    if (m_session) {
        auto s = std::move(m_session);
        bool empty = std::all_of(m_lines.begin(), m_lines.end(), [](const QString &l) { return l.isEmpty(); });
        if (empty) s->cancel();
        else s->finish(name(), glyph());
    }
    m_lines.clear();
    if (host()) host()->updateOverlay();
}

void TextTool::insert(const QString &text)
{
    if (!m_session) return;
    QStringList parts = QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split('\n');
    QString tail = m_lines[m_line].mid(m_col);
    m_lines[m_line] = m_lines[m_line].left(m_col) + parts[0];
    for (int i = 1; i < parts.size(); ++i) m_lines.insert(++m_line, parts[i]);
    m_col = int(m_lines[m_line].size());
    m_lines[m_line] += tail;
    m_caretOn = true;
    render();
    host()->updateOverlay();
}

void TextTool::inputText(const QString &text) { insert(text); }

void TextTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton) return;
    if (m_session) {
        if (moveNub().adjusted(-3, -3, 3, 3).contains(e.pos)) {
            m_moving = true;
            m_last = e.pos;
            return;
        }
        QRectF b = textBounds();
        if (b.contains(e.pos)) {
            // Place the caret.
            QFontMetricsF fm(font());
            m_line = std::clamp(int((e.pos.y() - b.top()) / fm.lineSpacing()), 0, int(m_lines.size()) - 1);
            const QString &l = m_lines[m_line];
            double lw = fm.horizontalAdvance(l);
            int align = app().settings().textAlign;
            double x0 = b.left() + (align == 1 ? (b.width() - lw) / 2 : align == 2 ? b.width() - lw : 0);
            m_col = int(l.size());
            for (int i = 0; i <= l.size(); ++i)
                if (x0 + fm.horizontalAdvance(l.left(i)) >= e.pos.x() - 1) {
                    m_col = i;
                    break;
                }
            m_caretOn = true;
            host()->updateOverlay();
            return;
        }
        commit();
    }
    m_session = std::make_unique<EditSession>(doc(), doc().activeLayerIndex());
    QFontMetricsF fm(font());
    m_origin = e.pos - QPointF(0, fm.ascent() / 2);
    m_lines = QStringList{QString()};
    m_line = m_col = 0;
    m_caretOn = true;
    m_blink.start();
    host()->updateOverlay();
}

void TextTool::pointerMove(const PointerEvent &e)
{
    if (!m_moving) return;
    m_origin += e.pos - m_last;
    m_last = e.pos;
    render();
    host()->updateOverlay();
}

void TextTool::pointerRelease(const PointerEvent &) { m_moving = false; }

void TextTool::hover(const PointerEvent &e)
{
    host()->setToolCursor(m_session && moveNub().adjusted(-3, -3, 3, 3).contains(e.pos) ? Qt::SizeAllCursor : Qt::IBeamCursor);
}

bool TextTool::keyPress(QKeyEvent *e)
{
    if (!m_session) return false;
    QString &line = m_lines[m_line];
    switch (e->key()) {
    case Qt::Key_Escape: commit(); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: insert(QStringLiteral("\n")); return true;
    case Qt::Key_Backspace:
        if (m_col > 0) line.remove(--m_col, 1);
        else if (m_line > 0) {
            m_col = int(m_lines[m_line - 1].size());
            m_lines[m_line - 1] += line;
            m_lines.removeAt(m_line--);
        }
        break;
    case Qt::Key_Delete:
        if (m_col < line.size()) line.remove(m_col, 1);
        else if (m_line + 1 < m_lines.size()) {
            line += m_lines[m_line + 1];
            m_lines.removeAt(m_line + 1);
        }
        break;
    case Qt::Key_Left:
        if (m_col > 0) --m_col;
        else if (m_line > 0) m_col = int(m_lines[--m_line].size());
        break;
    case Qt::Key_Right:
        if (m_col < line.size()) ++m_col;
        else if (m_line + 1 < m_lines.size()) { ++m_line; m_col = 0; }
        break;
    case Qt::Key_Up:
        if (m_line > 0) m_col = std::min(m_col, int(m_lines[--m_line].size()));
        break;
    case Qt::Key_Down:
        if (m_line + 1 < m_lines.size()) m_col = std::min(m_col, int(m_lines[++m_line].size()));
        break;
    case Qt::Key_Home: m_col = 0; break;
    case Qt::Key_End: m_col = int(line.size()); break;
    default:
        if (e->matches(QKeySequence::Paste)) {
            insert(QGuiApplication::clipboard()->text());
            return true;
        }
        if (!e->text().isEmpty() && e->text()[0].isPrint() && !(e->modifiers() & Qt::ControlModifier)) {
            insert(e->text());
            return true;
        }
        return e->modifiers() & Qt::ControlModifier ? false : true;
    }
    m_caretOn = true;
    render();
    host()->updateOverlay();
    return true;
}

void TextTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_session) return;
    QRectF b = textBounds();
    p.save();
    p.setPen(QPen(QColor(128, 128, 128, 180), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(t.mapRect(b));
    if (m_caretOn) {
        QFontMetricsF fm(font());
        const QString &l = m_lines[m_line];
        double lw = fm.horizontalAdvance(l);
        int align = app().settings().textAlign;
        double x = b.left() + (align == 1 ? (b.width() - lw) / 2 : align == 2 ? b.width() - lw : 0) + fm.horizontalAdvance(l.left(m_col));
        double y = b.top() + m_line * fm.lineSpacing();
        p.setPen(QPen(host()->accent(), 2));
        p.drawLine(t.map(QPointF(x, y)), t.map(QPointF(x, y + fm.lineSpacing())));
    }
    QRectF nub = t.mapRect(moveNub());
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(Qt::white);
    p.drawRect(nub);
    p.restore();
}

// ------------------------------------------------------------ Clone Stamp

QString CloneStampTool::glyph() const { return glyphs::CloneStamp; }

void CloneStampTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton) return;
    if (e.modifiers & Qt::ControlModifier) {
        m_source = e.pos;
        m_offsetSet = false;
        host()->updateOverlay();
        return;
    }
    if (!m_source) {
        emit app().hintChanged(glyph(), QStringLiteral("Clone Stamp: Ctrl+click to set the source first."));
        return;
    }
    if (!m_offsetSet) {
        m_offset = *m_source - e.pos;
        m_offsetSet = true;
    }
    const ToolSettings &s = app().settings();
    Surface snapshot = doc().activeLayer().surface; // shares tiles; sampling sees the layer as of stroke start
    QPointF off = m_offset;
    m_stroke = std::make_unique<CoverageStroke>(doc(), doc().activeLayerIndex(), 0, PaintMode::Blend);
    m_stroke->setColorFunction([snapshot = std::move(snapshot), off](int x, int y, Pixel) {
        return snapshot.pixel(int(std::floor(x + off.x())), int(std::floor(y + off.y())));
    });
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addDab(e.pos, d.radius, s.hardness / 100.0, s.antialias, d.strength);
    m_last = e.pos;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
}

void CloneStampTool::pointerMove(const PointerEvent &e)
{
    m_hover = e.pos;
    if (!m_stroke) return host()->updateOverlay();
    const ToolSettings &s = app().settings();
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addLine(m_last, e.pos, d.radius, s.hardness / 100.0, s.antialias, std::max(0.5, d.radius * 0.25), d.strength, d.strength);
    m_last = e.pos;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
    host()->updateOverlay();
}

void CloneStampTool::pointerRelease(const PointerEvent &)
{
    if (!m_stroke) return;
    auto stroke = std::move(m_stroke);
    if (auto item = stroke->finish(name(), glyph())) doc().history().push(std::move(item));
}

void CloneStampTool::deactivate()
{
    if (m_stroke) pointerRelease({});
    Tool::deactivate();
}

void CloneStampTool::hover(const PointerEvent &e)
{
    m_hover = e.pos;
    host()->updateOverlay();
}

void CloneStampTool::paintOverlay(QPainter &p, const QTransform &t)
{
    double vr = app().settings().brushWidth / 2.0 * host()->zoom();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    auto cross = [&](QPointF c) {
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
        p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
        p.setPen(QPen(host()->accent(), 1));
        p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
        p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
        if (vr > 3) p.drawEllipse(c, vr, vr);
    };
    if (m_source) cross(t.map(m_offsetSet ? m_hover + m_offset : *m_source));
    if (vr > 3) {
        p.setPen(QPen(QColor(0, 0, 0, 180), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(t.map(m_hover), vr, vr);
    }
    p.restore();
}

// ------------------------------------------------------------ Recolor

QString RecolorTool::glyph() const { return glyphs::Recolor; }

Pixel RecolorTool::recolorPixel(Pixel before, Pixel basis, Pixel repl)
{
    auto ch = [](int b, int k, int r) { return uint32_t(std::clamp(r + (b - k), 0, 255)); };
    return makePixel(ch(pxR(before), pxR(basis), pxR(repl)), ch(pxG(before), pxG(basis), pxG(repl)),
                     ch(pxB(before), pxB(basis), pxB(repl)), pxA(before));
}

void RecolorTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    const ToolSettings &s = app().settings();
    Pixel basis = app().secondary(), repl = app().primary();
    if (e.button == Qt::RightButton) std::swap(basis, repl);
    double tol = s.tolerance / 100.0;
    m_stroke = std::make_unique<CoverageStroke>(doc(), doc().activeLayerIndex(), 0, PaintMode::Overwrite);
    m_stroke->setColorFunction([basis, repl, tol](int, int, Pixel b) {
        return colorsSimilar(basis, b, tol) ? recolorPixel(b, basis, repl) : b;
    });
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addDab(e.pos, d.radius, s.hardness / 100.0, s.antialias, d.strength);
    m_last = e.pos;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
}

void RecolorTool::pointerMove(const PointerEvent &e)
{
    m_hover = e.pos;
    m_hoverValid = true;
    if (!m_stroke) return host()->updateOverlay();
    const ToolSettings &s = app().settings();
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addLine(m_last, e.pos, d.radius, s.hardness / 100.0, s.antialias, std::max(0.5, d.radius * 0.25), d.strength, d.strength);
    m_last = e.pos;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
    host()->updateOverlay();
}

void RecolorTool::pointerRelease(const PointerEvent &)
{
    if (!m_stroke) return;
    auto stroke = std::move(m_stroke);
    if (auto item = stroke->finish(name(), glyph())) doc().history().push(std::move(item));
}

void RecolorTool::deactivate()
{
    if (m_stroke) pointerRelease({});
    m_hoverValid = false;
    Tool::deactivate();
}

void RecolorTool::hover(const PointerEvent &e)
{
    m_hover = e.pos;
    m_hoverValid = true;
    host()->updateOverlay();
}

void RecolorTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_hoverValid) return;
    double vr = app().settings().brushWidth / 2.0 * host()->zoom();
    if (vr < 2) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 200), 1));
    p.drawEllipse(t.map(m_hover), vr + 0.5, vr + 0.5);
    p.setPen(QPen(QColor(0, 0, 0, 200), 1));
    p.drawEllipse(t.map(m_hover), vr - 0.5, vr - 0.5);
    p.restore();
}

} // namespace pane
