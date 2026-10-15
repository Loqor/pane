#pragma once

#include "core/LayerEdit.h"
#include "core/Paint.h"
#include "tools/Tool.h"
#include "tools/Transform.h"

#include <QElapsedTimer>
#include <optional>

namespace pane {

// Floating pixels for Move Selected Pixels and Paste.
class MovePixelsSession {
public:
    // Lifts the selected pixels of `layer` (erasing them unless leaveCopy).
    static std::unique_ptr<MovePixelsSession> lift(Document &doc, int layer, bool leaveCopy);
    // Floats `image` (straight alpha) at `origin` without touching the layer below.
    static std::unique_ptr<MovePixelsSession> paste(Document &doc, int layer, const QImage &image, QPoint origin,
                                                    const QString &historyName);

    BoxTransform &transform() { return m_transform; }
    void render(bool smooth);
    void finish();
    void cancel();

private:
    MovePixelsSession(Document &doc, int layer) : m_doc(doc), m_edit(doc, layer) {}
    Document &m_doc;
    LayerEdit m_edit;
    Surface m_base;
    QImage m_floating; // premultiplied
    QPoint m_origin;
    QPainterPath m_path;
    Selection m_selBefore;
    BoxTransform m_transform;
    QRect m_lastBounds;
    QString m_name;
};

// Rectangle and Ellipse Select: drag a box, then adjust it with handles.
class BoxSelectTool : public Tool {
    Q_OBJECT
public:
    QChar shortcut() const override { return 'S'; }
    QList<ToolOption> options() const override { return {ToolOption::SelectionMode, ToolOption::Antialias}; }

    void deactivate() override;
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return m_dragging || m_handle >= 0; }

protected:
    virtual QPainterPath shapePath(const QRectF &r) const = 0;
    virtual bool antialiased() const = 0;

private:
    void apply();
    bool rectStillApplies() const;
    bool m_dragging = false, m_moved = false;
    int m_handle = -1;
    QPointF m_start;
    QRectF m_rect;
    bool m_hasRect = false;
    CombineMode m_mode = CombineMode::Replace;
    Selection m_base, m_before;
    QPainterPath m_applied;
};

class RectangleSelectTool final : public BoxSelectTool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("RectangleSelect"); }
    QString name() const override { return QStringLiteral("Rectangle Select"); }
    QString glyph() const override;
    QString hint() const override { return QStringLiteral("Click and drag to draw a rectangular selection. Hold Shift to constrain to a square."); }
    QList<ToolOption> options() const override { return {ToolOption::SelectionMode}; }

protected:
    QPainterPath shapePath(const QRectF &r) const override;
    bool antialiased() const override { return false; }
};

class EllipseSelectTool final : public BoxSelectTool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("EllipseSelect"); }
    QString name() const override { return QStringLiteral("Ellipse Select"); }
    QString glyph() const override;
    QString hint() const override { return QStringLiteral("Click and drag to draw an elliptical selection. Hold Shift to constrain to a circle."); }

protected:
    QPainterPath shapePath(const QRectF &r) const override;
    bool antialiased() const override { return AppState::instance().settings().antialias; }
};

class LassoSelectTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("LassoSelect"); }
    QString name() const override { return QStringLiteral("Lasso Select"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'S'; }
    QString hint() const override
    {
        return QStringLiteral("Freeform: drag to draw a selection outline. Polygon: click to add points; double-click or Enter to finish.");
    }
    QList<ToolOption> options() const override { return {ToolOption::SelectionMode, ToolOption::LassoMode, ToolOption::Antialias}; }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    bool keyPress(QKeyEvent *e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    void commit() override;
    void deactivate() override;
    bool isBusy() const override { return m_active; }

private:
    void apply();
    bool m_active = false, m_dragging = false;
    QPolygonF m_points;
    QPointF m_hover;
    CombineMode m_mode = CombineMode::Replace;
    Selection m_base, m_before;
    QElapsedTimer m_lastClick;
};

class MagicWandTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("MagicWand"); }
    QString name() const override { return QStringLiteral("Magic Wand"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'S'; }
    QString hint() const override { return QStringLiteral("Click to select a region of similar color."); }
    QList<ToolOption> options() const override
    {
        return {ToolOption::SelectionMode, ToolOption::FloodMode, ToolOption::Tolerance, ToolOption::Sampling};
    }
    void pointerPress(const PointerEvent &e) override;
};

// Shared implementation of Move Selection / Move Selected Pixels.
class MoveToolBase : public Tool {
    Q_OBJECT
public:
    QChar shortcut() const override { return 'M'; }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return m_drag != Drag::None; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }

protected:
    enum class Drag { None, Move, Handle, Rotate };
    virtual bool beginSession(const PointerEvent &e) = 0; // ensure a session exists
    virtual void updateSession() = 0;                     // apply m_transform
    virtual void endDrag() = 0;
    virtual bool hasSession() const = 0;
    virtual BoxTransform *currentTransform() = 0;

    Drag m_drag = Drag::None;
    int m_handle = -1;
    QPointF m_last, m_rotStart;
    double m_rotBase = 0;
};

class MoveSelectionTool final : public MoveToolBase {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("MoveSelection"); }
    QString name() const override { return QStringLiteral("Move Selection"); }
    QString glyph() const override;
    QString hint() const override { return QStringLiteral("Drag the selection outline to move it. Drag a handle to scale; right-drag to rotate."); }
    void deactivate() override;

protected:
    bool beginSession(const PointerEvent &e) override;
    void updateSession() override;
    void endDrag() override;
    bool hasSession() const override;
    BoxTransform *currentTransform() override { return hasSession() ? &m_transform : nullptr; }

private:
    BoxTransform m_transform;
    QPainterPath m_origPath, m_applied;
    Selection m_before;
    bool m_active = false;
};

class MoveSelectedPixelsTool final : public MoveToolBase {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("MoveSelectedPixels"); }
    QString name() const override { return QStringLiteral("Move Selected Pixels"); }
    QString glyph() const override;
    QString hint() const override { return QStringLiteral("Drag to move the selected pixels. Ctrl+drag leaves a copy behind. Drag a handle to scale; right-drag to rotate."); }
    QList<ToolOption> options() const override { return {ToolOption::MoveQuality}; }
    void commit() override;
    bool keyPress(QKeyEvent *e) override;
    // Starts a floating paste (used by Edit → Paste).
    void beginPaste(const QImage &img, QPoint origin, const QString &historyName);

protected:
    bool beginSession(const PointerEvent &e) override;
    void updateSession() override;
    void endDrag() override;
    bool hasSession() const override { return bool(m_session); }
    BoxTransform *currentTransform() override { return m_session ? &m_session->transform() : nullptr; }

private:
    std::unique_ptr<MovePixelsSession> m_session;
};

class ZoomTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Zoom"); }
    QString name() const override { return QStringLiteral("Zoom"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'Z'; }
    QString hint() const override { return QStringLiteral("Click to zoom in, right-click to zoom out, or drag a rectangle to zoom to it."); }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    QCursor cursor() const override;
    bool isBusy() const override { return m_dragging; }

private:
    bool m_dragging = false;
    QPointF m_start, m_cur;
    Qt::MouseButton m_button = Qt::NoButton;
};

class PanTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Pan"); }
    QString name() const override { return QStringLiteral("Pan"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'H'; }
    QString hint() const override { return QStringLiteral("Click and drag to move the view. Hold Space with any tool to pan."); }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    QCursor cursor() const override { return m_dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
    bool isBusy() const override { return m_dragging; }

private:
    bool m_dragging = false;
    QPointF m_last;
};

class BrushTool : public Tool {
    Q_OBJECT
public:
    QList<ToolOption> options() const override
    {
        return {ToolOption::BrushWidth, ToolOption::Hardness, ToolOption::Antialias, ToolOption::AlphaBlending};
    }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    void hover(const PointerEvent &e) override;
    void paintOverlay(QPainter &p, const QTransform &t) override;
    bool isBusy() const override { return bool(m_stroke); }
    void deactivate() override;

protected:
    virtual bool erasing() const { return false; }

private:
    double radiusFor(double pressure) const;
    std::unique_ptr<CoverageStroke> m_stroke;
    QPointF m_last, m_hover;
    double m_lastStrength = 1.0;
    bool m_hoverValid = false;
};

class PaintbrushTool final : public BrushTool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Paintbrush"); }
    QString name() const override { return QStringLiteral("Paintbrush"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'B'; }
    QString hint() const override { return QStringLiteral("Left click to draw with the primary color, right click to draw with the secondary color."); }
};

class EraserTool final : public BrushTool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Eraser"); }
    QString name() const override { return QStringLiteral("Eraser"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'E'; }
    QString hint() const override { return QStringLiteral("Click and drag to erase a portion of the image."); }
    QList<ToolOption> options() const override { return {ToolOption::BrushWidth, ToolOption::Hardness, ToolOption::Antialias}; }

protected:
    bool erasing() const override { return true; }
};

class PencilTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("Pencil"); }
    QString name() const override { return QStringLiteral("Pencil"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'P'; }
    QString hint() const override { return QStringLiteral("Left click to draw freeform one-pixel wide lines with the primary color, right click to use the secondary color."); }
    QList<ToolOption> options() const override { return {ToolOption::AlphaBlending}; }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    bool isBusy() const override { return bool(m_stroke); }
    void deactivate() override;

private:
    std::unique_ptr<CoverageStroke> m_stroke;
    QPoint m_last;
};

class PaintBucketTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("PaintBucket"); }
    QString name() const override { return QStringLiteral("Paint Bucket"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'F'; }
    QString hint() const override { return QStringLiteral("Left click to fill a region with the primary color, right click to fill with the secondary color."); }
    QList<ToolOption> options() const override
    {
        return {ToolOption::FloodMode, ToolOption::Tolerance, ToolOption::Sampling, ToolOption::AlphaBlending};
    }
    void pointerPress(const PointerEvent &e) override;
};

class ColorPickerTool final : public Tool {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("ColorPicker"); }
    QString name() const override { return QStringLiteral("Color Picker"); }
    QString glyph() const override;
    QChar shortcut() const override { return 'K'; }
    QString hint() const override { return QStringLiteral("Left click to set the primary color, right click to set the secondary color."); }
    QList<ToolOption> options() const override { return {ToolOption::Sampling, ToolOption::PickerSampling, ToolOption::PickerAfter}; }
    void pointerPress(const PointerEvent &e) override;
    void pointerMove(const PointerEvent &e) override;
    void pointerRelease(const PointerEvent &e) override;
    bool isBusy() const override { return m_button != Qt::NoButton; }

private:
    void pick(const PointerEvent &e);
    Qt::MouseButton m_button = Qt::NoButton;
};

// Tools from later milestones: listed (like paint.net) but disabled.
class PlaceholderTool final : public Tool {
    Q_OBJECT
public:
    PlaceholderTool(QString id, QString name, QString glyph, QChar key)
        : m_id(std::move(id)), m_name(std::move(name)), m_glyph(std::move(glyph)), m_key(key) {}
    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString glyph() const override { return m_glyph; }
    QChar shortcut() const override { return m_key; }
    QString hint() const override { return QStringLiteral("%1 is not available yet.").arg(m_name); }
    bool enabled() const override { return false; }

private:
    QString m_id, m_name, m_glyph;
    QChar m_key;
};

} // namespace pane
