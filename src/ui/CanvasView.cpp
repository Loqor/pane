#include "ui/CanvasView.h"

#include "ui/theme/Theme.h"

#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTabletEvent>
#include <QWheelEvent>
#include <cmath>

namespace pane {

namespace {
constexpr double Pad = 32.0; // logical pixels around a scrolled image
constexpr double MinZoom = 0.01, MaxZoom = 32.0;
constexpr double KeepVisible = 64.0; // free panning stops when this much image remains on screen
} // namespace

const std::vector<double> &CanvasView::zoomPresets()
{
    static const std::vector<double> z = {0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.12, 0.16, 0.25, 1.0 / 3, 0.5,
                                          2.0 / 3, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0,
                                          20.0, 24.0, 32.0};
    return z;
}

CanvasView::CanvasView(QWidget *parent) : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAttribute(Qt::WA_TabletTracking);
    viewport()->setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    m_hRuler = new Ruler(Qt::Horizontal, this);
    m_vRuler = new Ruler(Qt::Vertical, this);
    m_hRuler->hide();
    m_vRuler->hide();
    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsOffset = (m_antsOffset + 1) % 8;
        viewport()->update();
    });
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, viewport(), qOverload<>(&QWidget::update));
}

void CanvasView::setWorkspace(Workspace *ws)
{
    if (m_tool && m_ws) m_tool->deactivate();
    disconnect(m_compConn);
    disconnect(m_selConn);
    disconnect(m_sizeConn);
    if (m_ws) m_ws->scroll = QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    m_ws = ws;
    if (m_ws) {
        m_compConn = connect(m_ws->compositor.get(), &Compositor::updated, this, &CanvasView::onCompositorUpdated);
        m_selConn = connect(m_ws->doc.get(), &Document::selectionChanged, this, [this] {
            if (m_ws->doc->selection().isEmpty()) m_antsTimer.stop();
            else if (!m_antsTimer.isActive()) m_antsTimer.start();
            viewport()->update();
        });
        m_sizeConn = connect(m_ws->doc.get(), &Document::sizeChanged, this, [this] {
            updateScrollBars();
            centerOn(QPointF(m_ws->doc->width() / 2.0, m_ws->doc->height() / 2.0));
            viewport()->update();
        });
        m_ws->compositor->update();
        if (!m_ws->viewInitialized) {
            m_ws->viewInitialized = true;
            updateScrollBars();
            // Open at 100% if it fits, otherwise zoom to window (like paint.net).
            QSizeF vs = QSizeF(viewport()->size()) * devicePixelRatioF();
            if (m_ws->doc->width() > vs.width() - 2 * Pad || m_ws->doc->height() > vs.height() - 2 * Pad) zoomToWindow();
            else {
                setZoom(1.0);
                centerOn(QPointF(m_ws->doc->width() / 2.0, m_ws->doc->height() / 2.0));
            }
        } else {
            updateScrollBars();
            horizontalScrollBar()->setValue(int(m_ws->scroll.x()));
            verticalScrollBar()->setValue(int(m_ws->scroll.y()));
            rememberCenter();
        }
        if (m_ws->doc->selection().isEmpty()) m_antsTimer.stop();
        else m_antsTimer.start();
        if (m_tool) m_tool->activate(*this);
        emit zoomChanged(m_ws->zoom);
    }
    viewport()->update();
    m_hRuler->update();
    m_vRuler->update();
}

void CanvasView::setTool(Tool *tool)
{
    if (m_tool == tool) return;
    if (m_tool && m_ws) m_tool->deactivate();
    m_tool = tool;
    if (m_tool && m_ws) m_tool->activate(*this);
    viewport()->update();
}

QColor CanvasView::accent() const { return ThemeEngine::instance().current().accent; }

void CanvasView::setToolCursor(const QCursor &c)
{
    m_toolCursor = c;
    if (!m_panning && !m_spaceHeld) viewport()->setCursor(c);
}

// --------------------------------------------------------------- geometry

static double viewScale(const CanvasView *v, double zoom) { return zoom / v->devicePixelRatioF(); }

QPointF CanvasView::origin() const
{
    if (!m_ws) return {};
    // Free panning: the scroll range covers the image plus most of the viewport on each side.
    double ox = marginX() - horizontalScrollBar()->value();
    double oy = marginY() - verticalScrollBar()->value();
    // Align to device pixels so nearest-neighbor zoom stays crisp.
    double dpr = devicePixelRatioF();
    return QPointF(std::round(ox * dpr) / dpr, std::round(oy * dpr) / dpr);
}

QTransform CanvasView::imageToView() const
{
    if (!m_ws) return {};
    double s = viewScale(this, m_ws->zoom);
    QPointF o = origin();
    return QTransform(s, 0, 0, s, o.x(), o.y());
}

QPointF CanvasView::viewToImage(QPointF v) const
{
    double s = viewScale(this, m_ws->zoom);
    return (v - origin()) / s;
}

QRectF CanvasView::visibleImageRect() const
{
    if (!m_ws) return {};
    return imageToView().inverted().mapRect(QRectF(viewport()->rect())).intersected(QRectF(m_ws->doc->bounds()));
}

QPoint CanvasView::visibleImageTopLeft() const
{
    QRectF r = visibleImageRect();
    return QPoint(int(std::ceil(r.left())), int(std::ceil(r.top())));
}

void CanvasView::updateScrollBars()
{
    if (!m_ws) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        return;
    }
    double s = viewScale(this, m_ws->zoom);
    double cw = m_ws->doc->width() * s, ch = m_ws->doc->height() * s;
    int vw = viewport()->width(), vh = viewport()->height();
    horizontalScrollBar()->setRange(0, std::max(0, int(std::ceil(cw + 2 * marginX() - vw))));
    verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(ch + 2 * marginY() - vh))));
    horizontalScrollBar()->setPageStep(vw);
    verticalScrollBar()->setPageStep(vh);
    horizontalScrollBar()->setSingleStep(32);
    verticalScrollBar()->setSingleStep(32);
}

void CanvasView::setZoom(double zoom, std::optional<QPointF> anchor)
{
    if (!m_ws) return;
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    QPointF a = anchor ? *anchor : viewToImage(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    QPointF anchorView = imageToView().map(a);
    m_ws->zoom = zoom;
    updateScrollBars();
    double s = viewScale(this, zoom);
    // Choose scroll values so `a` stays under anchorView.
    horizontalScrollBar()->setValue(int(std::round(marginX() - (anchorView.x() - a.x() * s))));
    verticalScrollBar()->setValue(int(std::round(marginY() - (anchorView.y() - a.y() * s))));
    rememberCenter();
    viewport()->update();
    m_hRuler->update();
    m_vRuler->update();
    emit zoomChanged(zoom);
}

void CanvasView::stepZoom(int dir, std::optional<QPointF> anchor)
{
    if (!m_ws) return;
    const auto &p = zoomPresets();
    double z = m_ws->zoom;
    double next = z;
    if (dir > 0) {
        for (double v : p)
            if (v > z * 1.001) { next = v; break; }
    } else {
        for (auto it = p.rbegin(); it != p.rend(); ++it)
            if (*it < z * 0.999) { next = *it; break; }
    }
    setZoom(next, anchor);
}

void CanvasView::zoomToRect(const QRectF &r)
{
    if (!m_ws || r.isEmpty()) return;
    double dpr = devicePixelRatioF();
    double z = std::min((viewport()->width() - 2 * Pad) * dpr / r.width(), (viewport()->height() - 2 * Pad) * dpr / r.height());
    setZoom(z, r.center());
    centerOn(r.center());
}

void CanvasView::zoomToWindow()
{
    if (!m_ws) return;
    double dpr = devicePixelRatioF();
    double z = std::min((viewport()->width() - 2 * Pad) * dpr / m_ws->doc->width(),
                        (viewport()->height() - 2 * Pad) * dpr / m_ws->doc->height());
    QPointF c(m_ws->doc->width() / 2.0, m_ws->doc->height() / 2.0);
    setZoom(std::max(MinZoom, z), c);
    centerOn(c);
}

double CanvasView::marginX() const { return std::max(Pad, viewport()->width() - KeepVisible); }
double CanvasView::marginY() const { return std::max(Pad, viewport()->height() - KeepVisible); }

void CanvasView::centerOn(QPointF imagePoint)
{
    if (!m_ws) return;
    double s = viewScale(this, m_ws->zoom);
    horizontalScrollBar()->setValue(int(std::round(marginX() - (viewport()->width() / 2.0 - imagePoint.x() * s))));
    verticalScrollBar()->setValue(int(std::round(marginY() - (viewport()->height() / 2.0 - imagePoint.y() * s))));
    rememberCenter();
}

void CanvasView::rememberCenter()
{
    if (m_ws && !m_resizing) m_centerImage = viewToImage(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

void CanvasView::zoomToSelection()
{
    if (!m_ws || m_ws->doc->selection().isEmpty()) return;
    zoomToRect(m_ws->doc->selection().path().boundingRect());
}

void CanvasView::actualSize() { setZoom(1.0); }

void CanvasView::panBy(QPointF d)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(std::round(d.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - int(std::round(d.y())));
}

void CanvasView::setPixelGrid(bool on)
{
    m_pixelGrid = on;
    viewport()->update();
}

void CanvasView::setRulersVisible(bool on)
{
    m_rulers = on;
    m_hRuler->setVisible(on);
    m_vRuler->setVisible(on);
    setViewportMargins(on ? Ruler::Thickness : 0, on ? Ruler::Thickness : 0, 0, 0);
    layoutRulers();
}

void CanvasView::setUnits(Units u)
{
    m_units = u;
    m_hRuler->update();
    m_vRuler->update();
}

void CanvasView::layoutRulers()
{
    QRect vp = viewport()->geometry();
    m_hRuler->setGeometry(vp.left(), vp.top() - Ruler::Thickness, vp.width(), Ruler::Thickness);
    m_vRuler->setGeometry(vp.left() - Ruler::Thickness, vp.top(), Ruler::Thickness, vp.height());
}

void CanvasView::resizeEvent(QResizeEvent *e)
{
    // Keep the same image point in the middle of the view.
    m_resizing = true;
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
    m_resizing = false;
    if (m_ws) centerOn(m_centerImage);
    layoutRulers();
}

void CanvasView::scrollContentsBy(int, int)
{
    rememberCenter();
    viewport()->update();
    m_hRuler->update();
    m_vRuler->update();
}

void CanvasView::onCompositorUpdated(const QRect &r)
{
    viewport()->update(imageToView().mapRect(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

// --------------------------------------------------------------- painting

void CanvasView::paintEvent(QPaintEvent *ev)
{
    QPainter p(viewport());
    const Theme &th = ThemeEngine::instance().current();
    p.fillRect(ev->rect(), th.canvasBackdrop());
    if (!m_ws) return;
    Document &doc = *m_ws->doc;
    Compositor &comp = *m_ws->compositor;
    if (comp.hasPendingWork()) comp.update();

    const QTransform t = imageToView();
    const double s = t.m11();
    const QRectF imgView = t.mapRect(QRectF(doc.bounds()));

    // Drop shadow.
    for (int i = 1; i <= 4; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 22 - i * 4));
        p.drawRect(imgView.adjusted(-i, -i, i, i));
    }

    // Checkerboard anchored to the image.
    {
        const int cell = 8;
        QPixmap tile(cell * 2, cell * 2);
        tile.fill(th.checkerA());
        QPainter tp(&tile);
        tp.fillRect(0, 0, cell, cell, th.checkerB());
        tp.fillRect(cell, cell, cell, cell, th.checkerB());
        tp.end();
        QBrush b(tile);
        b.setTransform(QTransform::fromTranslate(imgView.left(), imgView.top()));
        p.fillRect(imgView.intersected(QRectF(ev->rect())), b);
    }

    // Image tiles.
    const QRectF vis = t.inverted().mapRect(QRectF(ev->rect())).intersected(QRectF(doc.bounds()));
    if (!vis.isEmpty()) {
        const double zoomDevice = m_ws->zoom;
        int level = 0;
        if (zoomDevice < 1.0) level = std::clamp(int(std::floor(std::log2(1.0 / zoomDevice))), 0, MipLevels - 1);
        const bool smooth = zoomDevice < 1.0;
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        const int tx0 = int(vis.left()) >> TileShift, tx1 = std::min(comp.tilesX() - 1, int(vis.right()) >> TileShift);
        const int ty0 = int(vis.top()) >> TileShift, ty1 = std::min(comp.tilesY() - 1, int(vis.bottom()) >> TileShift);
        const double f = double(1 << level);
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                const QImage &img = comp.displayTile(ty * comp.tilesX() + tx, level);
                if (img.isNull()) continue;
                QRectF tileRect(tx * TileSize, ty * TileSize, std::min(TileSize, doc.width() - tx * TileSize),
                                std::min(TileSize, doc.height() - ty * TileSize));
                QRectF part = tileRect.intersected(vis);
                // Snap the visible part outward to whole source pixels.
                QRectF src((part.left() - tileRect.left()) / f, (part.top() - tileRect.top()) / f, part.width() / f,
                           part.height() / f);
                src = QRectF(QPointF(std::floor(src.left()), std::floor(src.top())),
                             QPointF(std::min<double>(img.width(), std::ceil(src.right())),
                                     std::min<double>(img.height(), std::ceil(src.bottom()))));
                QRectF imgPart(tileRect.left() + src.left() * f, tileRect.top() + src.top() * f, src.width() * f,
                               src.height() * f);
                imgPart = imgPart.intersected(tileRect);
                p.drawImage(t.mapRect(imgPart), img, src);
            }
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);

        // Pixel grid.
        if (m_pixelGrid && s >= 4) {
            p.setPen(QPen(QColor(128, 128, 128, 140), 0));
            for (int x = int(vis.left()); x <= int(std::ceil(vis.right())); ++x) {
                double vx = t.map(QPointF(x, 0)).x();
                p.drawLine(QPointF(vx, std::max(imgView.top(), 0.0)), QPointF(vx, std::min(imgView.bottom(), double(height()))));
            }
            for (int y = int(vis.top()); y <= int(std::ceil(vis.bottom())); ++y) {
                double vy = t.map(QPointF(0, y)).y();
                p.drawLine(QPointF(std::max(imgView.left(), 0.0), vy), QPointF(std::min(imgView.right(), double(width())), vy));
            }
        }
    }

    // Selection outline (marching ants).
    if (!doc.selection().isEmpty()) {
        QPainterPath vp = t.map(doc.selection().path());
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        QColor tint = th.accent;
        tint.setAlpha(28);
        p.fillPath(vp, tint);
        p.setPen(QPen(Qt::white, 0));
        p.drawPath(vp);
        QPen dash(Qt::black, 0, Qt::CustomDashLine);
        dash.setDashPattern({4, 4});
        dash.setDashOffset(m_antsOffset);
        p.setPen(dash);
        p.drawPath(vp);
    }

    if (m_tool) m_tool->paintOverlay(p, t);
}

// --------------------------------------------------------------- input

PointerEvent CanvasView::makeEvent(QPointF viewPos, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m,
                                   double pressure) const
{
    PointerEvent e;
    e.viewPos = viewPos;
    e.pos = viewToImage(viewPos);
    e.button = b;
    e.buttons = bs;
    e.modifiers = m;
    e.pressure = pressure;
    return e;
}

void CanvasView::beginInteraction()
{
    if (m_interacting) return;
    m_interacting = true;
    emit interactionChanged(true);
}

void CanvasView::endInteraction()
{
    if (!m_interacting) return;
    m_interacting = false;
    emit interactionChanged(false);
}

bool CanvasView::event(QEvent *e)
{
    // Moving to a monitor with a different scale: relayout and keep the same center.
    if (e->type() == QEvent::DevicePixelRatioChange) {
        QPointF c = m_centerImage;
        updateScrollBars();
        if (m_ws) centerOn(c);
        viewport()->update();
    }
    // While a tool is taking keyboard input (Text), keep keys away from shortcuts.
    if (e->type() == QEvent::ShortcutOverride && m_tool && m_tool->wantsKeyboard()) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (!(ke->modifiers() & Qt::ControlModifier) || ke->matches(QKeySequence::Paste)) {
            e->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(e);
}

void CanvasView::inputMethodEvent(QInputMethodEvent *e)
{
    if (m_tool && m_tool->wantsKeyboard() && !e->commitString().isEmpty()) {
        m_tool->inputText(e->commitString());
        viewport()->update();
    }
    e->accept();
}

QVariant CanvasView::inputMethodQuery(Qt::InputMethodQuery q) const
{
    if (q == Qt::ImEnabled) return m_tool && m_tool->wantsKeyboard();
    return QAbstractScrollArea::inputMethodQuery(q);
}

bool CanvasView::viewportEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        tabletEvent(static_cast<QTabletEvent *>(e));
        return true;
    default: break;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

void CanvasView::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    if (!m_ws) return;
    if (e->button() == Qt::MiddleButton || (m_spaceHeld && e->button() == Qt::LeftButton)) {
        m_panning = true;
        m_panLast = e->position();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (!m_tool || !m_tool->enabled() || m_toolDown) return;
    if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton) return;
    m_toolDown = true;
    beginInteraction();
    m_tool->pointerPress(makeEvent(e->position(), e->button(), e->buttons(), e->modifiers(), 1.0));
}

void CanvasView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_ws) return;
    QPointF ip = viewToImage(e->position());
    emit cursorMoved(QPoint(int(std::floor(ip.x())), int(std::floor(ip.y()))), m_ws->doc->bounds().contains(ip.toPoint()));
    if (m_rulers) {
        m_hRuler->setMarker(ip.x());
        m_vRuler->setMarker(ip.y());
    }
    if (m_panning) {
        panBy(e->position() - m_panLast);
        m_panLast = e->position();
        return;
    }
    if (!m_tool || m_tabletDown) return;
    PointerEvent pe = makeEvent(e->position(), Qt::NoButton, e->buttons(), e->modifiers(), 1.0);
    if (m_toolDown) m_tool->pointerMove(pe);
    else m_tool->hover(pe);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(m_spaceHeld ? QCursor(Qt::OpenHandCursor) : m_toolCursor);
        return;
    }
    if (!m_toolDown || !m_tool) return;
    if (e->buttons() & (Qt::LeftButton | Qt::RightButton)) return;
    m_toolDown = false;
    m_tool->pointerRelease(makeEvent(e->position(), e->button(), e->buttons(), e->modifiers(), 1.0));
    endInteraction();
}

void CanvasView::tabletEvent(QTabletEvent *e)
{
    if (!m_ws || !m_tool || m_spaceHeld) {
        e->ignore(); // fall back to synthesized mouse events
        return;
    }
    QPointF pos = viewport()->mapFromGlobal(e->globalPosition());
    double pressure = e->pressure() > 0 ? e->pressure() : 1.0;
    bool eraserEnd = e->pointerType() == QPointingDevice::PointerType::Eraser;
    Qt::MouseButton b = eraserEnd ? Qt::RightButton : e->button();
    switch (e->type()) {
    case QEvent::TabletPress:
        if (m_toolDown || !m_tool->enabled() || (b != Qt::LeftButton && b != Qt::RightButton)) break;
        setFocus(Qt::MouseFocusReason);
        m_toolDown = m_tabletDown = true;
        beginInteraction();
        m_tool->pointerPress(makeEvent(pos, b, e->buttons(), e->modifiers(), pressure));
        break;
    case QEvent::TabletMove: {
        QPointF ip = viewToImage(pos);
        emit cursorMoved(QPoint(int(std::floor(ip.x())), int(std::floor(ip.y()))), m_ws->doc->bounds().contains(ip.toPoint()));
        PointerEvent pe = makeEvent(pos, Qt::NoButton, e->buttons(), e->modifiers(), pressure);
        if (m_tabletDown) m_tool->pointerMove(pe);
        else m_tool->hover(pe);
        break;
    }
    case QEvent::TabletRelease:
        if (!m_tabletDown) break;
        m_toolDown = m_tabletDown = false;
        m_tool->pointerRelease(makeEvent(pos, b, e->buttons(), e->modifiers(), pressure));
        endInteraction();
        break;
    default: break;
    }
    e->accept();
}

void CanvasView::wheelEvent(QWheelEvent *e)
{
    if (!m_ws) return;
    if (e->modifiers() & Qt::ControlModifier) {
        int steps = e->angleDelta().y() / 120;
        if (steps == 0) steps = e->angleDelta().y() > 0 ? 1 : e->angleDelta().y() < 0 ? -1 : 0;
        QPointF anchor = viewToImage(e->position());
        for (int i = 0; i < std::abs(steps); ++i) stepZoom(steps > 0 ? 1 : -1, anchor);
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y() / 2);
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void CanvasView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && !m_toolDown && !(m_tool && m_tool->wantsKeyboard())) {
        m_spaceHeld = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_tool && m_tool->keyPress(e)) {
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceHeld = false;
        if (!m_panning) viewport()->setCursor(m_toolCursor);
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(e);
}

void CanvasView::leaveEvent(QEvent *e)
{
    emit cursorMoved({}, false);
    QAbstractScrollArea::leaveEvent(e);
}

void CanvasView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void CanvasView::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) emit filesDropped(paths);
}

// --------------------------------------------------------------- rulers

Ruler::Ruler(Qt::Orientation o, CanvasView *view) : QWidget(view), m_orient(o), m_view(view) {}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    p.fillRect(rect(), th.mantle);
    p.setPen(th.surface2);
    if (m_orient == Qt::Horizontal) p.drawLine(0, height() - 1, width(), height() - 1);
    else p.drawLine(width() - 1, 0, width() - 1, height());
    Workspace *ws = m_view->workspace();
    if (!ws) return;

    QTransform t = m_view->imageToView();
    double scale = t.m11();
    double off = m_orient == Qt::Horizontal ? t.dx() : t.dy();
    // Units per image pixel.
    double dpi = m_orient == Qt::Horizontal ? ws->doc->dpiX() : ws->doc->dpiY();
    double unitPx = 1.0; // image pixels per unit
    switch (m_view->units()) {
    case Units::Pixels: unitPx = 1.0; break;
    case Units::Inches: unitPx = dpi; break;
    case Units::Centimeters: unitPx = dpi / 2.54; break;
    }
    double pxPerUnit = unitPx * scale; // view pixels per unit
    // Pick a major step (in units) giving ~80 view pixels.
    static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000};
    double major = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * pxPerUnit >= 70) { major = s; break; }
    double minor = major / 10;
    int len = m_orient == Qt::Horizontal ? width() : height();
    double firstUnit = std::floor((-off) / pxPerUnit / major) * major;
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(th.subtext0);
    for (double u = firstUnit; (u * pxPerUnit + off) < len; u += minor) {
        double v = u * pxPerUnit + off;
        if (v < 0) continue;
        long idx = std::lround(u / minor);
        bool isMajor = idx % 10 == 0;
        int tick = isMajor ? Thickness : (idx % 5 == 0 ? Thickness / 2 : Thickness / 4);
        if (m_orient == Qt::Horizontal) {
            p.drawLine(QPointF(v, Thickness - tick), QPointF(v, Thickness));
            if (isMajor) p.drawText(QPointF(v + 2, 10), QString::number(u, 'g', 6));
        } else {
            p.drawLine(QPointF(Thickness - tick, v), QPointF(Thickness, v));
            if (isMajor) {
                p.save();
                p.translate(10, v + 2);
                p.rotate(90);
                p.drawText(QPointF(0, 0), QString::number(u, 'g', 6));
                p.restore();
            }
        }
    }
    if (m_marker >= 0) {
        double v = m_marker * scale + off;
        p.setPen(th.accent);
        if (m_orient == Qt::Horizontal) p.drawLine(QPointF(v, 0), QPointF(v, Thickness));
        else p.drawLine(QPointF(0, v), QPointF(Thickness, v));
    }
}

} // namespace pane
