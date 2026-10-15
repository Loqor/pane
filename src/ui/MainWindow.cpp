// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/MainWindow.h"

#include "core/Commands.h"
#include "core/Glyphs.h"
#include "io/FileType.h"
#include "io/Formats.h"
#include "tools/Tools.h"
#include "ui/Autosave.h"
#include "ui/FloatingPanel.h"
#include "ui/PrintLayout.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/GlyphIcon.h"
#include "ui/ImageTabStrip.h"
#include "ui/ToolOptionsBar.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/EffectDialogs.h"
#include "ui/panels/Panels.h"
#include "ui/theme/Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <cmath>

namespace pane {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Pane"));
    setAcceptDrops(true);
    resize(1280, 820);

    m_host = new FloatingPanelHost(this);
    m_canvas = new CanvasView(m_host);
    m_host->setCentral(m_canvas);
    setCentralWidget(m_host);

    createActions();
    createMenus();
    createToolBars();
    createPanels();
    createStatusBar();
    captureDefaultShortcutsAndApplyOverrides();

    m_autosave = new AutosaveManager(
        [this] {
            std::vector<Workspace *> v;
            for (auto &ws : m_workspaces) v.push_back(ws.get());
            return v;
        },
        this);
    {
        QSettings as;
        m_autosave->setIntervalMinutes(as.value(QStringLiteral("Autosave/minutes"), 5).toInt());
        m_autosave->setEnabled(as.value(QStringLiteral("Autosave/enabled"), true).toBool());
    }

    connect(m_canvas, &CanvasView::interactionChanged, m_host, &FloatingPanelHost::setCanvasInteracting);
    connect(m_canvas, &CanvasView::filesDropped, this, &MainWindow::openFiles);
    connect(m_canvas, &CanvasView::zoomChanged, this, &MainWindow::updateZoomWidgets);
    connect(m_canvas, &CanvasView::cursorMoved, this, [this](QPoint p, bool inside) {
        m_cursorLabel->setText(inside ? QStringLiteral("%1, %2").arg(formatLength(p.x(), true), formatLength(p.y(), false))
                                      : QString());
    });

    AppState &app = AppState::instance();
    connect(&app, &AppState::activeToolChanged, this, &MainWindow::setActiveTool);

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(300);
    connect(&m_thumbTimer, &QTimer::timeout, this, &MainWindow::updateTabs);

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("Window/geometry")).toByteArray());
    bool rulers = s.value(QStringLiteral("View/rulers"), false).toBool();
    bool grid = s.value(QStringLiteral("View/pixelGrid"), false).toBool();
    action(QStringLiteral("view.rulers"))->setChecked(rulers);
    action(QStringLiteral("view.grid"))->setChecked(grid);
    m_unitsCombo->setCurrentIndex(s.value(QStringLiteral("View/units"), 0).toInt());

    setActiveTool(QStringLiteral("Paintbrush"));
    updateActions();
    updateTitle();
}

MainWindow::~MainWindow()
{
    m_canvas->setWorkspace(nullptr);
}

QAction *MainWindow::action(const QString &id) const
{
    auto it = m_actions.find(id);
    return it == m_actions.end() ? nullptr : it->second;
}

QAction *MainWindow::addAction(const QString &id, const QString &text, const QString &glyph, const QKeySequence &shortcut,
                               std::function<void()> fn)
{
    auto *a = new QAction(text, this);
    if (!glyph.isEmpty()) a->setIcon(glyphIcon(glyph));
    if (!shortcut.isEmpty()) a->setShortcut(shortcut);
    a->setShortcutContext(Qt::ApplicationShortcut);
    a->setToolTip(shortcut.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText)));
    connect(a, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    QMainWindow::addAction(a);
    m_actions[id] = a;
    return a;
}

QAction *MainWindow::addDocAction(const QString &id, const QString &text, const QString &glyph, const QKeySequence &shortcut,
                                  std::function<void(Document &)> fn)
{
    return addAction(id, text, glyph, shortcut, [this, fn = std::move(fn)] { runDocCommand(fn); });
}

// ------------------------------------------------------------------ actions

void MainWindow::createActions()
{
    using QK = QKeySequence;
    // File
    addAction("file.new", "&New...", glyphs::NewImage, QK::New, [this] { newImageDialog(); });
    addAction("file.open", "&Open...", glyphs::Open, QK::Open, [this] { openDialog(); });
    addAction("file.save", "&Save", glyphs::Save, QK::Save, [this] { if (current()) save(*current(), false); });
    addAction("file.saveAs", "Save &As...", glyphs::Save, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] { if (current()) save(*current(), true); });
    addAction("file.saveAll", "Save A&ll", glyphs::SaveAll, QK(Qt::CTRL | Qt::ALT | Qt::Key_S), [this] {
        for (auto &ws : m_workspaces)
            if (ws->doc->isDirty() && !save(*ws, false)) break;
    });
    addAction("file.close", "&Close", glyphs::Close, QK(Qt::CTRL | Qt::Key_W), [this] { if (m_current >= 0) closeWorkspace(m_current); });
    addAction("file.closeAll", "Close All", QString(), QK(Qt::CTRL | Qt::SHIFT | Qt::Key_W), [this] { closeAll(); });
    addAction("file.print", "&Print...", glyphs::Print, QK::Print, [this] { printImage(); });
    addAction("app.settings", "&Settings...", glyphs::Settings, QK(Qt::CTRL | Qt::Key_Comma), [this] { showSettings(); });
    addAction("file.exit", "E&xit", QString(), QK(Qt::CTRL | Qt::Key_Q), [this] { close(); });

    // Edit
    addAction("edit.undo", "&Undo", glyphs::Undo, QK(Qt::CTRL | Qt::Key_Z), [this] {
        if (!current()) return;
        commitTool();
        current()->doc->history().undo();
    });
    addAction("edit.redo", "&Redo", glyphs::Redo, QK(Qt::CTRL | Qt::Key_Y), [this] {
        if (!current()) return;
        commitTool();
        current()->doc->history().redo();
    });
    if (auto *redo = action("edit.redo")) redo->setShortcuts({QK(Qt::CTRL | Qt::Key_Y), QK(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    addAction("edit.cut", "Cu&t", glyphs::Cut, QK::Cut, [this] { cut(); });
    addAction("edit.copy", "&Copy", glyphs::Copy, QK::Copy, [this] { copy(false); });
    addAction("edit.copyMerged", "Copy &Merged", glyphs::Copy, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] { copy(true); });
    addAction("edit.paste", "&Paste", glyphs::Paste, QK::Paste, [this] { paste(0); });
    addAction("edit.pasteNewLayer", "Paste into New &Layer", glyphs::Paste, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_V), [this] { paste(1); });
    addAction("edit.pasteNewImage", "Paste into New &Image", glyphs::Paste, QK(Qt::CTRL | Qt::ALT | Qt::Key_V), [this] { paste(2); });
    addDocAction("edit.selectAll", "Select &All", glyphs::SelectAll, QK(Qt::CTRL | Qt::Key_A), commands::selectAll);
    addDocAction("edit.deselect", "&Deselect All", glyphs::Deselect, QK(Qt::CTRL | Qt::Key_D), commands::deselect);
    addDocAction("edit.invertSelection", "&Invert Selection", glyphs::InvertSelection, QK(Qt::CTRL | Qt::Key_I), commands::invertSelection);
    addDocAction("edit.eraseSelection", "&Erase Selection", glyphs::Eraser, QK(Qt::Key_Delete), commands::eraseSelection);
    addDocAction("edit.fillSelection", "&Fill Selection", glyphs::Fill, QK(Qt::Key_Backspace),
                 [](Document &d) { commands::fillSelection(d, AppState::instance().primary()); });
    addDocAction("edit.crop", "Crop to Selection", glyphs::Crop, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_X), commands::cropToSelection);

    // View
    addAction("view.zoomIn", "Zoom &In", glyphs::ZoomIn, QK(Qt::CTRL | Qt::Key_Plus), [this] { m_canvas->stepZoom(1); });
    if (auto *zi = action("view.zoomIn")) zi->setShortcuts({QK(Qt::CTRL | Qt::Key_Plus), QK(Qt::CTRL | Qt::Key_Equal)});
    addAction("view.zoomOut", "Zoom &Out", glyphs::ZoomOut, QK(Qt::CTRL | Qt::Key_Minus), [this] { m_canvas->stepZoom(-1); });
    addAction("view.zoomWindow", "Zoom to &Window", glyphs::ZoomFit, QK(Qt::CTRL | Qt::Key_B), [this] { m_canvas->zoomToWindow(); });
    addAction("view.zoomSelection", "Zoom to &Selection", QString(), QK(Qt::CTRL | Qt::SHIFT | Qt::Key_B), [this] { m_canvas->zoomToSelection(); });
    addAction("view.actualSize", "&Actual Size", QString(), QK(Qt::CTRL | Qt::Key_0), [this] { m_canvas->actualSize(); });
    auto *grid = addAction("view.grid", "Pixel &Grid", glyphs::Grid, QK(Qt::CTRL | Qt::Key_Apostrophe), [] {});
    grid->setCheckable(true);
    connect(grid, &QAction::toggled, this, [this](bool on) { m_canvas->setPixelGrid(on); });
    auto *rulers = addAction("view.rulers", "&Rulers", glyphs::Ruler, QK(Qt::CTRL | Qt::ALT | Qt::Key_R), [] {});
    rulers->setCheckable(true);
    connect(rulers, &QAction::toggled, this, [this](bool on) { m_canvas->setRulersVisible(on); });
    auto *transl = addAction("view.translucent", "&Translucent Panels", QString(), {}, [] {});
    transl->setCheckable(true);
    connect(transl, &QAction::toggled, this, [this](bool on) { m_host->setTranslucencyEnabled(on); });

    // Image
    addAction("image.resize", "&Resize...", glyphs::Resize, QK(Qt::CTRL | Qt::Key_R), [this] { resizeImage(); });
    addAction("image.canvasSize", "Canvas &Size...", glyphs::CanvasSize, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_R), [this] { canvasSize(); });
    addDocAction("image.flipH", "Flip &Horizontal", glyphs::FlipH, {}, [](Document &d) { commands::flipImage(d, Qt::Horizontal); });
    addDocAction("image.flipV", "Flip &Vertical", glyphs::FlipV, {}, [](Document &d) { commands::flipImage(d, Qt::Vertical); });
    addDocAction("image.rotateCW", "Rotate 90° &Clockwise", glyphs::RotateRight, QK(Qt::CTRL | Qt::Key_H), [](Document &d) { commands::rotateImage(d, 90); });
    addDocAction("image.rotateCCW", "Rotate 90° Co&unter-clockwise", glyphs::RotateLeft, QK(Qt::CTRL | Qt::Key_G), [](Document &d) { commands::rotateImage(d, 270); });
    addDocAction("image.rotate180", "Rotate &180°", glyphs::RotateRight, QK(Qt::CTRL | Qt::Key_J), [](Document &d) { commands::rotateImage(d, 180); });
    addDocAction("image.flatten", "&Flatten", glyphs::Flatten, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_F), commands::flatten);

    // Layers
    addDocAction("layer.add", "&Add New Layer", glyphs::LayerAdd, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_N), commands::addNewLayer);
    addDocAction("layer.delete", "&Delete Layer", glyphs::Delete, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete), commands::deleteLayer);
    addDocAction("layer.duplicate", "D&uplicate Layer", glyphs::Duplicate, QK(Qt::CTRL | Qt::SHIFT | Qt::Key_D), commands::duplicateLayer);
    addDocAction("layer.mergeDown", "&Merge Layer Down", glyphs::Merge, QK(Qt::CTRL | Qt::Key_M), commands::mergeLayerDown);
    addAction("layer.import", "&Import from File...", glyphs::Open, {}, [this] { importFromFile(); });
    addDocAction("layer.flipH", "Flip &Horizontal", glyphs::FlipH, {}, [](Document &d) { commands::flipLayer(d, Qt::Horizontal); });
    addDocAction("layer.flipV", "Flip &Vertical", glyphs::FlipV, {}, [](Document &d) { commands::flipLayer(d, Qt::Vertical); });
    addDocAction("layer.rotate180", "&Rotate 180°", glyphs::RotateRight, {}, commands::rotateLayer180);
    addAction("layer.properties", "Layer &Properties...", glyphs::Properties, QK(Qt::Key_F4), [this] { layerProperties(); });
    addDocAction("layer.moveUp", "Move Layer &Up", glyphs::ArrowUp, QK(Qt::ALT | Qt::Key_PageUp), commands::moveLayerUp);
    addDocAction("layer.moveDown", "Move Layer D&own", glyphs::ArrowDown, QK(Qt::ALT | Qt::Key_PageDown), commands::moveLayerDown);

    // Window / misc
    addAction("window.next", "Next Image", QString(), QK(Qt::CTRL | Qt::Key_Tab), [this] {
        if (!m_workspaces.empty()) setCurrent((m_current + 1) % workspaceCount());
    });
    addAction("window.prev", "Previous Image", QString(), QK(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), [this] {
        if (!m_workspaces.empty()) setCurrent((m_current - 1 + workspaceCount()) % workspaceCount());
    });
    addAction("colors.swap", "Swap Colors", glyphs::Swap, QK(Qt::Key_X), [] { AppState::instance().swapColors(); });
    addAction("colors.reset", "Reset Colors", glyphs::Reset, QK(Qt::Key_D), [] { AppState::instance().resetColors(); });
    addAction("brush.smaller", "Decrease Brush Width", QString(), QK(Qt::Key_BracketLeft), [] {
        auto &a = AppState::instance();
        a.mutableSettings().brushWidth = std::max(1, a.settings().brushWidth - std::max(1, a.settings().brushWidth / 10));
        a.notifySettingsChanged();
    });
    addAction("brush.larger", "Increase Brush Width", QString(), QK(Qt::Key_BracketRight), [] {
        auto &a = AppState::instance();
        a.mutableSettings().brushWidth = std::min(2000, a.settings().brushWidth + std::max(1, a.settings().brushWidth / 10));
        a.notifySettingsChanged();
    });

    rebuildToolShortcuts();
}

void MainWindow::rebuildToolShortcuts()
{
    qDeleteAll(m_toolShortcuts);
    m_toolShortcuts.clear();
    // Single-key tool shortcuts cycle tools that share a key.
    for (QChar key : ToolRegistry::instance().keysInUse()) {
        auto *sc = new QShortcut(QKeySequence(QString(key)), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, key] {
            if (QWidget *f = QApplication::focusWidget(); f && (f->inherits("QLineEdit") || f->inherits("QAbstractSpinBox") || f->inherits("QKeySequenceEdit"))) return;
            if (Tool *t = ToolRegistry::instance().nextForKey(key, AppState::instance().activeToolId())) setActiveTool(t->id());
        });
        m_toolShortcuts << sc;
    }
}

std::vector<MainWindow::ShortcutInfo> MainWindow::shortcutList() const
{
    std::vector<ShortcutInfo> out;
    for (auto &[id, a] : m_actions) {
        if (id.startsWith(QLatin1String("view.units.")) || id == QLatin1String("view.panels")) continue;
        auto def = m_defaultShortcuts.find(id);
        out.push_back({id, QString(a->text()).remove(QLatin1Char('&')).remove(QStringLiteral("...")), a->shortcut(),
                       def != m_defaultShortcuts.end() ? def->second : QKeySequence()});
    }
    std::sort(out.begin(), out.end(), [](const ShortcutInfo &a, const ShortcutInfo &b) { return a.text.localeAwareCompare(b.text) < 0; });
    return out;
}

void MainWindow::setActionShortcut(const QString &id, const QKeySequence &seq)
{
    QAction *a = action(id);
    if (!a) return;
    a->setShortcut(seq);
    QSettings().setValue(QStringLiteral("Shortcuts/") + id, seq.toString(QKeySequence::PortableText));
}

void MainWindow::resetActionShortcut(const QString &id)
{
    QAction *a = action(id);
    auto it = m_defaultShortcutLists.find(id);
    if (!a || it == m_defaultShortcutLists.end()) return;
    a->setShortcuts(it->second);
    QSettings().remove(QStringLiteral("Shortcuts/") + id);
}

void MainWindow::captureDefaultShortcutsAndApplyOverrides()
{
    QSettings s;
    for (auto &[id, a] : m_actions) {
        m_defaultShortcuts[id] = a->shortcut();
        m_defaultShortcutLists[id] = a->shortcuts();
        const QString key = QStringLiteral("Shortcuts/") + id;
        if (s.contains(key)) a->setShortcut(QKeySequence(s.value(key).toString(), QKeySequence::PortableText));
    }
}

void MainWindow::createMenus()
{
    // The menu row: menu bar + image tabs + settings/help (paint.net 5 layout).
    auto *row = new QWidget(this);
    row->setObjectName(QStringLiteral("MenuRow"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 4, 0);
    h->setSpacing(4);
    auto *bar = new QMenuBar(row);
    bar->setNativeMenuBar(false);
    h->addWidget(bar, 0, Qt::AlignVCenter);
    m_tabs = new ImageTabStrip(row);
    h->addWidget(m_tabs, 1);
    auto mkButton = [&](const QString &glyph, const QString &tip) {
        auto *b = new QToolButton(row);
        b->setText(glyph);
        b->setFont(ThemeEngine::instance().glyphFont(18));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setPopupMode(QToolButton::InstantPopup);
        h->addWidget(b);
        return b;
    };
    auto *settingsBtn = mkButton(glyphs::Settings, QStringLiteral("Settings"));
    auto *helpBtn = mkButton(glyphs::Help, QStringLiteral("Help"));
    setMenuWidget(row);

    auto a = [this](const char *id) { return action(QString::fromLatin1(id)); };

    QMenu *file = bar->addMenu(QStringLiteral("&File"));
    file->addAction(a("file.new"));
    file->addAction(a("file.open"));
    m_recentMenu = file->addMenu(QStringLiteral("Open &Recent"));
    file->addSeparator();
    file->addAction(a("file.save"));
    file->addAction(a("file.saveAs"));
    file->addAction(a("file.saveAll"));
    file->addSeparator();
    file->addAction(a("file.close"));
    file->addAction(a("file.closeAll"));
    file->addSeparator();
    file->addAction(a("file.print"));
    file->addSeparator();
    file->addAction(a("file.exit"));
    rebuildRecentMenu();

    QMenu *edit = bar->addMenu(QStringLiteral("&Edit"));
    for (const char *id : {"edit.undo", "edit.redo", "", "edit.cut", "edit.copy", "edit.copyMerged", "edit.paste",
                           "edit.pasteNewLayer", "edit.pasteNewImage", "", "edit.selectAll", "edit.deselect", "",
                           "edit.eraseSelection", "edit.fillSelection", "edit.invertSelection", "", "edit.crop"}) {
        if (!*id) edit->addSeparator();
        else edit->addAction(a(id));
    }

    QMenu *view = bar->addMenu(QStringLiteral("&View"));
    for (const char *id : {"view.zoomIn", "view.zoomOut", "view.zoomWindow", "view.zoomSelection", "view.actualSize", "",
                           "view.grid", "view.rulers"}) {
        if (!*id) view->addSeparator();
        else view->addAction(a(id));
    }
    QMenu *units = view->addMenu(QStringLiteral("&Units"));
    auto *unitGroup = new QActionGroup(units);
    const QStringList unitNames = {QStringLiteral("Pixels"), QStringLiteral("Inches"), QStringLiteral("Centimeters")};
    for (int i = 0; i < unitNames.size(); ++i) {
        QAction *ua = units->addAction(unitNames[i]);
        ua->setCheckable(true);
        unitGroup->addAction(ua);
        connect(ua, &QAction::triggered, this, [this, i] { m_unitsCombo->setCurrentIndex(i); });
        m_actions[QStringLiteral("view.units.%1").arg(i)] = ua;
    }
    view->addSeparator();
    m_themeMenu = view->addMenu(QStringLiteral("&Theme"));
    rebuildThemeMenu();
    QMenu *panels = view->addMenu(QStringLiteral("&Panels"));
    m_actions["view.panels"] = panels->menuAction();
    view->addAction(a("view.translucent"));

    QMenu *image = bar->addMenu(QStringLiteral("&Image"));
    for (const char *id : {"edit.crop", "image.resize", "image.canvasSize", "", "image.flipH", "image.flipV", "",
                           "image.rotateCW", "image.rotateCCW", "image.rotate180", "", "image.flatten"}) {
        if (!*id) image->addSeparator();
        else image->addAction(a(id));
    }

    QMenu *layers = bar->addMenu(QStringLiteral("&Layers"));
    for (const char *id : {"layer.add", "layer.delete", "layer.duplicate", "layer.mergeDown", "layer.import", "",
                           "layer.flipH", "layer.flipV", "layer.rotate180", "", "layer.properties", "", "layer.moveUp",
                           "layer.moveDown"}) {
        if (!*id) layers->addSeparator();
        else layers->addAction(a(id));
    }

    QMenu *adjust = bar->addMenu(QStringLiteral("&Adjustments"));
    QMenu *effects = bar->addMenu(QStringLiteral("Effe&cts"));
    QAction *repeatAct = addAction(QStringLiteral("effect.repeat"), QStringLiteral("Repeat"), glyphs::RepeatFx,
                                   QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
                                       if (m_lastEffect) runEffect(*m_lastEffect, true);
                                   });
    repeatAct->setEnabled(false);
    effects->addAction(repeatAct);
    effects->addSeparator();
    std::map<QString, QMenu *> effectMenus;
    for (const char *name : {"Artistic", "Blurs", "Distort", "Noise", "Photo", "Render", "Stylize"})
        effectMenus[QString::fromUtf8(name)] = effects->addMenu(QString::fromUtf8(name));
    for (const Effect *fx : EffectRegistry::instance().effects()) {
        QString label = fx->name() + (fx->ui() == Effect::UI::None ? QString() : QStringLiteral("..."));
        QAction *ea = addAction(QStringLiteral("effect.") + fx->id(), label, fx->glyph(), fx->shortcut(), [this, fx] { runEffect(*fx); });
        if (fx->category() == QLatin1String("Adjustments")) {
            adjust->addAction(ea);
        } else {
            QMenu *&m = effectMenus[fx->category()];
            if (!m) m = effects->addMenu(fx->category());
            m->addAction(ea);
        }
    }
    for (auto &[name, menu] : effectMenus)
        if (menu->actions().isEmpty()) menu->addAction(QStringLiteral("Coming in M4"))->setEnabled(false);

    // Settings button: theme menu (full settings dialog arrives in M6).
    auto *settingsMenu = new QMenu(settingsBtn);
    settingsMenu->addAction(a("app.settings"));
    settingsMenu->addSeparator();
    settingsMenu->addMenu(m_themeMenu);
    settingsMenu->addAction(a("view.translucent"));
    settingsBtn->setMenu(settingsMenu);
    auto *helpMenu = new QMenu(helpBtn);
    helpMenu->addAction(QStringLiteral("About Pane"), this, [this] {
        QMessageBox::about(this, QStringLiteral("About Pane"),
                           QStringLiteral("<b>Pane</b> %1<br>An image editor for Linux in the style of paint.net.<br><br>"
                                          "<b>Written by Claude</b>, Anthropic's AI model, using Claude Code.<br>"
                                          "Directed and tested by Loqor.<br><br>"
                                          "Licensed under the GNU LGPL v3.0.<br>"
                                          "<a href=\"https://github.com/Loqor/pane\">github.com/Loqor/pane</a><br><br>"
                                          "Icons: Symbols Nerd Font (Material Design Icons).")
                               .arg(QApplication::applicationVersion()));
    });
    helpBtn->setMenu(helpMenu);

    connect(m_tabs, &ImageTabStrip::currentChanged, this, &MainWindow::setCurrent);
    connect(m_tabs, &ImageTabStrip::closeRequested, this, [this](int i) { closeWorkspace(i); });
    connect(m_tabs, &ImageTabStrip::moved, this, [this](int from, int to) {
        auto ws = std::move(m_workspaces[size_t(from)]);
        m_workspaces.erase(m_workspaces.begin() + from);
        m_workspaces.insert(m_workspaces.begin() + to, std::move(ws));
        if (m_current == from) m_current = to;
        else if (from < m_current && to >= m_current) --m_current;
        else if (from > m_current && to <= m_current) ++m_current;
        updateTabs();
    });
}

void MainWindow::rebuildThemeMenu()
{
    m_themeMenu->clear();
    ThemeEngine &te = ThemeEngine::instance();
    auto *group = new QActionGroup(m_themeMenu);
    for (const QString &id : te.themeIds()) {
        const Theme *t = te.theme(id);
        QAction *ta = m_themeMenu->addAction(t->name);
        ta->setCheckable(true);
        ta->setChecked(id == te.currentId());
        group->addAction(ta);
        connect(ta, &QAction::triggered, this, [this, id] { applyTheme(id, {}); });
    }
    m_themeMenu->addSeparator();
    QMenu *accent = m_themeMenu->addMenu(QStringLiteral("&Accent Color"));
    const Theme &cur = te.current();
    if (cur.catppuccin) {
        auto *ag = new QActionGroup(accent);
        for (const QString &name : ThemeEngine::catppuccinAccentNames()) {
            QPixmap swatch(14, 14);
            swatch.fill(cur.accents.at(name));
            QString label = name;
            label[0] = label[0].toUpper();
            QAction *aa = accent->addAction(QIcon(swatch), label);
            aa->setCheckable(true);
            aa->setChecked(cur.accent == cur.accents.at(name));
            ag->addAction(aa);
            connect(aa, &QAction::triggered, this, [this, name] { applyTheme(ThemeEngine::instance().currentId(), name); });
        }
    } else {
        const std::pair<const char *, const char *> presets[] = {{"Blue", nullptr}, {"Teal", "#00897b"}, {"Purple", "#7b5cd6"},
                                                                 {"Pink", "#d6336c"}, {"Orange", "#e8590c"}, {"Green", "#2f9e44"}};
        for (auto &[label, hex] : presets) {
            QString value = hex ? QString::fromLatin1(hex) : QString();
            QPixmap swatch(14, 14);
            swatch.fill(hex ? QColor(QString::fromLatin1(hex)) : te.theme(cur.id)->accent);
            QAction *aa = accent->addAction(QIcon(swatch), QString::fromLatin1(label));
            connect(aa, &QAction::triggered, this, [this, value] { applyTheme(ThemeEngine::instance().currentId(), value); });
        }
    }
    m_themeMenu->addSeparator();
    m_themeMenu->addAction(QStringLiteral("Reload Custom Themes"), this, [this] {
        ThemeEngine::instance().loadCustomThemes();
        rebuildThemeMenu();
    });
}

void MainWindow::applyTheme(const QString &id, const QString &accent)
{
    ThemeEngine::instance().apply(id, accent);
    QSettings s;
    s.setValue(QStringLiteral("Theme/id"), id);
    s.setValue(QStringLiteral("Theme/accent"), accent);
    rebuildThemeMenu();
}

void MainWindow::createToolBars()
{
    auto *tb = new QToolBar(QStringLiteral("Common Actions"), this);
    tb->setObjectName(QStringLiteral("CommonActions"));
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    auto a = [this](const char *id) { return action(QString::fromLatin1(id)); };
    for (const char *id : {"file.new", "file.open", "file.save", "file.print", "", "edit.cut", "edit.copy", "edit.paste", "",
                           "edit.crop", "edit.deselect", "", "edit.undo", "edit.redo", "", "view.grid", "view.rulers"}) {
        if (!*id) tb->addSeparator();
        else tb->addAction(a(id));
    }
    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" Units: "), tb));
    m_unitsCombo = new QComboBox(tb);
    m_unitsCombo->addItems({QStringLiteral("Pixels"), QStringLiteral("Inches"), QStringLiteral("Centimeters")});
    m_unitsCombo->setFocusPolicy(Qt::ClickFocus);
    tb->addWidget(m_unitsCombo);
    connect(m_unitsCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_canvas->setUnits(Units(i));
        if (QAction *ua = action(QStringLiteral("view.units.%1").arg(i))) ua->setChecked(true);
        updateStatusSelection();
        QSettings().setValue(QStringLiteral("View/units"), i);
        if (current()) m_sizeLabel->setText(QStringLiteral("%1 × %2").arg(formatLength(current()->doc->width(), true),
                                                                            formatLength(current()->doc->height(), false)));
    });
    tb->addWidget(new QLabel(QStringLiteral("  Zoom: "), tb));
    m_zoomCombo = new QComboBox(tb);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setFocusPolicy(Qt::ClickFocus);
    m_zoomCombo->addItem(QStringLiteral("Window"));
    const auto &presets = CanvasView::zoomPresets();
    for (auto it = presets.rbegin(); it != presets.rend(); ++it) m_zoomCombo->addItem(QStringLiteral("%1%").arg(std::lround(*it * 100)));
    m_zoomCombo->setMinimumWidth(80);
    tb->addWidget(m_zoomCombo);
    connect(m_zoomCombo, &QComboBox::textActivated, this, [this](const QString &t) {
        if (t == QLatin1String("Window")) return m_canvas->zoomToWindow();
        bool ok;
        double v = QString(t).remove('%').trimmed().toDouble(&ok);
        if (ok && v > 0) m_canvas->setZoom(v / 100.0);
    });
    addToolBar(Qt::TopToolBarArea, tb);
    addToolBarBreak();

    m_optionsBar = new ToolOptionsBar(this);
    addToolBar(Qt::TopToolBarArea, m_optionsBar);
    connect(m_optionsBar, &ToolOptionsBar::toolPicked, this, &MainWindow::setActiveTool);
}

void MainWindow::createPanels()
{
    m_tools = new ToolsPanel;
    m_toolsPanel = new FloatingPanel(QStringLiteral("tools"), QStringLiteral("Tools"), glyphs::Brush, m_host, false);
    m_toolsPanel->setContent(m_tools);
    connect(m_tools, &ToolsPanel::toolSelected, this, &MainWindow::setActiveTool);

    m_history = new HistoryPanel;
    m_historyPanel = new FloatingPanel(QStringLiteral("history"), QStringLiteral("History"), glyphs::History, m_host, true);
    m_historyPanel->setContent(m_history);
    connect(m_history, &HistoryPanel::beforeHistoryJump, this, &MainWindow::commitTool);

    m_layers = new LayersPanel;
    m_layersPanel = new FloatingPanel(QStringLiteral("layers"), QStringLiteral("Layers"), glyphs::Layers, m_host, true);
    m_layersPanel->setContent(m_layers);
    connect(m_layers->list(), &LayerList::beforeChange, this, &MainWindow::commitTool);
    connect(m_layers, &LayersPanel::actionRequested, this, [this](const QString &act) {
        static const std::map<QString, QString> map = {
            {"add", "layer.add"}, {"delete", "layer.delete"}, {"duplicate", "layer.duplicate"},
            {"mergeDown", "layer.mergeDown"}, {"moveUp", "layer.moveUp"}, {"moveDown", "layer.moveDown"},
            {"properties", "layer.properties"}};
        auto it = map.find(act);
        if (it != map.end()) action(it->second)->trigger();
    });

    m_colors = new ColorsPanel;
    m_colorsPanel = new FloatingPanel(QStringLiteral("colors"), QStringLiteral("Colors"), glyphs::Palette, m_host, false);
    m_colorsPanel->setContent(m_colors);

    QMenu *panelsMenu = action(QStringLiteral("view.panels"))->menu();
    const std::tuple<FloatingPanel *, const char *, QKeySequence> entries[] = {
        {m_toolsPanel, "&Tools", QKeySequence(Qt::Key_F5)},
        {m_historyPanel, "&History", QKeySequence(Qt::Key_F6)},
        {m_layersPanel, "&Layers", QKeySequence(Qt::Key_F7)},
        {m_colorsPanel, "&Colors", QKeySequence(Qt::Key_F8)},
    };
    for (auto &[panel, name, key] : entries) {
        m_host->addPanel(panel);
        QAction *pa = panelsMenu->addAction(QString::fromUtf8(name));
        m_actions[QStringLiteral("panel.") + panel->id()] = pa;
        pa->setCheckable(true);
        pa->setShortcut(key);
        pa->setShortcutContext(Qt::ApplicationShortcut);
        QMainWindow::addAction(pa);
        FloatingPanel *p = panel;
        connect(pa, &QAction::toggled, p, [p](bool on) {
            p->setVisible(on);
            if (on) p->raise();
        });
        connect(p, &FloatingPanel::visibilityChanged, pa, [pa](bool v) {
            QSignalBlocker b(pa);
            pa->setChecked(v);
        });
    }
    panelsMenu->addSeparator();
    panelsMenu->addAction(QStringLiteral("&Reset Panel Positions"), m_host, &FloatingPanelHost::resetPositions);
    action(QStringLiteral("view.translucent"))->setChecked(QSettings().value(QStringLiteral("Panels/translucent"), true).toBool());
}

void MainWindow::createStatusBar()
{
    QStatusBar *sb = statusBar();
    sb->setSizeGripEnabled(false);
    m_hintGlyph = new QLabel(sb);
    m_hintGlyph->setFont(ThemeEngine::instance().glyphFont(15));
    m_hint = new QLabel(sb);
    m_hint->setMinimumWidth(100);
    sb->addWidget(m_hintGlyph);
    sb->addWidget(m_hint, 1);
    auto glyphLabel = [sb](const QString &g) {
        auto *l = new QLabel(g, sb);
        l->setFont(ThemeEngine::instance().glyphFont(14));
        return l;
    };
    m_sizeLabel = new QLabel(sb);
    m_cursorLabel = new QLabel(sb);
    m_selLabel = new QLabel(sb);
    m_cursorLabel->setMinimumWidth(90);
    sb->addPermanentWidget(glyphLabel(glyphs::Image));
    sb->addPermanentWidget(m_sizeLabel);
    sb->addPermanentWidget(glyphLabel(glyphs::MoveSelected));
    sb->addPermanentWidget(m_cursorLabel);
    sb->addPermanentWidget(glyphLabel(glyphs::RectSelect));
    sb->addPermanentWidget(m_selLabel);
    m_zoomSlider = new QSlider(Qt::Horizontal, sb);
    m_zoomSlider->setRange(0, int(CanvasView::zoomPresets().size()) - 1);
    m_zoomSlider->setFixedWidth(120);
    m_zoomSlider->setFocusPolicy(Qt::NoFocus);
    m_zoomLabel = new QLabel(QStringLiteral("100%"), sb);
    m_zoomLabel->setMinimumWidth(48);
    sb->addPermanentWidget(m_zoomSlider);
    sb->addPermanentWidget(m_zoomLabel);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int i) {
        double z = CanvasView::zoomPresets()[size_t(i)];
        if (current() && std::abs(current()->zoom - z) > 1e-6) m_canvas->setZoom(z);
    });
    connect(&AppState::instance(), &AppState::hintChanged, this, [this](const QString &g, const QString &t) {
        m_hintGlyph->setText(g);
        m_hint->setText(t);
    });
}

void MainWindow::updateZoomWidgets(double zoom)
{
    QString text = QStringLiteral("%1%").arg(zoom * 100 >= 10 ? QString::number(std::lround(zoom * 100)) : QString::number(zoom * 100, 'g', 2));
    m_zoomLabel->setText(text);
    m_zoomCombo->setEditText(text);
    const auto &p = CanvasView::zoomPresets();
    int best = 0;
    for (size_t i = 0; i < p.size(); ++i)
        if (std::abs(std::log(p[i] / zoom)) < std::abs(std::log(p[size_t(best)] / zoom))) best = int(i);
    QSignalBlocker b(m_zoomSlider);
    m_zoomSlider->setValue(best);
}

QString MainWindow::formatLength(double px, bool horizontal) const
{
    Workspace *ws = current();
    double dpi = ws ? (horizontal ? ws->doc->dpiX() : ws->doc->dpiY()) : 96;
    switch (Units(m_unitsCombo ? m_unitsCombo->currentIndex() : 0)) {
    case Units::Pixels: return QString::number(std::lround(px));
    case Units::Inches: return QString::number(px / dpi, 'f', 2);
    case Units::Centimeters: return QString::number(px / dpi * 2.54, 'f', 2);
    }
    return {};
}

// ------------------------------------------------------------------ tools

void MainWindow::setActiveTool(const QString &id)
{
    Tool *t = ToolRegistry::instance().byId(id);
    if (!t || !t->enabled()) return;
    if (m_canvas->tool() == t) {
        m_tools->setActiveTool(id);
        return;
    }
    commitTool();
    AppState::instance().setActiveToolId(id);
    if (current()) m_canvas->setTool(t);
    else m_canvas->setTool(t);
    m_optionsBar->setTool(t);
    m_tools->setActiveTool(id);
    emit AppState::instance().hintChanged(t->glyph(), t->name() + QStringLiteral(": ") + t->hint());
}

void MainWindow::commitTool()
{
    if (Tool *t = m_canvas->tool(); t && current()) t->commit();
}

void MainWindow::runDocCommand(const std::function<void(Document &)> &fn)
{
    if (!current()) return;
    commitTool();
    try {
        fn(*current()->doc);
    } catch (const std::bad_alloc &) {
        showError(QStringLiteral("Not Enough Memory"), QStringLiteral("There is not enough memory to complete this operation."));
    } catch (const std::exception &e) {
        showError(QStringLiteral("Error"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::showError(const QString &title, const QString &message)
{
    QMessageBox::critical(this, title, message);
}

// ------------------------------------------------------------------ documents

void MainWindow::addWorkspace(std::unique_ptr<Document> doc)
{
    auto ws = std::make_unique<Workspace>(std::move(doc));
    Document *d = ws->doc.get();
    d->history().beforeChange = [this] { commitTool(); };
    connect(ws->compositor.get(), &Compositor::updated, this, [this] {
        if (!m_thumbTimer.isActive()) m_thumbTimer.start();
    });
    connect(d, &Document::dirtyChanged, this, [this] {
        updateTitle();
        updateTabs();
    });
    m_workspaces.push_back(std::move(ws));
    setCurrent(workspaceCount() - 1);
}

void MainWindow::setCurrent(int index)
{
    if (index == m_current && index >= 0) return;
    commitTool();
    for (auto &c : m_docConns) disconnect(c);
    m_docConns.clear();
    m_current = index;
    Workspace *ws = current();
    m_canvas->setWorkspace(ws);
    m_history->setWorkspace(ws);
    m_layers->setWorkspace(ws);
    if (ws) {
        Document *d = ws->doc.get();
        m_docConns << connect(d, &Document::selectionChanged, this, &MainWindow::updateStatusSelection);
        m_docConns << connect(&d->history(), &History::changed, this, &MainWindow::updateActions);
        m_docConns << connect(d, &Document::layersChanged, this, &MainWindow::updateActions);
        m_docConns << connect(d, &Document::activeLayerChanged, this, &MainWindow::updateActions);
        m_docConns << connect(d, &Document::sizeChanged, this, [this] {
            m_sizeLabel->setText(QStringLiteral("%1 × %2").arg(formatLength(current()->doc->width(), true),
                                                                formatLength(current()->doc->height(), false)));
        });
        m_sizeLabel->setText(QStringLiteral("%1 × %2").arg(formatLength(d->width(), true), formatLength(d->height(), false)));
    } else {
        m_sizeLabel->clear();
    }
    updateStatusSelection();
    updateActions();
    updateTitle();
    updateTabs();
}

void MainWindow::newImage(QSize size, double dpi)
{
    try {
        auto doc = Document::createBlank(size, AppState::instance().secondary() | 0xFF000000u);
        doc->setDpi(dpi, dpi);
        addWorkspace(std::move(doc));
    } catch (const std::exception &) {
        showError(QStringLiteral("Not Enough Memory"), QStringLiteral("There is not enough memory to create an image of this size."));
    }
}

void MainWindow::newImageDialog()
{
    QSize suggested(800, 600);
    if (QImage clip = QApplication::clipboard()->image(); !clip.isNull()) suggested = clip.size();
    else if (current()) suggested = current()->doc->size();
    NewImageDialog dlg(suggested, current() ? current()->doc->dpiX() : 96, this);
    if (dlg.exec() == QDialog::Accepted) newImage(dlg.imageSize(), dlg.dpi());
}

void MainWindow::openDialog()
{
    QSettings s;
    QString dir = s.value(QStringLiteral("Files/lastDir"), QDir::homePath()).toString();
    QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Open"), dir, filetypes::openFilter());
    if (files.isEmpty()) return;
    s.setValue(QStringLiteral("Files/lastDir"), QFileInfo(files.first()).absolutePath());
    openFiles(files);
}

void MainWindow::openFiles(const QStringList &paths)
{
    for (const QString &p : paths) {
        QString abs = QFileInfo(p).absoluteFilePath();
        // Already open? Switch to it.
        bool found = false;
        for (int i = 0; i < workspaceCount(); ++i)
            if (m_workspaces[size_t(i)]->doc->filePath() == abs) {
                setCurrent(i);
                found = true;
            }
        if (found) continue;
        try {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            auto doc = filetypes::load(abs);
            QApplication::restoreOverrideCursor();
            addWorkspace(std::move(doc));
            addRecent(abs);
        } catch (const FileError &e) {
            QApplication::restoreOverrideCursor();
            showError(QStringLiteral("Could Not Open File"), QStringLiteral("%1\n\n%2").arg(abs, QString::fromUtf8(e.what())));
        }
    }
}

bool MainWindow::save(Workspace &ws, bool saveAs)
{
    commitTool();
    Document &doc = *ws.doc;
    const FileType *type = filetypes::byId(doc.fileTypeId());
    QString path = doc.filePath();
    if (saveAs || path.isEmpty() || !type || !type->canSave()) {
        QSettings s;
        QString dir = s.value(QStringLiteral("Files/lastDir"), QDir::homePath()).toString();
        const FileType *def = (type && type->canSave()) ? type : pngFileType();
        QString base = path.isEmpty() ? doc.displayName() : QFileInfo(path).completeBaseName();
        QString suggested = QDir(path.isEmpty() ? dir : QFileInfo(path).absolutePath()).filePath(base + QStringLiteral(".") + def->extensions().first());
        QString filter;
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"), suggested, filetypes::saveFilter(def), &filter);
        if (path.isEmpty()) return false;
        type = filetypes::fromFilter(filter);
        if (const FileType *byExt = filetypes::forPath(path); byExt && byExt->canSave()) type = byExt;
        if (!type) type = def;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".") + type->extensions().first();
        s.setValue(QStringLiteral("Files/lastDir"), QFileInfo(path).absolutePath());
    }
    if (!type->supportsLayers() && doc.layerCount() > 1) {
        auto r = QMessageBox::question(this, QStringLiteral("Flatten Image"),
                                       QStringLiteral("Saving to this file type requires flattening the image.\n\nFlatten the image?"),
                                       QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (r != QMessageBox::Yes) return false;
        commands::flatten(doc);
    }
    SaveOptions opts;
    if (type->hasSaveOptions()) {
        const QString key = QStringLiteral("SaveOptions/") + type->id();
        QVariantMap initial = propertyDefaults(type->saveOptions());
        const QVariantMap stored = QSettings().value(key).toMap();
        for (auto it = stored.begin(); it != stored.end(); ++it) initial[it.key()] = it.value();
        SaveConfigDialog dlg(type, doc, type->supportsLayers() ? QImage() : doc.flattenToImage(), initial, this);
        if (dlg.exec() != QDialog::Accepted) return false;
        opts = dlg.options();
        QSettings().setValue(key, opts);
    }
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        filetypes::save(doc, path, type, opts);
        QApplication::restoreOverrideCursor();
    } catch (const FileError &e) {
        QApplication::restoreOverrideCursor();
        showError(QStringLiteral("Could Not Save File"), QStringLiteral("%1\n\n%2").arg(path, QString::fromUtf8(e.what())));
        return false;
    }
    addRecent(path);
    m_autosave->forget(ws);
    updateTitle();
    updateTabs();
    return true;
}

bool MainWindow::closeWorkspace(int index)
{
    if (index < 0 || index >= workspaceCount()) return true;
    Workspace &ws = *m_workspaces[size_t(index)];
    if (index == m_current) commitTool();
    if (ws.doc->isDirty()) {
        if (index != m_current) setCurrent(index);
        UnsavedChangesDialog dlg({ws.doc->displayName()}, {ws.compositor->thumbnail(96)}, this);
        int r = dlg.exec();
        if (r == UnsavedChangesDialog::CancelAction) return false;
        if (r == UnsavedChangesDialog::Save && !save(ws, false)) return false;
    }
    m_autosave->forget(ws);
    bool wasCurrent = index == m_current;
    if (wasCurrent) {
        m_canvas->setWorkspace(nullptr);
        m_history->setWorkspace(nullptr);
        m_layers->setWorkspace(nullptr);
        m_current = -1;
    } else if (index < m_current) {
        --m_current;
    }
    m_workspaces.erase(m_workspaces.begin() + index);
    if (wasCurrent) {
        int next = std::min(index, workspaceCount() - 1);
        m_current = -2; // force refresh
        setCurrent(next);
    } else {
        updateTabs();
    }
    return true;
}

bool MainWindow::closeAll()
{
    commitTool();
    QStringList names;
    QList<QImage> thumbs;
    for (auto &ws : m_workspaces)
        if (ws->doc->isDirty()) {
            names << ws->doc->displayName();
            thumbs << ws->compositor->thumbnail(96);
        }
    if (!names.isEmpty()) {
        UnsavedChangesDialog dlg(names, thumbs, this);
        int r = dlg.exec();
        if (r == UnsavedChangesDialog::CancelAction) return false;
        if (r == UnsavedChangesDialog::Save) {
            for (int i = 0; i < workspaceCount(); ++i) {
                Workspace &ws = *m_workspaces[size_t(i)];
                if (!ws.doc->isDirty()) continue;
                setCurrent(i);
                if (!save(ws, false)) return false;
            }
        }
    }
    m_canvas->setWorkspace(nullptr);
    m_history->setWorkspace(nullptr);
    m_layers->setWorkspace(nullptr);
    for (auto &ws : m_workspaces) m_autosave->forget(*ws);
    m_workspaces.clear();
    m_current = -2;
    setCurrent(-1);
    return true;
}

void MainWindow::addRecent(const QString &path)
{
    QSettings s;
    QStringList r = s.value(QStringLiteral("Files/recent")).toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 10) r.removeLast();
    s.setValue(QStringLiteral("Files/recent"), r);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    QStringList r = QSettings().value(QStringLiteral("Files/recent")).toStringList();
    for (int i = 0; i < r.size(); ++i) {
        QString path = r[i];
        m_recentMenu->addAction(QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(path).fileName()), this, [this, path] { openFiles({path}); })
            ->setToolTip(path);
    }
    m_recentMenu->setEnabled(!r.isEmpty());
}

// ------------------------------------------------------------------ clipboard

QImage MainWindow::selectionImage(bool merged) const
{
    Workspace *ws = current();
    if (!ws) return {};
    Document &doc = *ws->doc;
    const Selection &sel = doc.selection();
    QRect bounds = sel.boundsOrImage();
    if (bounds.isEmpty()) return {};
    Surface src = merged ? doc.flatten() : doc.activeLayer().surface;
    QImage img = src.copyRect(bounds);
    if (!sel.isEmpty()) {
        for (int idx : src.tilesIn(bounds)) {
            TileCoverage cov = sel.coverage(idx);
            if (cov.kind == TileCoverage::Full) continue;
            QRect tr = src.tileRect(idx);
            QRect part = tr.intersected(bounds);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                auto *row = reinterpret_cast<Pixel *>(img.scanLine(y - bounds.top()));
                for (int x = part.left(); x <= part.right(); ++x) {
                    uint32_t c = cov.kind == TileCoverage::None ? 0 : cov.mask[(y - tr.top()) * TileSize + (x - tr.left())];
                    Pixel &p = row[x - bounds.left()];
                    p = c ? withAlpha(p, mul255(pxA(p), c)) : 0;
                }
            }
        }
    }
    return img;
}

void MainWindow::copy(bool merged)
{
    if (!current()) return;
    commitTool();
    QImage img = selectionImage(merged);
    if (img.isNull()) return;
    QApplication::clipboard()->setImage(img);
}

void MainWindow::cut()
{
    if (!current()) return;
    copy(false);
    commands::eraseSelection(*current()->doc);
    // paint.net records Cut as "Erase Selection" + deselect.
    commands::deselect(*current()->doc);
}

void MainWindow::paste(int mode)
{
    const QMimeData *md = QApplication::clipboard()->mimeData();
    QImage img = QApplication::clipboard()->image();
    if (img.isNull() && md && md->hasUrls() && !md->urls().isEmpty() && md->urls().first().isLocalFile()) {
        img.load(md->urls().first().toLocalFile());
    }
    if (img.isNull()) {
        showError(QStringLiteral("Paste"), QStringLiteral("The clipboard does not contain an image."));
        return;
    }
    img = img.convertToFormat(QImage::Format_ARGB32);
    if (mode == 2 || !current()) {
        try {
            auto doc = Document::createBlank(img.size(), 0);
            addWorkspace(std::move(doc));
        } catch (const std::exception &) {
            return showError(QStringLiteral("Not Enough Memory"), QStringLiteral("There is not enough memory to paste this image."));
        }
    }
    commitTool();
    Document &doc = *current()->doc;
    if (mode == 1) commands::addNewLayer(doc);
    if (img.width() > doc.width() || img.height() > doc.height()) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("Image Larger than Canvas"),
                        QStringLiteral("The image being pasted is larger than the canvas size."), QMessageBox::NoButton, this);
        auto *expand = box.addButton(QStringLiteral("Expand canvas"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Keep canvas size"), QMessageBox::RejectRole);
        auto *cancel = box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == static_cast<QAbstractButton *>(cancel)) return;
        if (box.clickedButton() == static_cast<QAbstractButton *>(expand))
            commands::canvasSize(doc, doc.size().expandedTo(img.size()), 0, AppState::instance().secondary());
    }
    setActiveTool(QStringLiteral("MoveSelectedPixels"));
    auto *move = static_cast<MoveSelectedPixelsTool *>(ToolRegistry::instance().byId(QStringLiteral("MoveSelectedPixels")));
    QPoint origin = mode == 2 ? QPoint() : m_canvas->visibleImageTopLeft();
    if (origin.x() + img.width() > doc.width()) origin.setX(std::max(0, doc.width() - img.width()));
    if (origin.y() + img.height() > doc.height()) origin.setY(std::max(0, doc.height() - img.height()));
    move->beginPaste(img, origin, mode == 1 ? QStringLiteral("Paste into New Layer") : QStringLiteral("Paste"));
}

// ------------------------------------------------------------------ dialogs

void MainWindow::runEffect(const Effect &fx, bool repeat)
{
    if (!current()) return;
    commitTool();
    Document &doc = *current()->doc;
    QSettings s;
    const QString key = QStringLiteral("Effects/") + fx.id();
    QVariantMap params = s.value(key).toMap();
    if (params.isEmpty()) params = fx.defaults();
    try {
        EffectSession session(doc, fx);
        session.setContext({{QStringLiteral("_primary"), AppState::instance().primary()},
                            {QStringLiteral("_secondary"), AppState::instance().secondary()}});
        auto remember = [this, &fx](const QVariantMap &p) {
            m_lastEffect = &fx;
            m_lastEffectParams = p;
            QAction *rep = action(QStringLiteral("effect.repeat"));
            rep->setText(QStringLiteral("Repeat %1").arg(fx.name()));
            rep->setEnabled(true);
        };
        if (fx.ui() == Effect::UI::None || repeat) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            session.update(repeat ? m_lastEffectParams : params);
            session.finish();
            QApplication::restoreOverrideCursor();
            remember(repeat ? m_lastEffectParams : params);
            return;
        }
        std::unique_ptr<EffectDialogBase> dlg;
        if (fx.id() == QLatin1String("Levels")) dlg = std::make_unique<LevelsDialog>(session, params, this);
        else if (fx.id() == QLatin1String("Curves")) dlg = std::make_unique<CurvesDialog>(session, params, this);
        else dlg = std::make_unique<EffectDialog>(session, params, this);
        session.update(dlg->params());
        if (dlg->exec() == QDialog::Accepted) {
            session.update(dlg->params());
            QApplication::setOverrideCursor(Qt::WaitCursor);
            session.finish();
            QApplication::restoreOverrideCursor();
            s.setValue(key, dlg->params());
            remember(dlg->params());
        } else {
            session.cancel();
        }
    } catch (const std::bad_alloc &) {
        QApplication::restoreOverrideCursor();
        showError(QStringLiteral("Not Enough Memory"), QStringLiteral("There is not enough memory to apply this effect."));
    }
}

void MainWindow::layerProperties()
{
    if (!current()) return;
    commitTool();
    Document &doc = *current()->doc;
    int idx = doc.activeLayerIndex();
    LayerPropertiesDialog dlg(doc, idx, this);
    if (dlg.exec() == QDialog::Accepted) {
        LayerProperties after = dlg.result();
        doc.setLayerProperties(idx, dlg.original());
        commands::commitLayerProperties(doc, idx, dlg.original(), after);
    }
}

void MainWindow::importFromFile()
{
    if (!current()) return;
    commitTool();
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import From File"),
                                                QSettings().value(QStringLiteral("Files/lastDir"), QDir::homePath()).toString(),
                                                filetypes::openFilter());
    if (path.isEmpty()) return;
    try {
        auto src = filetypes::load(path);
        Document &doc = *current()->doc;
        QImage img = src->flattenToImage();
        if (img.width() > doc.width() || img.height() > doc.height()) {
            auto r = QMessageBox::question(this, QStringLiteral("Image Larger than Canvas"),
                                           QStringLiteral("The imported image is larger than the canvas. Expand the canvas?"),
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (r == QMessageBox::Cancel) return;
            if (r == QMessageBox::Yes) commands::canvasSize(doc, doc.size().expandedTo(img.size()), 0, AppState::instance().secondary());
        }
        commands::addLayerFromImage(doc, img, QFileInfo(path).completeBaseName(), QStringLiteral("Import From File"));
    } catch (const FileError &e) {
        showError(QStringLiteral("Could Not Import File"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::resizeImage()
{
    if (!current()) return;
    commitTool();
    Document &doc = *current()->doc;
    ResizeImageDialog dlg(doc.size(), doc.dpiX(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    QSize s = dlg.imageSize();
    auto mode = dlg.resampling();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    runDocCommand([s, mode](Document &d) { commands::resizeImage(d, s, mode); });
    QApplication::restoreOverrideCursor();
}

void MainWindow::canvasSize()
{
    if (!current()) return;
    commitTool();
    Document &doc = *current()->doc;
    CanvasSizeDialog dlg(doc.size(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    QSize s = dlg.canvasSize();
    int anchor = dlg.anchor();
    runDocCommand([s, anchor](Document &d) { commands::canvasSize(d, s, anchor, AppState::instance().secondary()); });
}

// ------------------------------------------------------------------ state

void MainWindow::updateActions()
{
    Workspace *ws = current();
    bool has = ws != nullptr;
    for (auto &[id, a] : m_actions) {
        if (id.startsWith(QLatin1String("edit.")) || id.startsWith(QLatin1String("image.")) || id.startsWith(QLatin1String("layer.")) ||
            id.startsWith(QLatin1String("effect.")) ||
            id == QLatin1String("file.save") || id == QLatin1String("file.saveAs") || id == QLatin1String("file.close") ||
            id.startsWith(QLatin1String("view.zoom")) || id == QLatin1String("view.actualSize"))
            a->setEnabled(has);
    }
    action(QStringLiteral("edit.paste"))->setEnabled(true);
    action(QStringLiteral("effect.repeat"))->setEnabled(has && m_lastEffect);
    action(QStringLiteral("edit.pasteNewImage"))->setEnabled(true);
    action(QStringLiteral("file.saveAll"))->setEnabled(has);
    action(QStringLiteral("file.closeAll"))->setEnabled(has);
    if (!ws) return;
    Document &d = *ws->doc;
    bool sel = !d.selection().isEmpty();
    action(QStringLiteral("edit.undo"))->setEnabled(d.history().canUndo());
    action(QStringLiteral("edit.redo"))->setEnabled(d.history().canRedo());
    action(QStringLiteral("edit.deselect"))->setEnabled(sel);
    action(QStringLiteral("edit.invertSelection"))->setEnabled(sel);
    action(QStringLiteral("edit.crop"))->setEnabled(sel);
    action(QStringLiteral("view.zoomSelection"))->setEnabled(sel);
    action(QStringLiteral("image.flatten"))->setEnabled(d.layerCount() > 1);
    action(QStringLiteral("layer.delete"))->setEnabled(d.layerCount() > 1);
    action(QStringLiteral("layer.mergeDown"))->setEnabled(d.activeLayerIndex() > 0);
    action(QStringLiteral("layer.moveDown"))->setEnabled(d.activeLayerIndex() > 0);
    action(QStringLiteral("layer.moveUp"))->setEnabled(d.activeLayerIndex() < d.layerCount() - 1);
}

void MainWindow::updateTitle()
{
    Workspace *ws = current();
    if (!ws) {
        setWindowTitle(QStringLiteral("Pane"));
        return;
    }
    setWindowTitle(QStringLiteral("%1%2 - Pane").arg(ws->doc->displayName(), ws->doc->isDirty() ? QStringLiteral("*") : QString()));
}

void MainWindow::updateTabs()
{
    std::vector<ImageTabStrip::Tab> tabs;
    for (auto &ws : m_workspaces) {
        ws->thumbnail = ws->compositor->thumbnail(64);
        tabs.push_back({ws->doc->displayName(), ws->thumbnail, ws->doc->isDirty()});
    }
    m_tabs->setTabs(tabs, m_current);
}

void MainWindow::updateStatusSelection()
{
    Workspace *ws = current();
    if (!ws) {
        m_selLabel->clear();
        updateActions();
        return;
    }
    QRect r = ws->doc->selection().boundsOrImage();
    m_selLabel->setText(QStringLiteral("%1 × %2").arg(formatLength(r.width(), true), formatLength(r.height(), false)));
    updateActions();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!closeAll()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("Window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("View/rulers"), action(QStringLiteral("view.rulers"))->isChecked());
    s.setValue(QStringLiteral("View/pixelGrid"), action(QStringLiteral("view.grid"))->isChecked());
    m_host->saveState();
    AppState::instance().save();
    e->accept();
}

} // namespace pane

namespace pane {

void MainWindow::checkRecovery()
{
    auto entries = AutosaveManager::orphans();
    if (entries.empty()) return;
    QStringList lines;
    for (const auto &e : entries)
        lines << QStringLiteral("• %1 — autosaved %2").arg(e.name.isEmpty() ? QStringLiteral("Untitled") : e.name,
                                                           QLocale().toString(e.saved, QLocale::ShortFormat));
    QMessageBox box(QMessageBox::Question, QStringLiteral("Recover Images"),
                    QStringLiteral("Pane did not close normally. These images can be recovered:\n\n%1").arg(lines.join(QLatin1Char('\n'))),
                    QMessageBox::NoButton, this);
    auto *recover = box.addButton(QStringLiteral("Recover"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(recover);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton *>(recover)) {
        for (const auto &e : entries) {
            try {
                addWorkspace(AutosaveManager::recover(e));
                AutosaveManager::discard(e); // the recovered document autosaves under a new id
            } catch (const std::exception &ex) {
                showError(QStringLiteral("Could Not Recover Image"), QString::fromUtf8(ex.what()));
            }
        }
    } else if (box.clickedButton() == static_cast<QAbstractButton *>(discard)) {
        for (const auto &e : entries) AutosaveManager::discard(e);
    }
}

void MainWindow::showSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::printImage()
{
    if (!current()) return;
    commitTool();
    const QImage image = current()->doc->flattenToImage();
    const double dpiX = current()->doc->dpiX(), dpiY = current()->doc->dpiY();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(current()->doc->displayName());
    printer.setPageOrientation(image.width() > image.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(QStringLiteral("Print"));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter *p) {
        QPainter painter(p);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QRectF page = p->pageLayout().paintRectPixels(p->resolution());
        page.moveTo(0, 0);
        painter.drawImage(printTargetRect(image.size(), dpiX, dpiY, page, p->resolution(), p->resolution()), image);
    });
    preview.exec();
}

} // namespace pane
