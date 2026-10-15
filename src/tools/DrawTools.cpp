#include "tools/DrawTools.h"

#include "core/Glyphs.h"
#include "tools/Gradient.h"

#include <QPainterPathStroker>
#include <cmath>
#include <numbers>

namespace pane {

namespace {

PaintMode blendSetting() { return AppState::instance().settings().alphaBlending ? PaintMode::Blend : PaintMode::Overwrite; }

// Draws `fn` into a premultiplied image covering `bounds` (image coords) and
// hands it to the session.
template <typename F> void renderPainted(EditSession &s, QRectF bounds, bool antialias, F &&fn)
{
    QRect r = bounds.toAlignedRect().adjusted(-2, -2, 2, 2).intersected(s.document().bounds());
    if (r.isEmpty()) return s.clear();
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(0);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.translate(-r.left(), -r.top());
        fn(p);
    }
    s.renderOverlay(img, r.topLeft(), blendSetting());
}

void paintNub(QPainter &p, QPointF c, const QColor &accent, bool square = false)
{
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(Qt::white);
    if (square) p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    else p.drawEllipse(c, 5, 5);
    p.setPen(QPen(accent, 1.5));
    p.setBrush(Qt::NoBrush);
    if (square) p.drawRect(QRectF(c.x() - 2.5, c.y() - 2.5, 5, 5));
    else p.drawEllipse(c, 3, 3);
}

QPointF constrainAngle(QPointF from, QPointF to)
{
    QLineF l(from, to);
    l.setAngle(std::round(l.angle() / 15.0) * 15.0);
    return l.p2();
}

} // namespace

QStringList shapeTypeNames()
{
    return {QStringLiteral("Rectangle"), QStringLiteral("Rounded Rectangle"), QStringLiteral("Ellipse"),
            QStringLiteral("Diamond"),   QStringLiteral("Triangle"),          QStringLiteral("Right Triangle"),
            QStringLiteral("Pentagon"),  QStringLiteral("Hexagon"),           QStringLiteral("Octagon"),
            QStringLiteral("Star"),      QStringLiteral("Arrow"),             QStringLiteral("Heart")};
}

Qt::PenStyle dashPenStyle(int s)
{
    switch (s) {
    case 1: return Qt::DashLine;
    case 2: return Qt::DotLine;
    case 3: return Qt::DashDotLine;
    case 4: return Qt::DashDotDotLine;
    }
    return Qt::SolidLine;
}

QPainterPath shapePath(ShapeType type, const QRectF &r)
{
    QPainterPath p;
    auto polygon = [&](int n, double rotation) {
        QPolygonF poly;
        for (int i = 0; i < n; ++i) {
            double a = rotation + 2 * std::numbers::pi * i / n;
            poly << QPointF(r.center().x() + std::cos(a) * r.width() / 2, r.center().y() + std::sin(a) * r.height() / 2);
        }
        p.addPolygon(poly);
        p.closeSubpath();
    };
    switch (type) {
    case ShapeType::Rectangle: p.addRect(r); break;
    case ShapeType::RoundedRectangle: {
        double rad = std::min(r.width(), r.height()) * 0.2;
        p.addRoundedRect(r, rad, rad);
        break;
    }
    case ShapeType::Ellipse: p.addEllipse(r); break;
    case ShapeType::Diamond:
        p.addPolygon(QPolygonF{QPointF(r.center().x(), r.top()), QPointF(r.right(), r.center().y()),
                               QPointF(r.center().x(), r.bottom()), QPointF(r.left(), r.center().y())});
        p.closeSubpath();
        break;
    case ShapeType::Triangle:
        p.addPolygon(QPolygonF{QPointF(r.center().x(), r.top()), r.bottomRight(), r.bottomLeft()});
        p.closeSubpath();
        break;
    case ShapeType::RightTriangle:
        p.addPolygon(QPolygonF{r.topLeft(), r.bottomRight(), r.bottomLeft()});
        p.closeSubpath();
        break;
    case ShapeType::Pentagon: polygon(5, -std::numbers::pi / 2); break;
    case ShapeType::Hexagon: polygon(6, 0); break;
    case ShapeType::Octagon: polygon(8, std::numbers::pi / 8); break;
    case ShapeType::Star: {
        QPolygonF poly;
        for (int i = 0; i < 10; ++i) {
            double a = -std::numbers::pi / 2 + std::numbers::pi * i / 5;
            double k = i % 2 ? 0.382 : 1.0;
            poly << QPointF(r.center().x() + std::cos(a) * r.width() / 2 * k, r.center().y() + std::sin(a) * r.height() / 2 * k);
        }
        p.addPolygon(poly);
        p.closeSubpath();
        break;
    }
    case ShapeType::Arrow: {
        double w = r.width(), h = r.height();
        double head = std::min(w * 0.45, h);
        p.addPolygon(QPolygonF{QPointF(r.left(), r.top() + h * 0.3), QPointF(r.right() - head, r.top() + h * 0.3),
                               QPointF(r.right() - head, r.top()), r.topRight() + QPointF(0, h / 2),
                               QPointF(r.right() - head, r.bottom()), QPointF(r.right() - head, r.top() + h * 0.7),
                               QPointF(r.left(), r.top() + h * 0.7)});
        p.closeSubpath();
        break;
    }
    case ShapeType::Heart: {
        double x = r.left(), y = r.top(), w = r.width(), h = r.height();
        p.moveTo(x + w / 2, y + h * 0.25);
        p.cubicTo(x + w / 2, y, x, y, x, y + h * 0.3);
        p.cubicTo(x, y + h * 0.6, x + w / 2, y + h * 0.8, x + w / 2, y + h);
        p.cubicTo(x + w / 2, y + h * 0.8, x + w, y + h * 0.6, x + w, y + h * 0.3);
        p.cubicTo(x + w, y, x + w / 2, y, x + w / 2, y + h * 0.25);
        p.closeSubpath();
        break;
    }
    }
    return p;
}

// ------------------------------------------------------------ Gradient

QString GradientTool::glyph() const { return glyphs::Gradient; }

GradientTool::GradientTool()
{
    auto rerender = [this] {
        if (m_session && host()) render();
    };
    connect(&app(), &AppState::settingsChanged, this, rerender);
    connect(&app(), &AppState::colorsChanged, this, rerender);
}

void GradientTool::commit()
{
    if (m_session) {
        auto s = std::move(m_session);
        s->finish(name(), glyph());
    }
    m_drag = 0;
    if (host()) host()->updateOverlay();
}

void GradientTool::render()
{
    const ToolSettings &s = app().settings();
    Pixel c0 = app().primary(), c1 = app().secondary();
    if (m_swap) std::swap(c0, c1);
    auto type = GradientType(s.gradientType);
    auto repeat = GradientRepeat(s.gradientRepeat);
    bool transparency = s.gradientTransparency;
    bool blend = s.alphaBlending;
    QPointF a = m_a, b = m_b;
    QRect region = doc().selection().boundsOrImage();
    // Precomputed color / alpha ramps: the per-pixel work is just t and a lookup.
    constexpr int N = 1024;
    auto ramp = std::make_shared<std::array<Pixel, N>>();
    auto alphaRamp = std::make_shared<std::array<uint16_t, N>>();
    for (int i = 0; i < N; ++i) {
        double t = double(i) / (N - 1);
        (*ramp)[size_t(i)] = lerpColorPremul(c0, c1, t);
        (*alphaRamp)[size_t(i)] = uint16_t(std::lround((pxA(c0) + (double(pxA(c1)) - pxA(c0)) * t) * 256.0 / 255.0));
    }
    m_session->render(region, [=](int x, int y, Pixel before) -> Pixel {
        int i = int(gradientT(type, repeat, a, b, QPointF(x + 0.5, y + 0.5)) * (N - 1) + 0.5);
        if (transparency) {
            uint32_t na = (pxA(before) * (*alphaRamp)[size_t(i)] + 128) >> 8;
            return na ? withAlpha(before, na) : 0;
        }
        Pixel c = (*ramp)[size_t(i)];
        return blend ? overPixel(before, c, pxA(c)) : c;
    });
}

int GradientTool::hitNub(QPointF v) const
{
    if (!m_session) return 0;
    QTransform t = host()->imageToView();
    if (QLineF(t.map(m_b), v).length() < 8) return 1;
    if (QLineF(t.map(m_a), v).length() < 8) return 2;
    if (QLineF(t.map((m_a + m_b) / 2), v).length() < 8) return 3;
    return 0;
}

void GradientTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    int nub = hitNub(e.viewPos);
    if (nub && e.button == Qt::LeftButton) {
        m_drag = nub;
        m_last = e.pos;
        return;
    }
    commit();
    m_session = std::make_unique<EditSession>(doc(), doc().activeLayerIndex());
    m_swap = e.button == Qt::RightButton;
    m_a = m_b = m_last = e.pos;
    m_drag = 1;
    render();
}

void GradientTool::pointerMove(const PointerEvent &e)
{
    if (!m_drag) return;
    switch (m_drag) {
    case 1: m_b = e.modifiers & Qt::ShiftModifier ? constrainAngle(m_a, e.pos) : e.pos; break;
    case 2: m_a = e.modifiers & Qt::ShiftModifier ? constrainAngle(m_b, e.pos) : e.pos; break;
    case 3: {
        QPointF d = e.pos - m_last;
        m_a += d;
        m_b += d;
        m_last = e.pos;
        break;
    }
    }
    render();
    host()->updateOverlay();
}

void GradientTool::pointerRelease(const PointerEvent &) { m_drag = 0; }

void GradientTool::hover(const PointerEvent &e)
{
    host()->setToolCursor(hitNub(e.viewPos) ? Qt::SizeAllCursor : Qt::CrossCursor);
}

bool GradientTool::keyPress(QKeyEvent *e)
{
    if (!m_session) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        m_session->cancel();
        m_session.reset();
        host()->updateOverlay();
        return true;
    }
    return false;
}

void GradientTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_session) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPointF a = t.map(m_a), b = t.map(m_b);
    p.setPen(QPen(QColor(0, 0, 0, 140), 3));
    p.drawLine(a, b);
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(a, b);
    paintNub(p, a, host()->accent());
    paintNub(p, b, host()->accent());
    paintNub(p, (a + b) / 2, host()->accent(), true);
    p.restore();
}

// ------------------------------------------------------------ Shapes

QString ShapesTool::glyph() const { return glyphs::Shapes; }

ShapesTool::ShapesTool()
{
    auto rerender = [this] {
        if (m_session && host()) render();
    };
    connect(&app(), &AppState::settingsChanged, this, rerender);
    connect(&app(), &AppState::colorsChanged, this, rerender);
}

void ShapesTool::commit()
{
    if (m_session) {
        auto s = std::move(m_session);
        s->finish(QStringLiteral("Shapes"), glyph());
    }
    m_drag = Drag::None;
    if (host()) host()->updateOverlay();
}

void ShapesTool::render()
{
    if (!m_session) return;
    const ToolSettings &s = app().settings();
    QRectF b = m_box.bounds();
    if (b.width() < 1 && b.height() < 1) return m_session->clear();
    QPainterPath path = m_box.matrix().map(shapePath(ShapeType(s.shapeType), b));
    double pw = std::max(1, s.brushWidth);
    QColor primary = qcolorFromPixel(app().primary()), secondary = qcolorFromPixel(app().secondary());
    int style = s.shapeStyle;
    QRectF bounds = path.boundingRect().adjusted(-pw, -pw, pw, pw);
    renderPainted(*m_session, bounds, s.antialias, [&](QPainter &p) {
        if (style == 1) p.fillPath(path, primary);
        if (style == 2) p.fillPath(path, secondary);
        if (style != 1) {
            QPen pen(primary, pw, dashPenStyle(s.dashStyle), Qt::FlatCap, Qt::MiterJoin);
            p.strokePath(path, pen);
        }
    });
}

void ShapesTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    if (m_session) {
        QPolygonF vc = host()->imageToView().map(m_box.corners());
        int h = handles::hitTest(vc, e.viewPos);
        bool inside = vc.containsPoint(e.viewPos, Qt::OddEvenFill);
        if (h >= 0 && e.button == Qt::LeftButton) {
            m_drag = Drag::Handle;
            m_handle = h;
            return;
        }
        if (inside) {
            m_last = e.pos;
            if (e.button == Qt::RightButton) {
                m_drag = Drag::Rotate;
                m_rotStart = e.pos - m_box.center();
                m_rotBase = m_box.rotation();
            } else {
                m_drag = Drag::Move;
            }
            return;
        }
        commit();
    }
    if (e.button != Qt::LeftButton) return;
    m_session = std::make_unique<EditSession>(doc(), doc().activeLayerIndex());
    m_start = e.pos;
    m_box = BoxTransform(QRectF(e.pos, e.pos));
    m_drag = Drag::Create;
}

void ShapesTool::pointerMove(const PointerEvent &e)
{
    switch (m_drag) {
    case Drag::None: return;
    case Drag::Create: {
        QPointF d = e.pos - m_start;
        if (e.modifiers & Qt::ShiftModifier) {
            double s = std::max(std::abs(d.x()), std::abs(d.y()));
            d = QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
        }
        m_box = BoxTransform(QRectF(m_start, m_start + d).normalized());
        break;
    }
    case Drag::Move:
        m_box.translate(e.pos - m_last);
        m_last = e.pos;
        break;
    case Drag::Handle: m_box.dragHandle(m_handle, e.pos, e.modifiers & Qt::ShiftModifier); break;
    case Drag::Rotate: {
        QPointF v = e.pos - m_box.center();
        double a = std::atan2(v.y(), v.x()) - std::atan2(m_rotStart.y(), m_rotStart.x());
        double deg = m_rotBase + a * 180.0 / std::numbers::pi;
        if (e.modifiers & Qt::ShiftModifier) deg = std::round(deg / 15.0) * 15.0;
        m_box.setRotation(deg);
        break;
    }
    }
    render();
    host()->updateOverlay();
}

void ShapesTool::pointerRelease(const PointerEvent &)
{
    if (m_drag == Drag::Create && m_box.bounds().width() < 1 && m_box.bounds().height() < 1) {
        m_session->cancel();
        m_session.reset();
    }
    m_drag = Drag::None;
    host()->updateOverlay();
}

void ShapesTool::hover(const PointerEvent &e)
{
    Qt::CursorShape c = Qt::CrossCursor;
    if (m_session) {
        QPolygonF vc = host()->imageToView().map(m_box.corners());
        int h = handles::hitTest(vc, e.viewPos);
        if (h >= 0) c = handles::cursorFor(h);
        else if (vc.containsPoint(e.viewPos, Qt::OddEvenFill)) c = Qt::SizeAllCursor;
    }
    host()->setToolCursor(c);
}

bool ShapesTool::keyPress(QKeyEvent *e)
{
    if (!m_session) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        m_session->cancel();
        m_session.reset();
        host()->updateOverlay();
        return true;
    }
    return false;
}

void ShapesTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_session || m_drag == Drag::Create) return;
    QPolygonF vc = t.map(m_box.corners());
    p.save();
    p.setPen(QPen(QColor(0, 0, 0, 120), 1, Qt::DashLine));
    p.drawPolygon(vc);
    p.restore();
    handles::paint(p, vc, host()->accent());
}

// ------------------------------------------------------------ Line / Curve

QString LineCurveTool::glyph() const { return glyphs::Line; }

LineCurveTool::LineCurveTool()
{
    auto rerender = [this] {
        if (m_session && host()) render();
    };
    connect(&app(), &AppState::settingsChanged, this, rerender);
    connect(&app(), &AppState::colorsChanged, this, rerender);
}

QPainterPath LineCurveTool::curvePath(const QPolygonF &pts, bool bezier)
{
    QPainterPath path;
    if (pts.size() < 2) return path;
    path.moveTo(pts[0]);
    if (pts.size() == 2) {
        path.lineTo(pts[1]);
        return path;
    }
    if (bezier && pts.size() == 3) {
        path.quadTo(pts[1], pts[2]);
        return path;
    }
    if (bezier && pts.size() == 4) {
        path.cubicTo(pts[1], pts[2], pts[3]);
        return path;
    }
    // Catmull-Rom spline through all points, as cubic Béziers.
    for (int i = 0; i + 1 < pts.size(); ++i) {
        QPointF p0 = pts[std::max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min<int>(int(pts.size()) - 1, i + 2)];
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    return path;
}

void LineCurveTool::commit()
{
    if (m_session) {
        auto s = std::move(m_session);
        s->finish(QStringLiteral("Line / Curve"), glyph());
    }
    m_points.clear();
    m_dragIndex = -1;
    if (host()) host()->updateOverlay();
}

void LineCurveTool::render()
{
    if (!m_session) return;
    const ToolSettings &s = app().settings();
    QPainterPath path = curvePath(m_points, s.curveBezier);
    double pw = std::max(1, s.brushWidth);
    QColor color = qcolorFromPixel(m_secondary ? app().secondary() : app().primary());
    QRectF bounds = path.boundingRect().adjusted(-pw * 4 - 4, -pw * 4 - 4, pw * 4 + 4, pw * 4 + 4);
    renderPainted(*m_session, bounds, s.antialias, [&](QPainter &p) {
        QPen pen(color, pw, dashPenStyle(s.dashStyle), Qt::FlatCap, Qt::RoundJoin);
        p.strokePath(path, pen);
        auto arrow = [&](double pct) {
            double head = std::max(8.0, pw * 3);
            QPointF tip = path.pointAtPercent(pct);
            double ang = path.angleAtPercent(pct) * std::numbers::pi / 180.0; // degrees, counter-clockwise
            QPointF dir(std::cos(ang), -std::sin(ang));
            if (pct == 0.0) dir = -dir;
            QPointF n(-dir.y(), dir.x());
            QPointF base = tip - dir * head;
            QPainterPath tri;
            tri.addPolygon(QPolygonF{tip, base + n * head * 0.5, base - n * head * 0.5});
            tri.closeSubpath();
            p.fillPath(tri, color);
        };
        if (s.arrowStart) arrow(0.0);
        if (s.arrowEnd) arrow(1.0);
    });
}

void LineCurveTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    QTransform t = host()->imageToView();
    if (m_session) {
        for (int i = 0; i < m_points.size(); ++i)
            if (QLineF(t.map(m_points[i]), e.viewPos).length() < 8) {
                m_dragIndex = i;
                return;
            }
        // Click on the curve inserts a control point (up to 6 points).
        QPainterPath vp = t.map(curvePath(m_points, app().settings().curveBezier));
        QPainterPathStroker st;
        st.setWidth(std::max(10.0, app().settings().brushWidth * host()->zoom()));
        if (m_points.size() < 6 && st.createStroke(vp).contains(e.viewPos)) {
            int best = 1;
            double bestD = 1e18;
            for (int i = 0; i + 1 < m_points.size(); ++i) {
                QLineF seg(m_points[i], m_points[i + 1]);
                QPointF d = seg.p2() - seg.p1();
                double len2 = d.x() * d.x() + d.y() * d.y();
                double u = len2 > 0 ? std::clamp(QPointF::dotProduct(e.pos - seg.p1(), d) / len2, 0.0, 1.0) : 0;
                double dist = QLineF(seg.p1() + d * u, e.pos).length();
                if (dist < bestD) {
                    bestD = dist;
                    best = i + 1;
                }
            }
            m_points.insert(best, e.pos);
            m_dragIndex = best;
            render();
            host()->updateOverlay();
            return;
        }
        commit();
    }
    m_session = std::make_unique<EditSession>(doc(), doc().activeLayerIndex());
    m_secondary = e.button == Qt::RightButton;
    m_points = QPolygonF{e.pos, e.pos};
    m_dragIndex = 1;
}

void LineCurveTool::pointerMove(const PointerEvent &e)
{
    if (m_dragIndex < 0) return;
    QPointF p = e.pos;
    if ((e.modifiers & Qt::ShiftModifier) && m_points.size() == 2) p = constrainAngle(m_points[1 - m_dragIndex], p);
    m_points[m_dragIndex] = p;
    render();
    host()->updateOverlay();
}

void LineCurveTool::pointerRelease(const PointerEvent &)
{
    if (m_dragIndex < 0) return;
    m_dragIndex = -1;
    if (m_points.size() == 2 && QLineF(m_points[0], m_points[1]).length() < 0.5) {
        m_session->cancel();
        m_session.reset();
        m_points.clear();
    }
    host()->updateOverlay();
}

void LineCurveTool::hover(const PointerEvent &e)
{
    Qt::CursorShape c = Qt::CrossCursor;
    if (m_session) {
        QTransform t = host()->imageToView();
        for (QPointF pt : m_points)
            if (QLineF(t.map(pt), e.viewPos).length() < 8) c = Qt::SizeAllCursor;
    }
    host()->setToolCursor(c);
}

bool LineCurveTool::keyPress(QKeyEvent *e)
{
    if (!m_session) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commit();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        m_session->cancel();
        m_session.reset();
        m_points.clear();
        host()->updateOverlay();
        return true;
    }
    return false;
}

void LineCurveTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_session || m_dragIndex >= 0) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (QPointF pt : m_points) paintNub(p, t.map(pt), host()->accent());
    p.restore();
}

} // namespace pane
