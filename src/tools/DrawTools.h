#pragma once

#include "tools/EditSession.h"
#include "tools/Tools.h"

#include <QFont>
#include <QTimer>
#include <optional>

namespace pane {

enum class ShapeType {
    Rectangle, RoundedRectangle, Ellipse, Diamond, Triangle, RightTriangle,
    Pentagon, Hexagon, Octagon, Star, Arrow, Heart,
};
QStringList shapeTypeNames();
QPainterPath shapePath(ShapeType type, const QRectF &r);
Qt::PenStyle dashPenStyle(int dashStyle);

class GradientTool final : public Tool {
    Q_OBJECT
public:
    GradientTool();
    QString id() const override { return QStringLiteral("Gradient"); }
    QString name() const override { return QStringLiteral("Gradient"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'G'; }
    QString hint() const override
    {
        return QStringLiteral("Click and drag to draw a gradient from the primary to the secondary color. Right-drag to reverse. Drag the nubs to adjust.");
    }
    QList<ToolOption> options() const override
    {
        return {ToolOption::GradientType, ToolOption::GradientMode, ToolOption::GradientRepeat, ToolOption::AlphaBlending};
    }
    void commit() override;
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    bool keyPress(QKeyEvent *e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return m_drag != 0; }

private:
    void render();
    int hitNub(QPointF view) const;
    std::unique_ptr<EditSession> m_session;
    QPointF m_a, m_b, m_last;
    bool m_swap = false;
    int m_drag = 0; // 0 none, 1 new/end, 2 start, 3 move
};

class ShapesTool final : public Tool {
    Q_OBJECT
public:
    ShapesTool();
    QString id() const override { return QStringLiteral("Shapes"); }
    QString name() const override { return QStringLiteral("Shapes"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'O'; }
    QString hint() const override
    {
        return QStringLiteral("Click and drag to draw a shape. Drag handles to resize, drag inside to move, right-drag to rotate. Enter finishes.");
    }
    QList<ToolOption> options() const override
    {
        return {ToolOption::ShapeType, ToolOption::ShapeStyle, ToolOption::BrushWidth, ToolOption::DashStyle,
                ToolOption::Antialias, ToolOption::AlphaBlending};
    }
    void commit() override;
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    bool keyPress(QKeyEvent *e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return m_drag != Drag::None; }

private:
    enum class Drag { None, Create, Move, Handle, Rotate };
    void render();
    std::unique_ptr<EditSession> m_session;
    BoxTransform m_box;
    QPointF m_start, m_last, m_rotStart;
    double m_rotBase = 0;
    int m_handle = -1;
    Drag m_drag = Drag::None;
};

class LineCurveTool final : public Tool {
    Q_OBJECT
public:
    LineCurveTool();
    QString id() const override { return QStringLiteral("LineCurve"); }
    QString name() const override { return QStringLiteral("Line / Curve"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'O'; }
    QString hint() const override
    {
        return QStringLiteral("Click and drag to draw a line. Drag on the line to add a control point and bend it. Enter finishes.");
    }
    QList<ToolOption> options() const override
    {
        return {ToolOption::BrushWidth, ToolOption::DashStyle, ToolOption::CurveType, ToolOption::Arrows,
                ToolOption::Antialias, ToolOption::AlphaBlending};
    }
    void commit() override;
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    bool keyPress(QKeyEvent *e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return m_dragIndex >= 0; }

    // The curve through/along the control points (exposed for tests).
    static QPainterPath curvePath(const QPolygonF &pts, bool bezier);

private:
    void render();
    std::unique_ptr<EditSession> m_session;
    QPolygonF m_points;
    int m_dragIndex = -1;
    bool m_secondary = false;
};

class TextTool final : public Tool {
    Q_OBJECT
public:
    TextTool();
    QString id() const override { return QStringLiteral("Text"); }
    QString name() const override { return QStringLiteral("Text"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'T'; }
    QString hint() const override { return QStringLiteral("Click to place the cursor, then type. Click elsewhere or press Escape to finish."); }
    QList<ToolOption> options() const override
    {
        return {ToolOption::Font, ToolOption::TextStyle, ToolOption::TextAlign, ToolOption::Antialias, ToolOption::AlphaBlending};
    }
    void commit() override;
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    bool keyPress(QKeyEvent *e) override;
    void inputText(const QString &text) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool wantsKeyboard() const override { return bool(m_session); }
    QCursor cursor() const override { return Qt::IBeamCursor; }

private:
    QFont font() const;
    QRectF textBounds() const;
    QRectF moveNub() const;
    void render();
    void insert(const QString &s);
    std::unique_ptr<EditSession> m_session;
    QPointF m_origin;
    QStringList m_lines;
    int m_line = 0, m_col = 0;
    bool m_moving = false;
    QPointF m_last;
    QTimer m_blink;
    bool m_caretOn = true;
};

class CloneStampTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("CloneStamp"); }
    QString name() const override { return QStringLiteral("Clone Stamp"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'L'; }
    QString hint() const override { return QStringLiteral("Ctrl+click to set the source, then click and drag to paint a copy of it."); }
    QList<ToolOption> options() const override { return {ToolOption::BrushWidth, ToolOption::Hardness, ToolOption::Antialias}; }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    void deactivate() override;
    bool isBusy() const override { return bool(m_stroke); }

private:
    std::unique_ptr<CoverageStroke> m_stroke;
    std::optional<QPointF> m_source;
    QPointF m_offset, m_last, m_hover;
    bool m_offsetSet = false;
};

class RecolorTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Recolor"); }
    QString name() const override { return QStringLiteral("Recolor"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'R'; }
    QString hint() const override
    {
        return QStringLiteral("Left click to replace the secondary color with the primary color; right click for the reverse.");
    }
    QList<ToolOption> options() const override
    {
        return {ToolOption::BrushWidth, ToolOption::Hardness, ToolOption::Tolerance, ToolOption::Antialias};
    }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    void deactivate() override;
    bool isBusy() const override { return bool(m_stroke); }

    // paint.net's recolor: keep the pixel's difference from the basis color.
    static Pixel recolorPixel(Pixel before, Pixel basis, Pixel replacement);

private:
    std::unique_ptr<CoverageStroke> m_stroke;
    QPointF m_last, m_hover;
    bool m_hoverValid = false;
};

} // namespace pane
