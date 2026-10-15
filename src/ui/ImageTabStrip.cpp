#include "ui/ImageTabStrip.h"

#include "core/Glyphs.h"
#include "ui/theme/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

namespace pane {

ImageTabStrip::ImageTabStrip(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ImageTabStrip"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(40);
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, this, qOverload<>(&QWidget::update));
}

void ImageTabStrip::setTabs(const std::vector<Tab> &tabs, int current)
{
    m_tabs = tabs;
    m_current = current;
    // Keep the current tab visible.
    if (current >= 0) {
        QRect r = tabRect(current);
        if (r.right() > width()) m_scroll += r.right() - width() + 4;
        if (r.left() < 0) m_scroll += r.left() - 4;
        m_scroll = std::max(0, m_scroll);
    }
    update();
}

void ImageTabStrip::updateTab(int index, const Tab &tab)
{
    if (index < 0 || size_t(index) >= m_tabs.size()) return;
    m_tabs[size_t(index)] = tab;
    update(tabRect(index));
}

QRect ImageTabStrip::tabRect(int i) const { return QRect(4 + i * (TabWidth + 4) - m_scroll, 2, TabWidth, height() - 4); }
QRect ImageTabStrip::closeRect(int i) const
{
    QRect r = tabRect(i);
    return QRect(r.right() - 15, r.top() + 1, 15, 15);
}

int ImageTabStrip::tabAt(QPoint p) const
{
    for (int i = 0; i < int(m_tabs.size()); ++i)
        if (tabRect(i).contains(p)) return i;
    return -1;
}

void ImageTabStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    p.fillRect(rect(), th.mantle);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < int(m_tabs.size()); ++i) {
        QRect r = tabRect(i);
        if (r.right() < 0 || r.left() > width()) continue;
        const Tab &t = m_tabs[size_t(i)];
        bool cur = i == m_current;
        if (cur || i == m_hover) {
            QColor bg = cur ? th.accent : th.surface1;
            if (cur) bg.setAlpha(th.dark ? 70 : 50);
            p.setPen(cur ? QPen(th.accent, 1) : Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        }
        // Thumbnail on checkerboard.
        QRect thumbArea = r.adjusted(4, 3, -4, -3);
        if (!t.thumbnail.isNull()) {
            QSize ts = t.thumbnail.size().scaled(thumbArea.size(), Qt::KeepAspectRatio);
            QRect tr(thumbArea.center().x() - ts.width() / 2 + 1, thumbArea.center().y() - ts.height() / 2 + 1, ts.width(), ts.height());
            p.setRenderHint(QPainter::Antialiasing, false);
            for (int y = 0; y < tr.height(); y += 4)
                for (int x = 0; x < tr.width(); x += 4)
                    p.fillRect(QRect(tr.left() + x, tr.top() + y, 4, 4).intersected(tr), ((x + y) / 4) % 2 ? th.checkerB() : th.checkerA());
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(tr, t.thumbnail);
            p.setPen(th.surface2);
            p.setBrush(Qt::NoBrush);
            p.drawRect(tr.adjusted(0, 0, -1, -1));
            p.setRenderHint(QPainter::Antialiasing);
        }
        if (t.dirty) {
            p.setPen(Qt::NoPen);
            p.setBrush(th.accent);
            p.drawEllipse(QPointF(r.left() + 7, r.bottom() - 6), 3, 3);
        }
        if (i == m_hover) {
            QRect cr = closeRect(i);
            p.setPen(Qt::NoPen);
            p.setBrush(th.surface2);
            p.drawEllipse(cr.adjusted(1, 1, -1, -1));
            p.setFont(ThemeEngine::instance().glyphFont(11));
            p.setPen(th.text);
            p.drawText(cr, Qt::AlignCenter, glyphs::Close);
        }
    }
    if (m_dragFrom >= 0 && m_dragTo >= 0 && m_dragTo != m_dragFrom) {
        QRect r = tabRect(m_dragTo);
        int x = m_dragTo > m_dragFrom ? r.right() + 2 : r.left() - 2;
        p.setPen(QPen(th.accent, 2));
        p.drawLine(x, 4, x, height() - 4);
    }
}

void ImageTabStrip::mousePressEvent(QMouseEvent *e)
{
    int i = tabAt(e->position().toPoint());
    if (i < 0) return;
    if (e->button() == Qt::MiddleButton) {
        emit closeRequested(i);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (closeRect(i).contains(e->position().toPoint())) {
        emit closeRequested(i);
        return;
    }
    m_dragFrom = i;
    m_dragTo = -1;
    m_press = e->position().toPoint();
    if (i != m_current) emit currentChanged(i);
}

void ImageTabStrip::mouseMoveEvent(QMouseEvent *e)
{
    int i = tabAt(e->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        if (i >= 0) setToolTip(m_tabs[size_t(i)].title + (m_tabs[size_t(i)].dirty ? QStringLiteral(" *") : QString()));
        update();
    }
    if (m_dragFrom >= 0 && (e->buttons() & Qt::LeftButton) && (e->position().toPoint() - m_press).manhattanLength() > 6) {
        int x = int(e->position().x()) + m_scroll - 4;
        m_dragTo = std::clamp(x / (TabWidth + 4), 0, int(m_tabs.size()) - 1);
        update();
    }
}

void ImageTabStrip::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragFrom >= 0 && m_dragTo >= 0 && m_dragTo != m_dragFrom) emit moved(m_dragFrom, m_dragTo);
    m_dragFrom = m_dragTo = -1;
    update();
}

void ImageTabStrip::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void ImageTabStrip::wheelEvent(QWheelEvent *e)
{
    int total = int(m_tabs.size()) * (TabWidth + 4) + 4;
    m_scroll = std::clamp(m_scroll - e->angleDelta().y() / 2 - e->angleDelta().x() / 2, 0, std::max(0, total - width()));
    update();
}

} // namespace pane
