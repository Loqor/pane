#include "ui/PropertyEditor.h"

#include "core/Color.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <cmath>

namespace pane {

PropertyEditor::PropertyEditor(const QList<PropertySpec> &props, const QVariantMap &initial, QWidget *parent)
    : QWidget(parent), m_props(props)
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setColumnStretch(1, 1);
    int row = 0;
    const bool hasLink = std::any_of(props.begin(), props.end(),
                                     [](const PropertySpec &p) { return p.id == QLatin1String("linked"); });
    for (const PropertySpec &spec : props) {
        Field f{spec, nullptr, nullptr};
        switch (spec.type) {
        case PropType::Int:
        case PropType::Double: {
            auto *label = new QLabel(spec.label + QLatin1Char(':'), this);
            auto *slider = new QSlider(Qt::Horizontal, this);
            double scale = std::pow(10.0, spec.decimals);
            slider->setRange(int(std::lround(spec.min * scale)), int(std::lround(spec.max * scale)));
            QWidget *editor;
            if (spec.type == PropType::Int) {
                auto *s = new QSpinBox(this);
                s->setRange(int(spec.min), int(spec.max));
                s->setSuffix(spec.suffix);
                connect(s, &QSpinBox::valueChanged, slider, &QSlider::setValue);
                connect(slider, &QSlider::valueChanged, s, &QSpinBox::setValue);
                editor = s;
            } else {
                auto *s = new QDoubleSpinBox(this);
                s->setRange(spec.min, spec.max);
                s->setDecimals(spec.decimals);
                s->setSingleStep(1.0 / scale);
                connect(s, &QDoubleSpinBox::valueChanged, slider, [slider, scale](double v) {
                    QSignalBlocker b(slider);
                    slider->setValue(int(std::lround(v * scale)));
                });
                connect(slider, &QSlider::valueChanged, s, [s, scale](int v) { s->setValue(v / scale); });
                editor = s;
            }
            editor->setMinimumWidth(80);
            grid->addWidget(label, row, 0);
            grid->addWidget(slider, row, 1);
            grid->addWidget(editor, row, 2);
            f.editor = editor;
            f.slider = slider;
            break;
        }
        case PropType::Bool: {
            auto *c = new QCheckBox(spec.label, this);
            grid->addWidget(c, row, 0, 1, 3);
            f.editor = c;
            break;
        }
        case PropType::Seed: {
            auto *s = new QSpinBox(this);
            s->setRange(0, 2147483647);
            auto *reseed = new QPushButton(QStringLiteral("Reseed"), this);
            connect(reseed, &QPushButton::clicked, s, [s] { s->setValue(int(QRandomGenerator::global()->bounded(2147483647))); });
            grid->addWidget(new QLabel(spec.label + QLatin1Char(':'), this), row, 0);
            grid->addWidget(reseed, row, 1, Qt::AlignLeft);
            grid->addWidget(s, row, 2);
            f.editor = s;
            break;
        }
        case PropType::Color: {
            auto *b = new QToolButton(this);
            b->setFixedSize(48, 22);
            connect(b, &QToolButton::clicked, this, [this, b] {
                QColor c = QColorDialog::getColor(qcolorFromPixel(b->property("pane_color").toUInt()), this, QStringLiteral("Choose Color"),
                                                  QColorDialog::ShowAlphaChannel);
                if (!c.isValid()) return;
                setColorButton(b, pixelFromQColor(c));
                emit changed();
            });
            grid->addWidget(new QLabel(spec.label + QLatin1Char(':'), this), row, 0);
            grid->addWidget(b, row, 1, Qt::AlignLeft);
            f.editor = b;
            break;
        }
        case PropType::Choice: {
            auto *c = new QComboBox(this);
            c->addItems(spec.choices);
            grid->addWidget(new QLabel(spec.label + QLatin1Char(':'), this), row, 0);
            grid->addWidget(c, row, 1, 1, 2);
            f.editor = c;
            break;
        }
        }
        m_fields.push_back(f);
        ++row;
    }
    auto *reset = new QPushButton(QStringLiteral("Reset"), this);
    grid->addWidget(reset, row, 2);
    connect(reset, &QPushButton::clicked, this, [this] { setValues(propertyDefaults(m_props)); emit changed(); });

    // Change notifications (and "linked" integer fields, as in Posterize).
    for (size_t i = 0; i < m_fields.size(); ++i) {
        Field &f = m_fields[i];
        if (auto *s = qobject_cast<QSpinBox *>(f.editor)) {
            connect(s, &QSpinBox::valueChanged, this, [this, hasLink, i](int v) {
                if (m_updating) return;
                if (hasLink && params().value(QStringLiteral("linked")).toBool()) {
                    m_updating = true;
                    for (size_t j = 0; j < m_fields.size(); ++j)
                        if (j != i)
                            if (auto *o = qobject_cast<QSpinBox *>(m_fields[j].editor)) o->setValue(v);
                    m_updating = false;
                }
                emit changed();
            });
        } else if (auto *d = qobject_cast<QDoubleSpinBox *>(f.editor)) {
            connect(d, &QDoubleSpinBox::valueChanged, this, [this] { if (!m_updating) emit changed(); });
        } else if (auto *c = qobject_cast<QCheckBox *>(f.editor)) {
            connect(c, &QCheckBox::toggled, this, [this] { if (!m_updating) emit changed(); });
        } else if (auto *c = qobject_cast<QComboBox *>(f.editor)) {
            connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!m_updating) emit changed(); });
        }
    }
    setValues(initial);
    setMinimumWidth(400);
}

void PropertyEditor::setColorButton(QToolButton *b, uint32_t color)
{
    b->setProperty("pane_color", color);
    QPixmap pm(40, 14);
    pm.fill(qcolorFromPixel(color));
    b->setIcon(QIcon(pm));
    b->setIconSize(pm.size());
}

void PropertyEditor::setValues(const QVariantMap &v)
{
    m_updating = true;
    for (Field &f : m_fields) {
        QVariant val = v.contains(f.spec.id) ? v.value(f.spec.id) : QVariant(f.spec.def);
        if (f.spec.type == PropType::Color) {
            setColorButton(qobject_cast<QToolButton *>(f.editor), v.contains(f.spec.id) ? val.toUInt() : uint32_t(f.spec.def));
            continue;
        }
        if (auto *s = qobject_cast<QSpinBox *>(f.editor)) s->setValue(val.toInt());
        else if (auto *d = qobject_cast<QDoubleSpinBox *>(f.editor)) d->setValue(val.toDouble());
        else if (auto *c = qobject_cast<QCheckBox *>(f.editor)) c->setChecked(val.toBool());
        else if (auto *c = qobject_cast<QComboBox *>(f.editor)) c->setCurrentIndex(val.toInt());
    }
    m_updating = false;
}

QVariantMap PropertyEditor::params() const
{
    QVariantMap m;
    for (const Field &f : m_fields) {
        if (f.spec.type == PropType::Color) {
            m[f.spec.id] = f.editor->property("pane_color").toUInt();
            continue;
        }
        if (auto *s = qobject_cast<QSpinBox *>(f.editor)) m[f.spec.id] = s->value();
        else if (auto *d = qobject_cast<QDoubleSpinBox *>(f.editor)) m[f.spec.id] = d->value();
        else if (auto *c = qobject_cast<QCheckBox *>(f.editor)) m[f.spec.id] = c->isChecked();
        else if (auto *c = qobject_cast<QComboBox *>(f.editor)) m[f.spec.id] = c->currentIndex();
    }
    return m;
}


} // namespace pane
