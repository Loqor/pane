#include "ui/GlyphIcon.h"

#include "ui/theme/Theme.h"

#include <QPainter>

namespace pane {

void GlyphIconEngine::paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State)
{
    const Theme &t = ThemeEngine::instance().current();
    QColor color = m_tint == Tint::Accent ? t.accent : m_tint == Tint::Danger ? t.danger() : t.text;
    if (mode == QIcon::Disabled) color = t.overlay0;
    p->save();
    p->setRenderHint(QPainter::TextAntialiasing);
    p->setFont(ThemeEngine::instance().glyphFont(int(rect.height() * 0.9)));
    p->setPen(color);
    p->drawText(rect, Qt::AlignCenter, m_glyph);
    p->restore();
}

QPixmap GlyphIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    return scaledPixmap(size, mode, state, 1.0);
}

QPixmap GlyphIconEngine::scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale)
{
    QPixmap pm(size * scale);
    pm.setDevicePixelRatio(scale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(&p, QRect(QPoint(), size), mode, state);
    return pm;
}

QIcon glyphIcon(const QString &glyph, GlyphIconEngine::Tint tint) { return QIcon(new GlyphIconEngine(glyph, tint)); }

} // namespace pane
