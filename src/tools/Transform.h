#pragma once

#include <QPainter>
#include <array>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

namespace pane {

// Interactive transform of a rectangle: offset, rotation and non-uniform scale
// about the rectangle's center. Used by the selection and move tools.
class BoxTransform {
public:
    BoxTransform() = default;
    explicit BoxTransform(const QRectF &bounds) : m_bounds(bounds) {}

    const QRectF &bounds() const { return m_bounds; }
    QTransform matrix() const;
    QPolygonF corners() const; // TL, TR, BR, BL in image coordinates
    QPointF center() const { return m_bounds.center() + m_offset; }

    void translate(QPointF d) { m_offset += d; }
    void setRotation(double degrees) { m_angle = degrees; }
    double rotation() const { return m_angle; }
    // Drags handle h (0..7: TL, T, TR, R, BR, B, BL, L) to image point p.
    void dragHandle(int h, QPointF p, bool keepAspect);

private:
    QRectF m_bounds;
    QPointF m_offset;
    double m_angle = 0;
    double m_sx = 1, m_sy = 1;
};

namespace handles {
// Handle positions (8) in view coordinates for a transformed box.
std::array<QPointF, 8> positions(const QPolygonF &viewCorners);
// Index of the handle under viewPos, or -1.
int hitTest(const QPolygonF &viewCorners, QPointF viewPos);
void paint(QPainter &p, const QPolygonF &viewCorners, const QColor &accent);
Qt::CursorShape cursorFor(int handle);
} // namespace handles

} // namespace pane
