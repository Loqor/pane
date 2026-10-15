// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>
#include <map>

namespace pane {

// Catppuccin-style color tokens. Light and Dark map onto the same names.
struct Theme {
    QString id, name;
    bool dark = false;
    bool catppuccin = false;
    QColor base, mantle, crust;
    QColor surface0, surface1, surface2;
    QColor overlay0, overlay1, overlay2;
    QColor text, subtext0, subtext1;
    QColor accent;
    std::map<QString, QColor> accents; // Catppuccin named accents

    // Derived
    QColor checkerA() const;
    QColor checkerB() const;
    QColor canvasBackdrop() const { return crust; }
    QColor danger() const;
};

class ThemeEngine : public QObject {
    Q_OBJECT
public:
    static ThemeEngine &instance();

    const Theme &current() const { return m_current; }
    QStringList themeIds() const;
    const Theme *theme(const QString &id) const;
    static QStringList catppuccinAccentNames();

    QString currentId() const { return m_current.id; }
    QString accentName() const { return m_accentName; }

    // Applies a theme; accentName is a Catppuccin accent name (for Catppuccin
    // themes) or a #rrggbb color (for Light/Dark). Empty = theme default.
    void apply(const QString &themeId, const QString &accentName = {});
    void loadCustomThemes();
    // Picks Light/Dark from the platform's color scheme preference.
    QString systemDefaultThemeId() const;

    QFont glyphFont(int pixelSize) const;
    QString glyphFamily() const { return m_glyphFamily; }

signals:
    void themeChanged();

private:
    ThemeEngine();
    QString styleSheet() const;
    std::map<QString, Theme> m_themes;
    QStringList m_order;
    Theme m_current;
    QString m_accentName;
    QString m_glyphFamily;
};

} // namespace pane
