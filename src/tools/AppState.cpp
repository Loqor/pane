#include "tools/AppState.h"

#include <QSettings>

namespace pane {

AppState &AppState::instance()
{
    static AppState s;
    return s;
}

void AppState::setPrimary(Pixel p)
{
    if (p == m_primary) return;
    m_primary = p;
    emit colorsChanged();
}

void AppState::setSecondary(Pixel p)
{
    if (p == m_secondary) return;
    m_secondary = p;
    emit colorsChanged();
}

void AppState::swapColors()
{
    std::swap(m_primary, m_secondary);
    emit colorsChanged();
}

void AppState::resetColors()
{
    m_primary = 0xFF000000;
    m_secondary = 0xFFFFFFFF;
    emit colorsChanged();
}

void AppState::setActiveToolId(const QString &id)
{
    if (id == m_activeTool) return;
    m_previousTool = m_activeTool;
    m_activeTool = id;
    emit activeToolChanged(id);
}

void AppState::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("Tools"));
    ToolSettings d;
    m_settings.brushWidth = s.value(QStringLiteral("brushWidth"), d.brushWidth).toInt();
    m_settings.hardness = s.value(QStringLiteral("hardness"), d.hardness).toInt();
    m_settings.antialias = s.value(QStringLiteral("antialias"), d.antialias).toBool();
    m_settings.alphaBlending = s.value(QStringLiteral("alphaBlending"), d.alphaBlending).toBool();
    m_settings.floodGlobal = s.value(QStringLiteral("floodGlobal"), d.floodGlobal).toBool();
    m_settings.tolerance = s.value(QStringLiteral("tolerance"), d.tolerance).toInt();
    m_settings.sampleImage = s.value(QStringLiteral("sampleImage"), d.sampleImage).toBool();
    m_settings.pickerSampling = PickerSampling(s.value(QStringLiteral("pickerSampling"), int(d.pickerSampling)).toInt());
    m_settings.pickerAfter = PickerAfter(s.value(QStringLiteral("pickerAfter"), int(d.pickerAfter)).toInt());
    m_settings.moveSmooth = s.value(QStringLiteral("moveSmooth"), d.moveSmooth).toBool();
    m_settings.pressureSize = s.value(QStringLiteral("pressureSize"), d.pressureSize).toBool();
    m_settings.pressureOpacity = s.value(QStringLiteral("pressureOpacity"), d.pressureOpacity).toBool();
    m_settings.pressureGamma = s.value(QStringLiteral("pressureGamma"), d.pressureGamma).toDouble();
    m_settings.pressureMin = s.value(QStringLiteral("pressureMin"), d.pressureMin).toInt();
    m_primary = s.value(QStringLiteral("primary"), m_primary).toUInt();
    m_secondary = s.value(QStringLiteral("secondary"), m_secondary).toUInt();
    s.endGroup();
}

void AppState::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("Tools"));
    s.setValue(QStringLiteral("brushWidth"), m_settings.brushWidth);
    s.setValue(QStringLiteral("hardness"), m_settings.hardness);
    s.setValue(QStringLiteral("antialias"), m_settings.antialias);
    s.setValue(QStringLiteral("alphaBlending"), m_settings.alphaBlending);
    s.setValue(QStringLiteral("floodGlobal"), m_settings.floodGlobal);
    s.setValue(QStringLiteral("tolerance"), m_settings.tolerance);
    s.setValue(QStringLiteral("sampleImage"), m_settings.sampleImage);
    s.setValue(QStringLiteral("pickerSampling"), int(m_settings.pickerSampling));
    s.setValue(QStringLiteral("pickerAfter"), int(m_settings.pickerAfter));
    s.setValue(QStringLiteral("moveSmooth"), m_settings.moveSmooth);
    s.setValue(QStringLiteral("pressureSize"), m_settings.pressureSize);
    s.setValue(QStringLiteral("pressureOpacity"), m_settings.pressureOpacity);
    s.setValue(QStringLiteral("pressureGamma"), m_settings.pressureGamma);
    s.setValue(QStringLiteral("pressureMin"), m_settings.pressureMin);
    s.setValue(QStringLiteral("primary"), m_primary);
    s.setValue(QStringLiteral("secondary"), m_secondary);
    s.endGroup();
}

} // namespace pane
