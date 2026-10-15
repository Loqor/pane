#include "core/Glyphs.h"
#include "core/Paint.h"
#include "tools/DrawTools.h"
#include "tools/Tools.h"

#include <QSettings>
#include <cmath>

namespace pane {

namespace {

Pixel colorFor(Qt::MouseButton b)
{
    return b == Qt::RightButton ? AppState::instance().secondary() : AppState::instance().primary();
}

PaintMode blendModeSetting() { return AppState::instance().settings().alphaBlending ? PaintMode::Blend : PaintMode::Overwrite; }

} // namespace

Pixel compositePixel(const Document &doc, int x, int y)
{
    Pixel out = 0;
    for (int i = 0; i < doc.layerCount(); ++i) {
        const Layer &l = doc.layer(i);
        if (!l.props.visible) continue;
        out = blendPixel(l.props.blendMode, out, l.surface.pixel(x, y), l.props.opacity);
    }
    return out;
}

PressureDab pressureDab(const ToolSettings &s, double pressure)
{
    const double p = std::pow(std::clamp(pressure, 0.0, 1.0), std::clamp(s.pressureGamma, 0.1, 10.0));
    const double minK = std::clamp(s.pressureMin, 1, 100) / 100.0;
    const double sizeK = s.pressureSize ? minK + (1.0 - minK) * p : 1.0;
    return {std::max(0.5, s.brushWidth * sizeK / 2.0), s.pressureOpacity ? p : 1.0};
}

// ------------------------------------------------------------ Brushes

QString PaintbrushTool::glyph() const { return glyphs::Brush; }
QString EraserTool::glyph() const { return glyphs::Eraser; }

double BrushTool::radiusFor(double pressure) const { return pressureDab(app().settings(), pressure).radius; }

void BrushTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    const ToolSettings &s = app().settings();
    PaintMode mode = erasing() ? PaintMode::Erase : blendModeSetting();
    m_stroke = std::make_unique<CoverageStroke>(doc(), doc().activeLayerIndex(), colorFor(e.button), mode);
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addDab(e.pos, d.radius, s.hardness / 100.0, s.antialias, d.strength);
    m_last = e.pos;
    m_lastStrength = d.strength;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
}

void BrushTool::pointerMove(const PointerEvent &e)
{
    m_hover = e.pos;
    m_hoverValid = true;
    if (!m_stroke) {
        host()->updateOverlay();
        return;
    }
    const ToolSettings &s = app().settings();
    PressureDab d = pressureDab(s, e.pressure);
    m_stroke->addLine(m_last, e.pos, d.radius, s.hardness / 100.0, s.antialias, std::max(0.5, d.radius * 0.25), m_lastStrength,
                      d.strength);
    m_last = e.pos;
    m_lastStrength = d.strength;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
    host()->updateOverlay();
}

void BrushTool::pointerRelease(const PointerEvent &)
{
    if (!m_stroke) return;
    auto stroke = std::move(m_stroke);
    if (auto item = stroke->finish(name(), glyph())) doc().history().push(std::move(item));
}

void BrushTool::deactivate()
{
    if (m_stroke) pointerRelease({});
    m_hoverValid = false;
    Tool::deactivate();
}

void BrushTool::hover(const PointerEvent &e)
{
    m_hover = e.pos;
    m_hoverValid = true;
    host()->updateOverlay();
}

void BrushTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_hoverValid) return;
    double r = app().settings().brushWidth / 2.0;
    QPointF c = t.map(m_hover);
    double vr = r * host()->zoom();
    if (vr < 2) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 200), 1));
    p.drawEllipse(c, vr + 0.5, vr + 0.5);
    p.setPen(QPen(QColor(0, 0, 0, 200), 1));
    p.drawEllipse(c, vr - 0.5, vr - 0.5);
    p.restore();
}

// ------------------------------------------------------------ Pencil

QString PencilTool::glyph() const { return glyphs::Pencil; }

void PencilTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    m_stroke = std::make_unique<CoverageStroke>(doc(), doc().activeLayerIndex(), colorFor(e.button), blendModeSetting());
    m_last = QPoint(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    m_stroke->addPixel(m_last.x(), m_last.y());
    doc().notifyPixelsChanged(m_stroke->takeDirty());
}

void PencilTool::pointerMove(const PointerEvent &e)
{
    if (!m_stroke) return;
    QPoint p(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    if (p == m_last) return;
    m_stroke->addPixelLine(m_last, p);
    m_last = p;
    doc().notifyPixelsChanged(m_stroke->takeDirty());
}

void PencilTool::pointerRelease(const PointerEvent &)
{
    if (!m_stroke) return;
    auto stroke = std::move(m_stroke);
    if (auto item = stroke->finish(name(), glyph())) doc().history().push(std::move(item));
}

void PencilTool::deactivate()
{
    if (m_stroke) pointerRelease({});
    Tool::deactivate();
}

// ------------------------------------------------------------ Paint Bucket

QString PaintBucketTool::glyph() const { return glyphs::Bucket; }

void PaintBucketTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    QPoint seed(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    if (!doc().bounds().contains(seed)) return;
    const ToolSettings &s = app().settings();
    Surface sample = s.sampleImage ? doc().flatten() : doc().activeLayer().surface;
    QRect bounds;
    auto mask = floodMask(sample, seed, s.tolerance / 100.0, s.floodGlobal, &bounds);
    if (bounds.isEmpty()) return;
    CoverageStroke stroke(doc(), doc().activeLayerIndex(), colorFor(e.button), blendModeSetting());
    stroke.addMask(mask, bounds);
    doc().notifyPixelsChanged(stroke.takeDirty());
    if (auto item = stroke.finish(name(), glyph())) doc().history().push(std::move(item));
}

// ------------------------------------------------------------ Color Picker

QString ColorPickerTool::glyph() const { return glyphs::ColorPicker; }

void ColorPickerTool::pick(const PointerEvent &e)
{
    const ToolSettings &s = app().settings();
    int x = int(std::floor(e.pos.x())), y = int(std::floor(e.pos.y()));
    if (!doc().bounds().contains(x, y)) return;
    int radius = int(s.pickerSampling); // 0 = single, 1 = 3x3, ...
    const Surface &layer = doc().activeLayer().surface;
    uint64_t acc[4] = {0, 0, 0, 0}, n = 0;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = x + dx, py = y + dy;
            if (!doc().bounds().contains(px, py)) continue;
            Pixel p = premultiply(s.sampleImage ? compositePixel(doc(), px, py) : layer.pixel(px, py));
            acc[0] += pxR(p); acc[1] += pxG(p); acc[2] += pxB(p); acc[3] += pxA(p);
            ++n;
        }
    if (!n) return;
    Pixel avg = unpremultiply(makePixel(uint32_t(acc[0] / n), uint32_t(acc[1] / n), uint32_t(acc[2] / n), uint32_t(acc[3] / n)));
    if (m_button == Qt::RightButton) app().setSecondary(avg);
    else app().setPrimary(avg);
}

void ColorPickerTool::pointerPress(const PointerEvent &e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton) return;
    m_button = e.button;
    pick(e);
}

void ColorPickerTool::pointerMove(const PointerEvent &e)
{
    if (m_button != Qt::NoButton) pick(e);
}

void ColorPickerTool::pointerRelease(const PointerEvent &)
{
    if (m_button == Qt::NoButton) return;
    m_button = Qt::NoButton;
    switch (app().settings().pickerAfter) {
    case PickerAfter::Nothing: break;
    case PickerAfter::PreviousTool:
        if (!app().previousToolId().isEmpty()) app().setActiveToolId(app().previousToolId());
        break;
    case PickerAfter::Pencil: app().setActiveToolId(QStringLiteral("Pencil")); break;
    }
}

// ------------------------------------------------------------ Zoom / Pan

QString ZoomTool::glyph() const { return glyphs::Zoom; }
QString PanTool::glyph() const { return glyphs::Pan; }

QCursor ZoomTool::cursor() const { return Qt::CrossCursor; }

void ZoomTool::pointerPress(const PointerEvent &e)
{
    m_dragging = true;
    m_start = m_cur = e.pos;
    m_button = e.button;
}

void ZoomTool::pointerMove(const PointerEvent &e)
{
    if (!m_dragging) return;
    m_cur = e.pos;
    host()->updateOverlay();
}

void ZoomTool::pointerRelease(const PointerEvent &e)
{
    if (!m_dragging) return;
    m_dragging = false;
    QPointF d = (e.pos - m_start) * host()->zoom();
    if (m_button == Qt::LeftButton && (std::abs(d.x()) > 4 || std::abs(d.y()) > 4)) {
        host()->zoomToRect(QRectF(m_start, e.pos).normalized());
    } else if (m_button == Qt::RightButton) {
        host()->zoomOut(e.pos);
    } else {
        host()->zoomIn(e.pos);
    }
    host()->updateOverlay();
}

void ZoomTool::paintOverlay(QPainter &p, const QTransform &t)
{
    if (!m_dragging || m_button != Qt::LeftButton) return;
    QRectF r = t.mapRect(QRectF(m_start, m_cur).normalized());
    p.save();
    p.setPen(QPen(host()->accent(), 1, Qt::DashLine));
    QColor fill = host()->accent();
    fill.setAlpha(40);
    p.setBrush(fill);
    p.drawRect(r);
    p.restore();
}

void PanTool::pointerPress(const PointerEvent &e)
{
    m_dragging = true;
    m_last = e.viewPos;
    host()->setToolCursor(cursor());
}

void PanTool::pointerMove(const PointerEvent &e)
{
    if (!m_dragging) return;
    host()->panBy(e.viewPos - m_last);
    m_last = e.viewPos;
}

void PanTool::pointerRelease(const PointerEvent &)
{
    m_dragging = false;
    host()->setToolCursor(cursor());
}

// ------------------------------------------------------------ Registry

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry r;
    return r;
}

ToolRegistry::ToolRegistry()
{
    // paint.net's Tools window order.
    m_tools = {
        new RectangleSelectTool,
        new MoveSelectedPixelsTool,
        new LassoSelectTool,
        new MoveSelectionTool,
        new EllipseSelectTool,
        new ZoomTool,
        new MagicWandTool,
        new PanTool,
        new PaintBucketTool,
        new GradientTool,
        new PaintbrushTool,
        new EraserTool,
        new PencilTool,
        new ColorPickerTool,
        new CloneStampTool,
        new RecolorTool,
        new TextTool,
        new LineCurveTool,
        new ShapesTool,
    };
    for (Tool *t : m_tools) m_owned.emplace_back(t);
    QSettings s;
    s.beginGroup(QStringLiteral("ToolKeys"));
    for (const QString &id : s.childKeys()) {
        QString v = s.value(id).toString();
        if (!v.isEmpty()) m_keyOverrides[id] = v[0].toUpper();
    }
}

Tool *ToolRegistry::byId(const QString &id) const
{
    for (Tool *t : m_tools)
        if (t->id() == id) return t;
    return nullptr;
}

Tool *ToolRegistry::nextForKey(QChar key, const QString &current) const
{
    std::vector<Tool *> matches;
    for (Tool *t : m_tools)
        if (t->enabled() && keyFor(t) == key.toUpper()) matches.push_back(t);
    if (matches.empty()) return nullptr;
    for (size_t i = 0; i < matches.size(); ++i)
        if (matches[i]->id() == current) return matches[(i + 1) % matches.size()];
    return matches.front();
}

QChar ToolRegistry::keyFor(const Tool *t) const
{
    auto it = m_keyOverrides.find(t->id());
    return it != m_keyOverrides.end() ? it->second : t->shortcut();
}

void ToolRegistry::setKey(const QString &id, QChar key)
{
    QSettings s;
    if (key.isNull()) {
        m_keyOverrides.erase(id);
        s.remove(QStringLiteral("ToolKeys/") + id);
    } else {
        m_keyOverrides[id] = key.toUpper();
        s.setValue(QStringLiteral("ToolKeys/") + id, QString(key.toUpper()));
    }
}

QString ToolRegistry::keysInUse() const
{
    QString keys;
    for (Tool *t : m_tools) {
        QChar k = keyFor(t);
        if (t->enabled() && !k.isNull() && !keys.contains(k)) keys += k;
    }
    return keys;
}

} // namespace pane
