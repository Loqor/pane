#pragma once

#include "core/Document.h"
#include "tools/AppState.h"

#include <QCursor>
#include <QKeyEvent>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QTransform>

namespace pane {

struct PointerEvent {
    QPointF pos;      // image coordinates (pixel centers at +0.5)
    QPointF viewPos;  // viewport coordinates
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
    double pressure = 1.0;
};

// Services the canvas provides to tools.
class ToolHost {
public:
    virtual ~ToolHost() = default;
    virtual Document &document() = 0;
    virtual void updateOverlay() = 0;
    virtual double zoom() const = 0;
    virtual void zoomIn(QPointF imageAnchor) = 0;
    virtual void zoomOut(QPointF imageAnchor) = 0;
    virtual void zoomToRect(const QRectF &imageRect) = 0;
    virtual void panBy(QPointF viewDelta) = 0;
    virtual QTransform imageToView() const = 0;
    virtual void setToolCursor(const QCursor &c) = 0;
    virtual QColor accent() const = 0;
};

// Options a tool shows in the tool options bar, in order.
enum class ToolOption {
    SelectionMode,
    BrushWidth,
    Hardness,
    Antialias,
    AlphaBlending,
    FloodMode,
    Tolerance,
    Sampling,
    PickerSampling,
    PickerAfter,
    MoveQuality,
    LassoMode,
    GradientType,
    GradientMode,
    GradientRepeat,
    ShapeType,
    ShapeStyle,
    DashStyle,
    CurveType,
    Arrows,
    Font,
    TextStyle,
    TextAlign,
};

class Tool : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString glyph() const = 0;
    virtual QChar shortcut() const = 0;
    virtual QString hint() const = 0;
    virtual QList<ToolOption> options() const { return {}; }
    virtual bool enabled() const { return true; } // false for not-yet-implemented tools

    virtual void activate(ToolHost &host) { m_host = &host; host.setToolCursor(cursor()); }
    virtual void deactivate() { commit(); m_host = nullptr; }
    // Finish any pending, uncommitted work (e.g. floating pixels).
    virtual void commit() {}
    virtual bool isBusy() const { return false; }
    // True while the tool wants all key presses (e.g. typing text).
    virtual bool wantsKeyboard() const { return false; }
    virtual void inputText(const QString &) {}

    virtual void pointerPress(const PointerEvent &) {}
    virtual void pointerMove(const PointerEvent &) {}
    virtual void pointerRelease(const PointerEvent &) {}
    virtual void hover(const PointerEvent &) {}
    virtual bool keyPress(QKeyEvent *) { return false; }
    virtual void paintOverlay(QPainter &, const QTransform & /*imageToView*/) {}
    virtual QCursor cursor() const { return Qt::CrossCursor; }

protected:
    ToolHost *host() const { return m_host; }
    Document &doc() const { return m_host->document(); }
    static AppState &app() { return AppState::instance(); }

private:
    ToolHost *m_host = nullptr;
};

class ToolRegistry {
public:
    static ToolRegistry &instance();
    const std::vector<Tool *> &tools() const { return m_tools; }
    Tool *byId(const QString &id) const;
    // The next tool sharing `key` after `current` (paint.net's key cycling).
    Tool *nextForKey(QChar key, const QString &current) const;
    // The tool's single-letter key: a user override (Settings) or its default.
    QChar keyFor(const Tool *t) const;
    // QChar() restores the default. Persisted under ToolKeys/<id>.
    void setKey(const QString &id, QChar key);
    // Distinct keys currently assigned to enabled tools.
    QString keysInUse() const;

private:
    ToolRegistry();
    std::vector<Tool *> m_tools;
    std::vector<std::unique_ptr<Tool>> m_owned;
    std::map<QString, QChar> m_keyOverrides;
};

// Brush radius and dab strength for a pointer pressure, using the tablet settings.
struct PressureDab {
    double radius;
    double strength;
};
PressureDab pressureDab(const ToolSettings &s, double pressure);

// Composite color of all visible layers at one pixel.
Pixel compositePixel(const Document &doc, int x, int y);

} // namespace pane
