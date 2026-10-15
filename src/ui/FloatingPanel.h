// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

namespace pane {

class FloatingPanelHost;

// A paint.net-style floating window drawn inside the main window, so tiling
// compositors (niri) only ever see one toplevel.
class FloatingPanel : public QFrame {
    Q_OBJECT
public:
    FloatingPanel(const QString &id, const QString &title, const QString &glyph, FloatingPanelHost *host, bool resizable);

    QString id() const { return m_id; }
    void setContent(QWidget *w);
    QWidget *content() const { return m_content; }
    bool isResizable() const { return m_resizable; }
    void setTranslucent(bool on);

    // Edge anchoring, so panels stick to the side they're closest to.
    void updateAnchor();
    void applyAnchor();

signals:
    void visibilityChanged(bool visible);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
    int edgeAt(QPoint p) const;
    QPoint snapped(QPoint topLeft) const;

    QString m_id;
    FloatingPanelHost *m_host;
    QWidget *m_title = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout = nullptr;
    bool m_resizable;
    bool m_dragging = false;
    int m_resizeEdges = None;
    QPoint m_pressGlobal;
    QRect m_pressGeom;
    QGraphicsOpacityEffect *m_opacity = nullptr;
    bool m_hovered = false;
    bool m_translucent = false;
    bool m_anchorRight = false, m_anchorBottom = false;
    QPoint m_anchorDist;
};

// Container for the canvas plus overlaid floating panels.
class FloatingPanelHost : public QWidget {
    Q_OBJECT
public:
    explicit FloatingPanelHost(QWidget *parent = nullptr);
    void setCentral(QWidget *w);
    void addPanel(FloatingPanel *p);
    const QList<FloatingPanel *> &panels() const { return m_panels; }
    FloatingPanel *panel(const QString &id) const;

    void resetPositions();
    void saveState() const;
    void restoreState();
    void setCanvasInteracting(bool on);
    void setTranslucencyEnabled(bool on);
    bool translucencyEnabled() const { return m_translucencyEnabled; }

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    QWidget *m_central = nullptr;
    QList<FloatingPanel *> m_panels;
    bool m_translucencyEnabled = true;
    bool m_restored = false;
};

} // namespace pane
