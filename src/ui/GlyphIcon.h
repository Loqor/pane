#pragma once

#include <QIcon>
#include <QIconEngine>
#include <QString>

namespace pane {

// Renders a Nerd Font glyph as an icon, colored from the current theme at paint
// time (so icons follow live theme changes).
class GlyphIconEngine : public QIconEngine {
public:
    enum class Tint { Text, Accent, Danger };
    explicit GlyphIconEngine(QString glyph, Tint tint = Tint::Text) : m_glyph(std::move(glyph)), m_tint(tint) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override;
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override;
    QIconEngine *clone() const override { return new GlyphIconEngine(m_glyph, m_tint); }

private:
    QString m_glyph;
    Tint m_tint;
};

QIcon glyphIcon(const QString &glyph, GlyphIconEngine::Tint tint = GlyphIconEngine::Tint::Text);

} // namespace pane
