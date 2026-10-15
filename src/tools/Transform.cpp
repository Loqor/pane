#include "tools/Transform.h"

#include <array>
#include <cmath>

namespace pane {

QTransform BoxTransform::matrix() const
{
    QPointF c = m_bounds.center();
    QTransform t;
    t.translate(c.x() + m_offset.x(), c.y() + m_offset.y());
    t.rotate(m_angle);
    t.scale(m_sx, m_sy);
    t.translate(-c.x(), -c.y());
    return t;
}

QPolygonF BoxTransform::corners() const
{
    QTransform t = matrix();
    return QPolygonF{t.map(m_bounds.topLeft()), t.map(m_bounds.topRight()), t.map(m_bounds.bottomRight()),
                     t.map(m_bounds.bottomLeft())};
}

void BoxTransform::dragHandle(int h, QPointF p, bool keepAspect)
{
    static const int sgnX[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    static const int sgnY[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    const double w = std::max(1.0, m_bounds.width()), hgt = std::max(1.0, m_bounds.height());
    QTransform rot;
    rot.rotate(m_angle);
    QTransform inv = rot.inverted();
    QPointF local = inv.map(p - center());

    double halfW = m_sx * w / 2, halfH = m_sy * hgt / 2;
    double left = -halfW, right = halfW, top = -halfH, bottom = halfH;
    int sx = sgnX[h], sy = sgnY[h];
    if (sx > 0) right = std::max(local.x(), left + 1);
    if (sx < 0) left = std::min(local.x(), right - 1);
    if (sy > 0) bottom = std::max(local.y(), top + 1);
    if (sy < 0) top = std::min(local.y(), bottom - 1);

    if (keepAspect && sx != 0 && sy != 0) {
        double ratio = std::max((right - left) / w, (bottom - top) / hgt);
        double nw = ratio * w, nh = ratio * hgt;
        if (sx > 0) right = left + nw; else left = right - nw;
        if (sy > 0) bottom = top + nh; else top = bottom - nh;
    }
    m_sx = (right - left) / w;
    m_sy = (bottom - top) / hgt;
    QPointF shift((left + right) / 2, (top + bottom) / 2);
    m_offset += rot.map(shift);
}

namespace handles {

std::array<QPointF, 8> positions(const QPolygonF &c)
{
    auto mid = [](QPointF a, QPointF b) { return (a + b) / 2; };
    return {c[0], mid(c[0], c[1]), c[1], mid(c[1], c[2]), c[2], mid(c[2], c[3]), c[3], mid(c[3], c[0])};
}

int hitTest(const QPolygonF &c, QPointF v)
{
    if (c.size() < 4) return -1;
    auto pos = positions(c);
    for (int i = 0; i < 8; ++i) {
        QPointF d = pos[size_t(i)] - v;
        if (std::abs(d.x()) <= 6 && std::abs(d.y()) <= 6) return i;
    }
    return -1;
}

void paint(QPainter &p, const QPolygonF &c, const QColor &accent)
{
    if (c.size() < 4) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (QPointF pt : positions(c)) {
        QRectF r(pt.x() - 4, pt.y() - 4, 8, 8);
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.setBrush(Qt::white);
        p.drawEllipse(r);
        p.setPen(QPen(accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(1.5, 1.5, -1.5, -1.5));
    }
    p.restore();
}

Qt::CursorShape cursorFor(int h)
{
    switch (h) {
    case 0: case 4: return Qt::SizeFDiagCursor;
    case 2: case 6: return Qt::SizeBDiagCursor;
    case 1: case 5: return Qt::SizeVerCursor;
    case 3: case 7: return Qt::SizeHorCursor;
    }
    return Qt::ArrowCursor;
}

} // namespace handles
} // namespace pane
