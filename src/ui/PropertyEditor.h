#pragma once

#include "core/Property.h"

#include <QWidget>

class QToolButton;

namespace pane {

// Builds a form (slider + spin box, checkbox, combo, color, seed) from a
// property list. Used by effect dialogs and the Save Configuration dialog.
class PropertyEditor : public QWidget {
    Q_OBJECT
public:
    PropertyEditor(const QList<PropertySpec> &props, const QVariantMap &initial, QWidget *parent = nullptr);
    QVariantMap params() const;
    void setValues(const QVariantMap &v);

signals:
    void changed();

private:
    void setColorButton(QToolButton *b, uint32_t color);
    struct Field {
        PropertySpec spec;
        QWidget *editor = nullptr;
        QWidget *slider = nullptr;
    };
    QList<PropertySpec> m_props;
    std::vector<Field> m_fields;
    bool m_updating = false;
};

} // namespace pane
