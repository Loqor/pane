#pragma once

#include "ui/CanvasView.h"
#include "ui/Workspace.h"

#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <map>

class QLabel;
class QSlider;
class QComboBox;
class QActionGroup;
class QShortcut;

namespace pane {

class FloatingPanelHost;
class FloatingPanel;
class ToolsPanel;
class HistoryPanel;
class LayersPanel;
class ColorsPanel;
class ImageTabStrip;
class ToolOptionsBar;
class FileType;
class Effect;
class AutosaveManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFiles(const QStringList &paths);
    void newImage(QSize size, double dpi = 96);
    Workspace *current() const { return m_current >= 0 ? m_workspaces[size_t(m_current)].get() : nullptr; }
    int workspaceCount() const { return int(m_workspaces.size()); }
    CanvasView *canvas() const { return m_canvas; }
    QAction *action(const QString &id) const;
    void setActiveTool(const QString &id);

    // Settings support.
    struct ShortcutInfo {
        QString id, text;
        QKeySequence current, defaultSequence;
    };
    std::vector<ShortcutInfo> shortcutList() const;
    void setActionShortcut(const QString &id, const QKeySequence &seq);
    void resetActionShortcut(const QString &id);
    void rebuildToolShortcuts();
    AutosaveManager *autosave() const { return m_autosave; }
    void applyTheme(const QString &id, const QString &accent);
    FloatingPanelHost *panelHost() const { return m_host; }
    // Offers documents left behind by a crash (call after show()).
    void checkRecovery();
    void showSettings();
    void printImage();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    // Setup
    void createActions();
    void createMenus();
    void createToolBars();
    void createPanels();
    void createStatusBar();
    QAction *addAction(const QString &id, const QString &text, const QString &glyph, const QKeySequence &shortcut,
                       std::function<void()> fn);
    QAction *addDocAction(const QString &id, const QString &text, const QString &glyph, const QKeySequence &shortcut,
                          std::function<void(Document &)> fn);

    // Documents
    void addWorkspace(std::unique_ptr<Document> doc);
    void setCurrent(int index);
    bool closeWorkspace(int index);
    bool closeAll();
    bool save(Workspace &ws, bool saveAs);
    void openDialog();
    void newImageDialog();
    void addRecent(const QString &path);
    void rebuildRecentMenu();

    // Clipboard
    QImage selectionImage(bool merged) const;
    void copy(bool merged);
    void cut();
    void paste(int mode); // 0 = paste, 1 = into new layer, 2 = into new image

    // Commands
    void commitTool();
    void runDocCommand(const std::function<void(Document &)> &fn);
    void showError(const QString &title, const QString &message);
    void runEffect(const Effect &fx, bool repeat = false);
    void layerProperties();
    void importFromFile();
    void resizeImage();
    void canvasSize();
    void rebuildThemeMenu();
    void captureDefaultShortcutsAndApplyOverrides();
    void updateActions();
    void updateTitle();
    void updateTabs();
    void updateStatusSelection();
    void updateZoomWidgets(double zoom);
    QString formatLength(double px, bool horizontal) const;

    std::vector<std::unique_ptr<Workspace>> m_workspaces;
    int m_current = -1;
    QList<QMetaObject::Connection> m_docConns;

    CanvasView *m_canvas = nullptr;
    FloatingPanelHost *m_host = nullptr;
    FloatingPanel *m_toolsPanel = nullptr, *m_historyPanel = nullptr, *m_layersPanel = nullptr, *m_colorsPanel = nullptr;
    ToolsPanel *m_tools = nullptr;
    HistoryPanel *m_history = nullptr;
    LayersPanel *m_layers = nullptr;
    ColorsPanel *m_colors = nullptr;
    ImageTabStrip *m_tabs = nullptr;
    ToolOptionsBar *m_optionsBar = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_themeMenu = nullptr;

    QLabel *m_hintGlyph = nullptr, *m_hint = nullptr, *m_sizeLabel = nullptr, *m_cursorLabel = nullptr, *m_selLabel = nullptr;
    QComboBox *m_unitsCombo = nullptr, *m_zoomCombo = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;

    std::map<QString, QAction *> m_actions;
    std::map<QString, QKeySequence> m_defaultShortcuts;
    std::map<QString, QList<QKeySequence>> m_defaultShortcutLists;
    QList<QShortcut *> m_toolShortcuts;
    AutosaveManager *m_autosave = nullptr;
    QTimer m_thumbTimer;
    const Effect *m_lastEffect = nullptr;
    QVariantMap m_lastEffectParams;
};

} // namespace pane
