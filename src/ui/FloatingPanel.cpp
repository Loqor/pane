// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/FloatingPanel.h"

#include "core/Glyphs.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QSettings>
#include <QToolButton>

namespace pane {

namespace {
constexpr int SnapDistance = 8;
constexpr int Margin = 8;
constexpr int Grip = 6;
} // namespace

FloatingPanel::FloatingPanel(const QString &id, const QString &title, const QString &glyph, FloatingPanelHost *host,
                             bool resizable)
    : QFrame(host), m_id(id), m_host(host), m_resizable(resizable)
{
    setObjectName(QStringLiteral("FloatingPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 4);
    m_layout->setSpacing(2);

    m_title = new QWidget(this);
    m_title->setCursor(Qt::SizeAllCursor);
    auto *tl = new QHBoxLayout(m_title);
    tl->setContentsMargins(4, 0, 0, 0);
    tl->setSpacing(4);
    auto *icon = new QLabel(glyph, m_title);
    icon->setFont(ThemeEngine::instance().glyphFont(13));
    icon->setObjectName(QStringLiteral("PanelTitle"));
    auto *label = new QLabel(title, m_title);
    label->setObjectName(QStringLiteral("PanelTitle"));
    auto *close = new QToolButton(m_title);
    close->setText(glyphs::Close);
    close->setFont(ThemeEngine::instance().glyphFont(13));
    close->setAutoRaise(true);
    close->setToolTip(QStringLiteral("Close"));
    close->setCursor(Qt::ArrowCursor);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    tl->addWidget(icon);
    tl->addWidget(label);
    tl->addStretch();
    tl->addWidget(close);
    // Let drags on the title reach the panel.
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->installEventFilter(this);
    m_layout->addWidget(m_title);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(1.0);
    m_opacity->setEnabled(false);
    setGraphicsEffect(m_opacity);
}

bool FloatingPanel::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_title) {
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(e);
            QMouseEvent mapped(me->type(), m_title->mapTo(this, me->position()), me->scenePosition(), me->globalPosition(),
                               me->button(), me->buttons(), me->modifiers());
            if (e->type() == QEvent::MouseButtonPress) mousePressEvent(&mapped);
            else if (e->type() == QEvent::MouseMove) mouseMoveEvent(&mapped);
            else mouseReleaseEvent(&mapped);
            return true;
        }
        default: break;
        }
    }
    return QFrame::eventFilter(o, e);
}

void FloatingPanel::setContent(QWidget *w)
{
    m_content = w;
    m_layout->addWidget(w, 1);
}

void FloatingPanel::setTranslucent(bool on)
{
    m_translucent = on;
    bool apply = on && !m_hovered;
    m_opacity->setEnabled(apply);
    m_opacity->setOpacity(apply ? 0.4 : 1.0);
}

int FloatingPanel::edgeAt(QPoint p) const
{
    if (!m_resizable) return None;
    int e = None;
    if (p.x() <= Grip) e |= Left;
    if (p.x() >= width() - Grip) e |= Right;
    if (p.y() >= height() - Grip) e |= Bottom;
    if (p.y() <= 2) e |= Top;
    return e;
}

QPoint FloatingPanel::snapped(QPoint tl) const
{
    QRect host = m_host->rect().adjusted(Margin, Margin, -Margin, -Margin);
    QRect r(tl, size());
    auto snap = [](int v, int target) { return std::abs(v - target) <= SnapDistance; };
    if (snap(r.left(), host.left())) r.moveLeft(host.left());
    if (snap(r.right(), host.right())) r.moveRight(host.right());
    if (snap(r.top(), host.top())) r.moveTop(host.top());
    if (snap(r.bottom(), host.bottom())) r.moveBottom(host.bottom());
    for (FloatingPanel *o : m_host->panels()) {
        if (o == this || !o->isVisible()) continue;
        QRect g = o->geometry();
        bool vOverlap = r.bottom() >= g.top() - SnapDistance && r.top() <= g.bottom() + SnapDistance;
        bool hOverlap = r.right() >= g.left() - SnapDistance && r.left() <= g.right() + SnapDistance;
        if (vOverlap) {
            if (snap(r.left(), g.right() + 4)) r.moveLeft(g.right() + 4);
            if (snap(r.right(), g.left() - 4)) r.moveRight(g.left() - 4);
        }
        if (hOverlap) {
            if (snap(r.top(), g.bottom() + 4)) r.moveTop(g.bottom() + 4);
            if (snap(r.bottom(), g.top() - 4)) r.moveBottom(g.top() - 4);
            if (snap(r.left(), g.left())) r.moveLeft(g.left());
            if (snap(r.right(), g.right())) r.moveRight(g.right());
        }
    }
    // Keep at least part of the panel on screen.
    QRect bounds = m_host->rect();
    r.moveLeft(std::clamp(r.left(), bounds.left() - r.width() + 40, bounds.right() - 40));
    r.moveTop(std::clamp(r.top(), bounds.top(), bounds.bottom() - 30));
    return r.topLeft();
}

void FloatingPanel::mousePressEvent(QMouseEvent *e)
{
    raise();
    if (e->button() != Qt::LeftButton) return;
    m_pressGlobal = e->globalPosition().toPoint();
    m_pressGeom = geometry();
    m_resizeEdges = edgeAt(e->position().toPoint());
    if (m_resizeEdges == None && m_title->geometry().contains(e->position().toPoint())) m_dragging = true;
}

void FloatingPanel::mouseMoveEvent(QMouseEvent *e)
{
    QPoint d = e->globalPosition().toPoint() - m_pressGlobal;
    if (m_dragging) {
        move(snapped(m_pressGeom.topLeft() + d));
        return;
    }
    if (m_resizeEdges != None && (e->buttons() & Qt::LeftButton)) {
        QRect g = m_pressGeom;
        if (m_resizeEdges & Left) g.setLeft(std::min(g.left() + d.x(), g.right() - minimumWidth()));
        if (m_resizeEdges & Right) g.setRight(g.right() + d.x());
        if (m_resizeEdges & Bottom) g.setBottom(g.bottom() + d.y());
        if (m_resizeEdges & Top) g.setTop(std::min(g.top() + d.y(), g.bottom() - minimumHeight()));
        g.setSize(g.size().expandedTo(minimumSizeHint()).expandedTo(QSize(140, 100)));
        setGeometry(g);
        return;
    }
    int edge = edgeAt(e->position().toPoint());
    if ((edge & (Left | Right)) && (edge & Bottom)) setCursor((edge & Left) ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor);
    else if (edge & (Left | Right)) setCursor(Qt::SizeHorCursor);
    else if (edge & (Bottom | Top)) setCursor(Qt::SizeVerCursor);
    else if (m_title->geometry().contains(e->position().toPoint())) setCursor(Qt::SizeAllCursor);
    else unsetCursor();
}

void FloatingPanel::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging || m_resizeEdges != None) updateAnchor();
    m_dragging = false;
    m_resizeEdges = None;
}

void FloatingPanel::showEvent(QShowEvent *e)
{
    QFrame::showEvent(e);
    emit visibilityChanged(true);
}

void FloatingPanel::hideEvent(QHideEvent *e)
{
    QFrame::hideEvent(e);
    emit visibilityChanged(false);
}

void FloatingPanel::enterEvent(QEnterEvent *e)
{
    m_hovered = true;
    setTranslucent(m_translucent);
    QFrame::enterEvent(e);
}

void FloatingPanel::leaveEvent(QEvent *e)
{
    m_hovered = false;
    setTranslucent(m_translucent);
    QFrame::leaveEvent(e);
}

void FloatingPanel::updateAnchor()
{
    QRect h = m_host->rect();
    QRect g = geometry();
    m_anchorRight = g.center().x() > h.center().x();
    m_anchorBottom = g.center().y() > h.center().y();
    m_anchorDist = QPoint(m_anchorRight ? h.right() - g.right() : g.left(), m_anchorBottom ? h.bottom() - g.bottom() : g.top());
}

void FloatingPanel::applyAnchor()
{
    QRect h = m_host->rect();
    QRect g = geometry();
    int x = m_anchorRight ? h.right() - m_anchorDist.x() - g.width() + 1 : m_anchorDist.x();
    int y = m_anchorBottom ? h.bottom() - m_anchorDist.y() - g.height() + 1 : m_anchorDist.y();
    move(std::max(0, x), std::max(0, y));
}

// ------------------------------------------------------------------ host

FloatingPanelHost::FloatingPanelHost(QWidget *parent) : QWidget(parent) {}

void FloatingPanelHost::setCentral(QWidget *w)
{
    m_central = w;
    w->setParent(this);
    w->lower();
}

void FloatingPanelHost::addPanel(FloatingPanel *p)
{
    m_panels << p;
    p->raise();
}

FloatingPanel *FloatingPanelHost::panel(const QString &id) const
{
    for (FloatingPanel *p : m_panels)
        if (p->id() == id) return p;
    return nullptr;
}

void FloatingPanelHost::resizeEvent(QResizeEvent *)
{
    if (m_central) m_central->setGeometry(rect());
    if (!m_restored) {
        m_restored = true;
        restoreState();
        return;
    }
    for (FloatingPanel *p : m_panels) p->applyAnchor();
}

void FloatingPanelHost::resetPositions()
{
    QRect h = rect().adjusted(Margin, Margin, -Margin, -Margin);
    for (FloatingPanel *p : m_panels) {
        p->adjustSize();
        QSize s = p->sizeHint();
        if (p->id() == QLatin1String("tools")) p->setGeometry(QRect(h.topLeft(), s));
        else if (p->id() == QLatin1String("colors")) p->setGeometry(QRect(QPoint(h.left(), h.bottom() - s.height() + 1), s));
        else if (p->id() == QLatin1String("history")) p->setGeometry(QRect(QPoint(h.right() - 220 + 1, h.top()), QSize(220, 200)));
        else if (p->id() == QLatin1String("layers")) p->setGeometry(QRect(QPoint(h.right() - 220 + 1, h.bottom() - 220 + 1), QSize(220, 220)));
        p->show();
        p->raise();
        p->updateAnchor();
    }
}

void FloatingPanelHost::saveState() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("Panels"));
    for (FloatingPanel *p : m_panels) {
        s.setValue(p->id() + QStringLiteral("/geometry"), p->geometry());
        s.setValue(p->id() + QStringLiteral("/visible"), p->isVisible());
    }
    s.setValue(QStringLiteral("translucent"), m_translucencyEnabled);
    s.endGroup();
}

void FloatingPanelHost::restoreState()
{
    resetPositions();
    QSettings s;
    s.beginGroup(QStringLiteral("Panels"));
    m_translucencyEnabled = s.value(QStringLiteral("translucent"), true).toBool();
    for (FloatingPanel *p : m_panels) {
        QRect g = s.value(p->id() + QStringLiteral("/geometry")).toRect();
        if (g.isValid() && rect().intersects(g)) p->setGeometry(g);
        p->setVisible(s.value(p->id() + QStringLiteral("/visible"), true).toBool());
        p->updateAnchor();
    }
    s.endGroup();
}

void FloatingPanelHost::setCanvasInteracting(bool on)
{
    for (FloatingPanel *p : m_panels) p->setTranslucent(on && m_translucencyEnabled);
}

void FloatingPanelHost::setTranslucencyEnabled(bool on)
{
    m_translucencyEnabled = on;
    if (!on) setCanvasInteracting(false);
}

} // namespace pane
