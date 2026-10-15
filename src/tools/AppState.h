// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Color.h"
#include "core/Selection.h"

#include <QObject>
#include <QString>

namespace pane {

enum class PickerSampling { Single, Region3, Region5, Region7, Region9 };
enum class PickerAfter { Nothing, PreviousTool, Pencil };

// Tool options shared across tools, like paint.net's AppEnvironment.
struct ToolSettings {
    int brushWidth = 2;
    int hardness = 75; // percent
    bool antialias = true;
    bool alphaBlending = true; // false = Overwrite
    bool floodGlobal = false;
    int tolerance = 50; // percent
    bool sampleImage = false; // false = Layer
    PickerSampling pickerSampling = PickerSampling::Single;
    PickerAfter pickerAfter = PickerAfter::Nothing;
    CombineMode selectionMode = CombineMode::Replace;
    bool moveSmooth = true; // Move Selected Pixels resampling

    // Tablet pressure (M6)
    bool pressureSize = true;
    bool pressureOpacity = false;
    double pressureGamma = 1.0; // response curve p′ = p^gamma
    int pressureMin = 10;       // minimum size, percent of brush width

    // M2
    bool lassoPolygon = false;
    int gradientType = 0;  // GradientType
    bool gradientTransparency = false;
    int gradientRepeat = 0; // 0 none, 1 sawtooth, 2 triangular
    int shapeType = 0;     // ShapeType
    int shapeStyle = 0;    // 0 outline, 1 filled, 2 filled with outline
    int dashStyle = 0;     // 0 solid, 1 dash, 2 dot, 3 dash-dot, 4 dash-dot-dot
    bool curveBezier = false;
    bool arrowStart = false, arrowEnd = false;
    QString fontFamily;
    int fontSize = 12;
    bool bold = false, italic = false, underline = false, strikeout = false;
    int textAlign = 0; // 0 left, 1 center, 2 right
};

// Global, per-process editor state: colors, tool settings, active tool.
class AppState : public QObject {
    Q_OBJECT
public:
    static AppState &instance();

    Pixel primary() const { return m_primary; }
    Pixel secondary() const { return m_secondary; }
    void setPrimary(Pixel p);
    void setSecondary(Pixel p);
    void swapColors();
    void resetColors();

    const ToolSettings &settings() const { return m_settings; }
    ToolSettings &mutableSettings() { return m_settings; }
    void notifySettingsChanged() { emit settingsChanged(); }

    QString activeToolId() const { return m_activeTool; }
    QString previousToolId() const { return m_previousTool; }
    void setActiveToolId(const QString &id);

    void load();
    void save() const;

signals:
    void colorsChanged();
    void settingsChanged();
    void activeToolChanged(const QString &id);
    void hintChanged(const QString &glyph, const QString &text);

private:
    AppState() = default;
    Pixel m_primary = 0xFF000000, m_secondary = 0xFFFFFFFF;
    ToolSettings m_settings;
    QString m_activeTool, m_previousTool;
};

} // namespace pane
