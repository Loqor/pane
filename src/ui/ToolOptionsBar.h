#pragma once

#include "tools/Tool.h"

#include <QToolBar>

namespace pane {

// The tool options toolbar: "Tool: [glyph]" followed by the options the
// active tool declares (brush width, hardness, antialiasing, ...).
class ToolOptionsBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolOptionsBar(QWidget *parent = nullptr);
    void setTool(Tool *tool);

signals:
    void toolPicked(const QString &id);

private:
    void rebuild();
    QWidget *labeled(const QString &text);
    Tool *m_tool = nullptr;
};

} // namespace pane
