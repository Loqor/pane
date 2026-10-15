// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "tools/Tool.h"
#include "ui/Workspace.h"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QTimer>

namespace pane {

class Ruler;

enum class Units { Pixels, Inches, Centimeters };

class CanvasView : public QAbstractScrollArea, public ToolHost {
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);

    void setWorkspace(Workspace *ws);
    Workspace *workspace() const { return m_ws; }
    void setTool(Tool *tool);
    Tool *tool() const { return m_tool; }

    // Zoom presets from paint.net (percent).
    static const std::vector<double> &zoomPresets();
    void setZoom(double zoom, std::optional<QPointF> imageAnchor = {});
    void zoomToWindow();
    void zoomToSelection();
    void actualSize();
    void stepZoom(int dir, std::optional<QPointF> anchor = {});

    void setPixelGrid(bool on);
    bool pixelGrid() const { return m_pixelGrid; }
    void setRulersVisible(bool on);
    bool rulersVisible() const { return m_rulers; }
    void setUnits(Units u);
    Units units() const { return m_units; }
    // Image point at the top-left of the visible area (for paste placement).
    QPoint visibleImageTopLeft() const;
    QRectF visibleImageRect() const;

    // ToolHost
    Document &document() override { return *m_ws->doc; }
    void updateOverlay() override { viewport()->update(); }
    double zoom() const override { return m_ws ? m_ws->zoom : 1.0; }
    void zoomIn(QPointF anchor) override { stepZoom(1, anchor); }
    void zoomOut(QPointF anchor) override { stepZoom(-1, anchor); }
    void zoomToRect(const QRectF &r) override;
    void panBy(QPointF viewDelta) override;
    QTransform imageToView() const override;
    void setToolCursor(const QCursor &c) override;
    QColor accent() const override;

signals:
    void zoomChanged(double zoom);
    void cursorMoved(QPoint imagePos, bool inside);
    void interactionChanged(bool active);
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int dx, int dy) override;
    bool event(QEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override { mousePressEvent(e); }
    void tabletEvent(QTabletEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void updateScrollBars();
    double marginX() const;
    double marginY() const;
    void centerOn(QPointF imagePoint);
    void rememberCenter();
    QPointF origin() const;
    QPointF viewToImage(QPointF v) const;
    PointerEvent makeEvent(QPointF viewPos, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m, double pressure) const;
    void beginInteraction();
    void endInteraction();
    void layoutRulers();
    void onCompositorUpdated(const QRect &imageRect);

    Workspace *m_ws = nullptr;
    Tool *m_tool = nullptr;
    QMetaObject::Connection m_compConn, m_selConn, m_sizeConn;
    bool m_pixelGrid = false;
    bool m_rulers = false;
    Units m_units = Units::Pixels;
    Ruler *m_hRuler = nullptr, *m_vRuler = nullptr;

    bool m_spaceHeld = false;
    bool m_resizing = false;
    QPointF m_centerImage;
    bool m_panning = false;      // middle-drag or space-drag
    QPointF m_panLast;
    bool m_toolDown = false;
    bool m_tabletDown = false;
    bool m_interacting = false;
    QTimer m_antsTimer;
    int m_antsOffset = 0;
    QCursor m_toolCursor;
};

class Ruler : public QWidget {
    Q_OBJECT
public:
    Ruler(Qt::Orientation o, CanvasView *view);
    void setMarker(double imageCoord) { m_marker = imageCoord; update(); }
    static constexpr int Thickness = 20;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Qt::Orientation m_orient;
    CanvasView *m_view;
    double m_marker = -1;
};

} // namespace pane
