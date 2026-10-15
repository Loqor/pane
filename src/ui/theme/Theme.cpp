#include "ui/theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace pane {

namespace {

QColor c(const char *hex) { return QColor(QString::fromLatin1(hex)); }

const char *const kAccentNames[] = {"rosewater", "flamingo", "pink", "mauve", "red", "maroon", "peach",
                                    "yellow", "green", "teal", "sky", "sapphire", "blue", "lavender"};

Theme catppuccin(const QString &id, const QString &name, bool dark, std::initializer_list<const char *> accents,
                 std::initializer_list<const char *> neutrals)
{
    Theme t;
    t.id = id;
    t.name = name;
    t.dark = dark;
    t.catppuccin = true;
    int i = 0;
    for (const char *a : accents) t.accents[QString::fromLatin1(kAccentNames[i++])] = c(a);
    auto n = neutrals.begin();
    t.text = c(*n++); t.subtext1 = c(*n++); t.subtext0 = c(*n++);
    t.overlay2 = c(*n++); t.overlay1 = c(*n++); t.overlay0 = c(*n++);
    t.surface2 = c(*n++); t.surface1 = c(*n++); t.surface0 = c(*n++);
    t.base = c(*n++); t.mantle = c(*n++); t.crust = c(*n++);
    t.accent = t.accents[QStringLiteral("blue")];
    return t;
}

} // namespace

QColor Theme::checkerA() const { return dark ? QColor(0x66, 0x66, 0x66) : QColor(0xFF, 0xFF, 0xFF); }
QColor Theme::checkerB() const { return dark ? QColor(0x4D, 0x4D, 0x4D) : QColor(0xCC, 0xCC, 0xCC); }
QColor Theme::danger() const
{
    auto it = accents.find(QStringLiteral("red"));
    return it != accents.end() ? it->second : QColor(0xC4, 0x2B, 0x1C);
}

ThemeEngine &ThemeEngine::instance()
{
    static ThemeEngine e;
    return e;
}

ThemeEngine::ThemeEngine()
{
    int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SymbolsNerdFont-Regular.ttf"));
    QStringList fams = QFontDatabase::applicationFontFamilies(fontId);
    m_glyphFamily = fams.isEmpty() ? QStringLiteral("Symbols Nerd Font") : fams.first();

    Theme light;
    light.id = QStringLiteral("light");
    light.name = QStringLiteral("Light");
    light.base = c("#f3f3f3"); light.mantle = c("#ebebeb"); light.crust = c("#dcdcdc");
    light.surface0 = c("#ffffff"); light.surface1 = c("#e5e5e5"); light.surface2 = c("#d1d1d1");
    light.overlay0 = c("#a3a3a3"); light.overlay1 = c("#8a8a8a"); light.overlay2 = c("#707070");
    light.text = c("#1b1b1b"); light.subtext1 = c("#3a3a3a"); light.subtext0 = c("#5f5f5f");
    light.accent = c("#0067c0");

    Theme dark;
    dark.id = QStringLiteral("dark");
    dark.name = QStringLiteral("Dark");
    dark.dark = true;
    dark.base = c("#2b2b2b"); dark.mantle = c("#232323"); dark.crust = c("#1b1b1b");
    dark.surface0 = c("#383838"); dark.surface1 = c("#444444"); dark.surface2 = c("#535353");
    dark.overlay0 = c("#6b6b6b"); dark.overlay1 = c("#878787"); dark.overlay2 = c("#a3a3a3");
    dark.text = c("#f3f3f3"); dark.subtext1 = c("#d6d6d6"); dark.subtext0 = c("#b4b4b4");
    dark.accent = c("#4cc2ff");

    // Official Catppuccin palettes: accents (rosewater..lavender), then
    // text, subtext1, subtext0, overlay2..0, surface2..0, base, mantle, crust.
    Theme latte = catppuccin(QStringLiteral("latte"), QStringLiteral("Catppuccin Latte"), false,
        {"#dc8a78", "#dd7878", "#ea76cb", "#8839ef", "#d20f39", "#e64553", "#fe640b", "#df8e1d", "#40a02b", "#179299", "#04a5e5", "#209fb5", "#1e66f5", "#7287fd"},
        {"#4c4f69", "#5c5f77", "#6c6f85", "#7c7f93", "#8c8fa1", "#9ca0b0", "#acb0be", "#bcc0cc", "#ccd0da", "#eff1f5", "#e6e9ef", "#dce0e8"});
    Theme frappe = catppuccin(QStringLiteral("frappe"), QStringLiteral("Catppuccin Frappé"), true,
        {"#f2d5cf", "#eebebe", "#f4b8e4", "#ca9ee6", "#e78284", "#ea999c", "#ef9f76", "#e5c890", "#a6d189", "#81c8be", "#99d1db", "#85c1dc", "#8caaee", "#babbf1"},
        {"#c6d0f5", "#b5bfe2", "#a5adce", "#949cbb", "#838ba7", "#737994", "#626880", "#51576d", "#414559", "#303446", "#292c3c", "#232634"});
    Theme macchiato = catppuccin(QStringLiteral("macchiato"), QStringLiteral("Catppuccin Macchiato"), true,
        {"#f4dbd6", "#f0c6c6", "#f5bde6", "#c6a0f6", "#ed8796", "#ee99a0", "#f5a97f", "#eed49f", "#a6da95", "#8bd5ca", "#91d7e3", "#7dc4e4", "#8aadf4", "#b7bdf8"},
        {"#cad3f5", "#b8c0e0", "#a5adcb", "#939ab7", "#8087a2", "#6e738d", "#5b6078", "#494d64", "#363a4f", "#24273a", "#1e2030", "#181926"});
    Theme mocha = catppuccin(QStringLiteral("mocha"), QStringLiteral("Catppuccin Mocha"), true,
        {"#f5e0dc", "#f2cdcd", "#f5c2e7", "#cba6f7", "#f38ba8", "#eba0ac", "#fab387", "#f9e2af", "#a6e3a1", "#94e2d5", "#89dceb", "#74c7ec", "#89b4fa", "#b4befe"},
        {"#cdd6f4", "#bac2de", "#a6adc8", "#9399b2", "#7f849c", "#6c7086", "#585b70", "#45475a", "#313244", "#1e1e2e", "#181825", "#11111b"});

    for (Theme *t : {&light, &dark, &latte, &frappe, &macchiato, &mocha}) {
        m_order << t->id;
        m_themes[t->id] = *t;
    }
    m_current = m_themes[QStringLiteral("light")];
}

QStringList ThemeEngine::themeIds() const { return m_order; }

const Theme *ThemeEngine::theme(const QString &id) const
{
    auto it = m_themes.find(id);
    return it == m_themes.end() ? nullptr : &it->second;
}

QStringList ThemeEngine::catppuccinAccentNames()
{
    QStringList l;
    for (const char *n : kAccentNames) l << QString::fromLatin1(n);
    return l;
}

QString ThemeEngine::systemDefaultThemeId() const
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("dark")
                                                                                   : QStringLiteral("light");
}

void ThemeEngine::loadCustomThemes()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/themes"));
    const QStringList keys = {"base", "mantle", "crust", "surface0", "surface1", "surface2", "overlay0",
                              "overlay1", "overlay2", "text", "subtext0", "subtext1", "accent"};
    for (const QString &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        Theme t;
        t.id = QStringLiteral("custom:") + QFileInfo(f).baseName();
        t.name = o.value(QStringLiteral("name")).toString(QFileInfo(f).baseName());
        t.dark = o.value(QStringLiteral("dark")).toBool();
        QColor *targets[] = {&t.base, &t.mantle, &t.crust, &t.surface0, &t.surface1, &t.surface2, &t.overlay0,
                           &t.overlay1, &t.overlay2, &t.text, &t.subtext0, &t.subtext1, &t.accent};
        bool ok = true;
        for (int i = 0; i < keys.size(); ++i) {
            QColor col(o.value(keys[i]).toString());
            if (!col.isValid()) {
                qWarning("Skipping theme %s: missing or invalid \"%s\"", qPrintable(f), qPrintable(keys[i]));
                ok = false;
                break;
            }
            *targets[i] = col;
        }
        if (!ok) continue;
        if (!m_themes.count(t.id)) m_order << t.id;
        m_themes[t.id] = t;
    }
}

void ThemeEngine::apply(const QString &themeId, const QString &accent)
{
    const Theme *t = theme(themeId);
    if (!t) t = theme(QStringLiteral("light"));
    m_current = *t;
    m_accentName = accent;
    if (!accent.isEmpty()) {
        auto it = m_current.accents.find(accent);
        if (it != m_current.accents.end()) m_current.accent = it->second;
        else if (QColor(accent).isValid() && !m_current.catppuccin) m_current.accent = QColor(accent);
    }

    const Theme &th = m_current;
    QPalette p;
    p.setColor(QPalette::Window, th.base);
    p.setColor(QPalette::WindowText, th.text);
    p.setColor(QPalette::Base, th.surface0);
    p.setColor(QPalette::AlternateBase, th.mantle);
    p.setColor(QPalette::ToolTipBase, th.mantle);
    p.setColor(QPalette::ToolTipText, th.text);
    p.setColor(QPalette::PlaceholderText, th.overlay1);
    p.setColor(QPalette::Text, th.text);
    p.setColor(QPalette::Button, th.surface0);
    p.setColor(QPalette::ButtonText, th.text);
    p.setColor(QPalette::BrightText, th.danger());
    p.setColor(QPalette::Highlight, th.accent);
    p.setColor(QPalette::HighlightedText, th.dark ? th.crust : QColor(Qt::white));
    p.setColor(QPalette::Link, th.accent);
    p.setColor(QPalette::Light, th.surface1);
    p.setColor(QPalette::Midlight, th.surface1);
    p.setColor(QPalette::Mid, th.surface2);
    p.setColor(QPalette::Dark, th.overlay0);
    p.setColor(QPalette::Shadow, th.crust);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, th.overlay0);

    if (qApp) {
        if (!qApp->style() || qApp->style()->name() != QLatin1String("fusion")) qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        qApp->setPalette(p);
        qApp->setStyleSheet(styleSheet());
    }
    emit themeChanged();
}

QFont ThemeEngine::glyphFont(int pixelSize) const
{
    QFont f(m_glyphFamily);
    f.setPixelSize(pixelSize);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QString ThemeEngine::styleSheet() const
{
    const Theme &t = m_current;
    auto n = [](const QColor &c) { return c.name(QColor::HexArgb); };
    QColor accentSoft = t.accent;
    accentSoft.setAlpha(t.dark ? 70 : 45);
    QColor onAccent = t.dark ? t.crust : QColor(Qt::white);
    QString s = QStringLiteral(R"(
QWidget { color: %text; }
QMainWindow, QDialog { background: %base; }
QToolTip { background: %mantle; color: %text; border: 1px solid %surface2; padding: 4px 6px; border-radius: 4px; }

#MenuRow, QMenuBar { background: %mantle; }
QMenuBar::item { padding: 5px 9px; background: transparent; border-radius: 4px; }
QMenuBar::item:selected { background: %surface1; }
QMenuBar::item:pressed { background: %surface2; }
QMenu { background: %mantle; border: 1px solid %surface2; padding: 4px; border-radius: 6px; }
QMenu::item { padding: 5px 28px 5px 26px; border-radius: 4px; }
QMenu::item:selected { background: %accentSoft; }
QMenu::item:disabled { color: %overlay0; }
QMenu::separator { height: 1px; background: %surface1; margin: 4px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }

QToolBar { background: %mantle; border: none; border-bottom: 1px solid %crust; spacing: 2px; padding: 2px 4px; }
QToolBar::separator { background: %surface1; width: 1px; margin: 4px 4px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px; }
QToolButton:hover { background: %surface1; }
QToolButton:pressed { background: %surface2; }
QToolButton:checked { background: %accentSoft; border-color: %accent; }
QToolButton:disabled { color: %overlay0; }
QToolButton::menu-indicator { image: none; width: 0; }

QPushButton { background: %surface0; border: 1px solid %surface2; border-radius: 4px; padding: 5px 14px; min-width: 64px; }
QPushButton:hover { background: %surface1; }
QPushButton:pressed { background: %surface2; }
QPushButton:default { border: 1px solid %accent; }
QPushButton:disabled { color: %overlay0; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
  background: %surface0; border: 1px solid %surface2; border-radius: 4px; padding: 2px 6px; min-height: 20px;
  selection-background-color: %accent; selection-color: %onAccent; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid %accent; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %mantle; border: 1px solid %surface2; selection-background-color: %accentSoft; selection-color: %text; outline: 0; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 14px; border: none; background: transparent; }

QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QSlider::groove:horizontal { height: 4px; background: %surface2; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %accent; border-radius: 2px; }
QSlider::handle:horizontal { background: %accent; width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; border: 2px solid %surface0; }
QSlider::groove:vertical { width: 4px; background: %surface2; border-radius: 2px; }
QSlider::handle:vertical { background: %accent; width: 12px; height: 12px; margin: 0 -5px; border-radius: 6px; border: 2px solid %surface0; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle { background: %overlay0; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }
QScrollBar::handle:hover { background: %overlay1; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar { background: %mantle; border-top: 1px solid %crust; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding: 0 6px; }

QGroupBox { border: 1px solid %surface1; border-radius: 6px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %subtext0; }
QListWidget, QListView { background: %base; border: none; outline: 0; }
QListWidget::item { padding: 3px 4px; border-radius: 4px; }
QListWidget::item:selected { background: %accentSoft; color: %text; }
QListWidget::item:hover { background: %surface1; }

#FloatingPanel { background: %base; border: 1px solid %surface2; border-radius: 8px; }
#PanelTitle { background: transparent; color: %subtext0; font-weight: 600; }
#PanelFooter { border-top: 1px solid %surface1; }
#ImageTabStrip { background: %mantle; }
)");
    std::pair<const char *, QColor> tokens[] = {
        {"%accentSoft", accentSoft}, {"%onAccent", onAccent}, {"%accent", t.accent},
        {"%subtext0", t.subtext0}, {"%subtext1", t.subtext1}, {"%surface0", t.surface0},
        {"%surface1", t.surface1}, {"%surface2", t.surface2}, {"%overlay0", t.overlay0},
        {"%overlay1", t.overlay1}, {"%overlay2", t.overlay2}, {"%mantle", t.mantle},
        {"%crust", t.crust}, {"%base", t.base}, {"%text", t.text},
    };
    for (auto &[k, v] : tokens) s.replace(QLatin1String(k), n(v));
    return s;
}

} // namespace pane
