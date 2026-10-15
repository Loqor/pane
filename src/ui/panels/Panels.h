#pragma once

#include "tools/Tool.h"
#include "ui/Workspace.h"

#include <QButtonGroup>
#include <QListWidget>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>

class QLineEdit;
class QSpinBox;
class QSlider;
class QToolButton;

namespace pane {

class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget *parent = nullptr);
    void setActiveTool(const QString &id);

signals:
    void toolSelected(const QString &id);

private:
    QButtonGroup *m_group;
    std::map<QString, QToolButton *> m_buttons;
};

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget *parent = nullptr);
    void setWorkspace(Workspace *ws);

signals:
    void beforeHistoryJump();

private:
    void rebuild();
    QListWidget *m_list;
    Workspace *m_ws = nullptr;
    QMetaObject::Connection m_conn;
    QToolButton *m_undo, *m_redo;
    bool m_updating = false;
};

// paint.net-style layer list: top layer first, thumbnail, name, visibility checkbox.
class LayerList : public QWidget {
    Q_OBJECT
public:
    explicit LayerList(QWidget *parent = nullptr);
    void setWorkspace(Workspace *ws);
    QSize sizeHint() const override;
    void refreshThumbnails();

signals:
    void propertiesRequested();
    void beforeChange();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    static constexpr int RowHeight = 44;
    int rowToLayer(int row) const;
    int rowAt(int y) const;
    QRect checkRect(int row) const;
    Workspace *m_ws = nullptr;
    std::vector<QImage> m_thumbs;
    int m_dragFrom = -1, m_dragTo = -1;
    QPoint m_pressPos;
};

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);
    void setWorkspace(Workspace *ws);
    LayerList *list() const { return m_list; }

signals:
    void actionRequested(const QString &action);

private:
    LayerList *m_list;
    QScrollArea *m_scroll;
    Workspace *m_ws = nullptr;
    QList<QMetaObject::Connection> m_conns;
    QTimer m_thumbTimer;
};

// Hue/saturation wheel (angle = hue, radius = saturation).
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget *parent = nullptr);
    void setHsv(double h, double s, double v);
    QSize sizeHint() const override { return {150, 150}; }
    QSize minimumSizeHint() const override { return {110, 110}; }

signals:
    void hueSatChanged(double h, double s);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *) override { m_cache = QImage(); }

private:
    void pick(QPointF p);
    double m_h = 0, m_s = 0, m_v = 1;
    QImage m_cache;
    double m_cacheV = -1;
};

// Primary/secondary swatches with swap/reset, like paint.net's color well.
class ColorWell : public QWidget {
    Q_OBJECT
public:
    explicit ColorWell(QWidget *parent = nullptr);
    bool editingSecondary() const { return m_secondary; }
    QSize sizeHint() const override { return {64, 64}; }

signals:
    void targetChanged(bool secondary);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QRect primaryRect() const { return QRect(2, 2, 38, 38); }
    QRect secondaryRect() const { return QRect(22, 22, 38, 38); }
    QRect swapRect() const { return QRect(44, 0, 18, 18); }
    QRect resetRect() const { return QRect(0, 44, 18, 18); }
    bool m_secondary = false;
};

class PaletteGrid : public QWidget {
    Q_OBJECT
public:
    explicit PaletteGrid(QWidget *parent = nullptr);
    static std::vector<Pixel> defaultPalette();
    QSize sizeHint() const override;
    const std::vector<Pixel> &colors() const { return m_colors; }
    // Replaces the palette and persists it.
    void setColors(const std::vector<Pixel> &colors);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    static constexpr int Cell = 12, Cols = 16;
    std::vector<Pixel> m_colors;
};

class ColorsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorsPanel(QWidget *parent = nullptr);

private:
    void syncFromState();
    void setFromHsv(double h, double s, double v, int a);
    void setFromRgb(int r, int g, int b, int a);
    Pixel current() const;
    void commit(Pixel p);

    ColorWell *m_well;
    PaletteGrid *m_palette;
    QWidget *m_more;
    QToolButton *m_moreButton;
    ColorWheel *m_wheel;
    QSlider *m_value, *m_alpha;
    QSpinBox *m_r, *m_g, *m_b, *m_hS, *m_sS, *m_vS, *m_a;
    QLineEdit *m_hex;
    bool m_syncing = false;
    double m_h = 0, m_s = 0, m_v = 0;
};

} // namespace pane
