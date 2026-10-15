// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/ToolOptionsBar.h"

#include "core/Glyphs.h"
#include "tools/DrawTools.h"
#include "tools/Gradient.h"
#include "ui/GlyphIcon.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

namespace pane {

namespace {

QComboBox *combo(QWidget *parent, const QStringList &items, int current)
{
    auto *c = new QComboBox(parent);
    c->addItems(items);
    c->setCurrentIndex(current);
    c->setFocusPolicy(Qt::ClickFocus);
    return c;
}

} // namespace

ToolOptionsBar::ToolOptionsBar(QWidget *parent) : QToolBar(QStringLiteral("Tool Options"), parent)
{
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(18, 18));
    setObjectName(QStringLiteral("ToolOptionsBar"));
}

void ToolOptionsBar::setTool(Tool *tool)
{
    m_tool = tool;
    rebuild();
}

QWidget *ToolOptionsBar::labeled(const QString &text)
{
    auto *l = new QLabel(text, this);
    l->setContentsMargins(6, 0, 2, 0);
    return l;
}

void ToolOptionsBar::rebuild()
{
    // QToolBar::clear() does not delete actions; delete them (and their widgets) ourselves.
    for (QAction *a : actions()) {
        removeAction(a);
        a->deleteLater(); // may be called from one of its own widgets
    }
    if (!m_tool) return;
    AppState &app = AppState::instance();
    ToolSettings &s = app.mutableSettings();
    // Helpers for simple int/bool settings.
    auto intCombo = [this, &app](const QStringList &items, int ToolSettings::*field) {
        auto *c = new QComboBox(this);
        c->addItems(items);
        c->setCurrentIndex(app.settings().*field);
        c->setFocusPolicy(Qt::ClickFocus);
        connect(c, &QComboBox::currentIndexChanged, this, [&app, field](int i) {
            app.mutableSettings().*field = i;
            app.notifySettingsChanged();
        });
        return c;
    };
    auto toggle = [this, &app](const QString &glyph, const QString &tip, bool ToolSettings::*field) {
        auto *b = new QToolButton(this);
        b->setText(glyph);
        b->setFont(ThemeEngine::instance().glyphFont(16));
        b->setCheckable(true);
        b->setChecked(app.settings().*field);
        b->setToolTip(tip);
        connect(b, &QToolButton::toggled, this, [&app, field](bool on) {
            app.mutableSettings().*field = on;
            app.notifySettingsChanged();
        });
        return b;
    };

    addWidget(labeled(QStringLiteral("Tool:")));
    auto *picker = new QToolButton(this);
    picker->setText(m_tool->glyph());
    picker->setFont(ThemeEngine::instance().glyphFont(18));
    picker->setToolTip(m_tool->name());
    picker->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(picker);
    for (Tool *t : ToolRegistry::instance().tools()) {
        QAction *a = menu->addAction(glyphIcon(t->glyph()), t->name());
        a->setEnabled(t->enabled());
        QString id = t->id();
        connect(a, &QAction::triggered, this, [this, id] { emit toolPicked(id); });
    }
    picker->setMenu(menu);
    addWidget(picker);
    addSeparator();

    for (ToolOption opt : m_tool->options()) {
        switch (opt) {
        case ToolOption::SelectionMode: {
            addWidget(labeled(QStringLiteral("Selection mode:")));
            auto *c = combo(this, {QStringLiteral("Replace"), QStringLiteral("Add (union)"), QStringLiteral("Subtract"),
                                   QStringLiteral("Intersect"), QStringLiteral("Invert (xor)")},
                            int(s.selectionMode));
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().selectionMode = CombineMode(i);
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::BrushWidth: {
            addWidget(labeled(QStringLiteral("Brush width:")));
            auto *c = new QComboBox(this);
            c->setEditable(true);
            c->setFocusPolicy(Qt::ClickFocus);
            for (int w : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75,
                          80, 85, 90, 95, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325, 350, 375, 400, 450, 500, 550,
                          600, 650, 700, 750, 800, 850, 900, 950, 1000, 1500, 2000})
                c->addItem(QString::number(w));
            c->setValidator(new QIntValidator(1, 2000, c));
            c->setCurrentText(QString::number(s.brushWidth));
            c->setFixedWidth(70);
            auto apply = [&app, c](int w) {
                w = std::clamp(w, 1, 2000);
                app.mutableSettings().brushWidth = w;
                if (c->currentText() != QString::number(w)) c->setCurrentText(QString::number(w));
                app.notifySettingsChanged();
            };
            connect(c, &QComboBox::currentTextChanged, this, [apply](const QString &t) {
                bool ok;
                int w = t.toInt(&ok);
                if (ok && w > 0) apply(w);
            });
            addWidget(c);
            auto *minus = new QToolButton(this);
            minus->setText(QStringLiteral("−"));
            minus->setToolTip(QStringLiteral("Decrease brush width ( [ )"));
            auto *plus = new QToolButton(this);
            plus->setText(QStringLiteral("+"));
            plus->setToolTip(QStringLiteral("Increase brush width ( ] )"));
            connect(minus, &QToolButton::clicked, this, [&app, apply] { apply(app.settings().brushWidth - 1); });
            connect(plus, &QToolButton::clicked, this, [&app, apply] { apply(app.settings().brushWidth + 1); });
            connect(&app, &AppState::settingsChanged, c, [c, &app] {
                if (c->currentText() != QString::number(app.settings().brushWidth))
                    c->setCurrentText(QString::number(app.settings().brushWidth));
            });
            addWidget(minus);
            addWidget(plus);
            break;
        }
        case ToolOption::Hardness: {
            addWidget(labeled(QStringLiteral("Hardness:")));
            auto *sl = new QSlider(Qt::Horizontal, this);
            sl->setRange(0, 100);
            sl->setValue(s.hardness);
            sl->setFixedWidth(100);
            sl->setFocusPolicy(Qt::ClickFocus);
            auto *val = new QLabel(QStringLiteral("%1%").arg(s.hardness), this);
            val->setMinimumWidth(36);
            connect(sl, &QSlider::valueChanged, this, [&app, val](int v) {
                app.mutableSettings().hardness = v;
                val->setText(QStringLiteral("%1%").arg(v));
                app.notifySettingsChanged();
            });
            addWidget(sl);
            addWidget(val);
            break;
        }
        case ToolOption::Antialias: {
            auto *b = new QToolButton(this);
            b->setCheckable(true);
            b->setChecked(s.antialias);
            b->setText(QStringLiteral("Antialiasing"));
            b->setToolTip(QStringLiteral("Antialiasing"));
            connect(b, &QToolButton::toggled, this, [&app](bool on) {
                app.mutableSettings().antialias = on;
                app.notifySettingsChanged();
            });
            addWidget(b);
            break;
        }
        case ToolOption::AlphaBlending: {
            addWidget(labeled(QStringLiteral("Blending:")));
            auto *c = combo(this, {QStringLiteral("Normal"), QStringLiteral("Overwrite")}, s.alphaBlending ? 0 : 1);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().alphaBlending = i == 0;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::FloodMode: {
            addWidget(labeled(QStringLiteral("Flood mode:")));
            auto *c = combo(this, {QStringLiteral("Contiguous"), QStringLiteral("Global")}, s.floodGlobal ? 1 : 0);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().floodGlobal = i == 1;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::Tolerance: {
            addWidget(labeled(QStringLiteral("Tolerance:")));
            auto *sl = new QSlider(Qt::Horizontal, this);
            sl->setRange(0, 100);
            sl->setValue(s.tolerance);
            sl->setFixedWidth(100);
            sl->setFocusPolicy(Qt::ClickFocus);
            auto *val = new QLabel(QStringLiteral("%1%").arg(s.tolerance), this);
            val->setMinimumWidth(36);
            connect(sl, &QSlider::valueChanged, this, [&app, val](int v) {
                app.mutableSettings().tolerance = v;
                val->setText(QStringLiteral("%1%").arg(v));
                app.notifySettingsChanged();
            });
            addWidget(sl);
            addWidget(val);
            break;
        }
        case ToolOption::Sampling: {
            addWidget(labeled(QStringLiteral("Sampling:")));
            auto *c = combo(this, {QStringLiteral("Layer"), QStringLiteral("Image")}, s.sampleImage ? 1 : 0);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().sampleImage = i == 1;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::PickerSampling: {
            auto *c = combo(this, {QStringLiteral("Single pixel"), QStringLiteral("3 x 3 region"), QStringLiteral("5 x 5 region"),
                                   QStringLiteral("7 x 7 region"), QStringLiteral("9 x 9 region")},
                            int(s.pickerSampling));
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().pickerSampling = PickerSampling(i);
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::PickerAfter: {
            addWidget(labeled(QStringLiteral("After click:")));
            auto *c = combo(this, {QStringLiteral("Do not switch tool"), QStringLiteral("Switch to previous tool"),
                                   QStringLiteral("Switch to Pencil tool")},
                            int(s.pickerAfter));
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().pickerAfter = PickerAfter(i);
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::MoveQuality: {
            addWidget(labeled(QStringLiteral("Resampling:")));
            auto *c = combo(this, {QStringLiteral("Bilinear"), QStringLiteral("Nearest Neighbor")}, s.moveSmooth ? 0 : 1);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().moveSmooth = i == 0;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::LassoMode: {
            addWidget(labeled(QStringLiteral("Lasso mode:")));
            auto *c = combo(this, {QStringLiteral("Freeform"), QStringLiteral("Polygon")}, s.lassoPolygon ? 1 : 0);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().lassoPolygon = i == 1;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::GradientType:
            addWidget(labeled(QStringLiteral("Gradient:")));
            addWidget(intCombo(gradientTypeNames(), &ToolSettings::gradientType));
            break;
        case ToolOption::GradientMode: {
            auto *c = combo(this, {QStringLiteral("Color mode"), QStringLiteral("Transparency mode")}, s.gradientTransparency ? 1 : 0);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().gradientTransparency = i == 1;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::GradientRepeat:
            addWidget(labeled(QStringLiteral("Repeat:")));
            addWidget(intCombo({QStringLiteral("No repeat"), QStringLiteral("Sawtooth"), QStringLiteral("Triangular")},
                               &ToolSettings::gradientRepeat));
            break;
        case ToolOption::ShapeType:
            addWidget(labeled(QStringLiteral("Shape:")));
            addWidget(intCombo(shapeTypeNames(), &ToolSettings::shapeType));
            break;
        case ToolOption::ShapeStyle:
            addWidget(labeled(QStringLiteral("Draw:")));
            addWidget(intCombo({QStringLiteral("Outline"), QStringLiteral("Filled"), QStringLiteral("Filled with outline")},
                               &ToolSettings::shapeStyle));
            break;
        case ToolOption::DashStyle:
            addWidget(labeled(QStringLiteral("Dash:")));
            addWidget(intCombo({QStringLiteral("Solid"), QStringLiteral("Dash"), QStringLiteral("Dot"), QStringLiteral("Dash-dot"),
                                QStringLiteral("Dash-dot-dot")},
                               &ToolSettings::dashStyle));
            break;
        case ToolOption::CurveType: {
            addWidget(labeled(QStringLiteral("Curve:")));
            auto *c = combo(this, {QStringLiteral("Spline"), QStringLiteral("Bézier")}, s.curveBezier ? 1 : 0);
            connect(c, &QComboBox::currentIndexChanged, this, [&app](int i) {
                app.mutableSettings().curveBezier = i == 1;
                app.notifySettingsChanged();
            });
            addWidget(c);
            break;
        }
        case ToolOption::Arrows:
            addWidget(toggle(glyphs::ArrowStart, QStringLiteral("Arrow at start"), &ToolSettings::arrowStart));
            addWidget(toggle(glyphs::ArrowEnd, QStringLiteral("Arrow at end"), &ToolSettings::arrowEnd));
            break;
        case ToolOption::Font: {
            addWidget(labeled(QStringLiteral("Font:")));
            auto *fc = new QFontComboBox(this);
            fc->setFocusPolicy(Qt::ClickFocus);
            fc->setCurrentFont(s.fontFamily.isEmpty() ? QApplication::font() : QFont(s.fontFamily));
            fc->setMaximumWidth(180);
            connect(fc, &QFontComboBox::currentFontChanged, this, [&app](const QFont &f) {
                app.mutableSettings().fontFamily = f.family();
                app.notifySettingsChanged();
            });
            addWidget(fc);
            auto *size = new QComboBox(this);
            size->setEditable(true);
            size->setFocusPolicy(Qt::ClickFocus);
            for (int v : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 96, 144, 288})
                size->addItem(QString::number(v));
            size->setValidator(new QIntValidator(1, 2000, size));
            size->setCurrentText(QString::number(s.fontSize));
            size->setFixedWidth(64);
            connect(size, &QComboBox::currentTextChanged, this, [&app](const QString &t) {
                bool ok;
                int v = t.toInt(&ok);
                if (!ok || v <= 0) return;
                app.mutableSettings().fontSize = v;
                app.notifySettingsChanged();
            });
            addWidget(size);
            break;
        }
        case ToolOption::TextStyle:
            addWidget(toggle(glyphs::Bold, QStringLiteral("Bold"), &ToolSettings::bold));
            addWidget(toggle(glyphs::Italic, QStringLiteral("Italic"), &ToolSettings::italic));
            addWidget(toggle(glyphs::Underline, QStringLiteral("Underline"), &ToolSettings::underline));
            addWidget(toggle(glyphs::Strikeout, QStringLiteral("Strikeout"), &ToolSettings::strikeout));
            break;
        case ToolOption::TextAlign: {
            auto *group = new QButtonGroup(this);
            const QString g[3] = {glyphs::AlignLeft, glyphs::AlignCenter, glyphs::AlignRight};
            const char *tips[3] = {"Align left", "Align center", "Align right"};
            for (int i = 0; i < 3; ++i) {
                auto *b = new QToolButton(this);
                b->setText(g[i]);
                b->setFont(ThemeEngine::instance().glyphFont(16));
                b->setCheckable(true);
                b->setChecked(s.textAlign == i);
                b->setToolTip(QString::fromLatin1(tips[i]));
                group->addButton(b, i);
                addWidget(b);
            }
            connect(group, &QButtonGroup::idClicked, this, [&app](int i) {
                app.mutableSettings().textAlign = i;
                app.notifySettingsChanged();
            });
            break;
        }
        }
    }
}

} // namespace pane
