// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/dialogs/Dialogs.h"

#include "io/Formats.h"
#include "ui/PropertyEditor.h"

#include <QApplication>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace pane {

namespace {

QSpinBox *sizeSpin(QWidget *parent, int value)
{
    auto *s = new QSpinBox(parent);
    s->setRange(1, 65535);
    s->setValue(value);
    s->setSuffix(QStringLiteral(" px"));
    s->setMinimumWidth(110);
    return s;
}

QDialogButtonBox *okCancel(QDialog *d)
{
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    QObject::connect(bb, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return bb;
}

QString sizeText(QSize s)
{
    double mb = double(s.width()) * s.height() * 4 / (1024.0 * 1024.0);
    return QStringLiteral("%1 × %2 pixels (%3 MB per layer)").arg(s.width()).arg(s.height()).arg(mb, 0, 'f', 1);
}

} // namespace

// ------------------------------------------------------------ New Image

NewImageDialog::NewImageDialog(QSize suggested, double dpi, QWidget *parent)
    : QDialog(parent), m_ratio(double(suggested.width()) / suggested.height())
{
    setWindowTitle(QStringLiteral("New Image"));
    auto *v = new QVBoxLayout(this);
    auto *pixel = new QGroupBox(QStringLiteral("Pixel size"), this);
    auto *f = new QFormLayout(pixel);
    m_aspect = new QCheckBox(QStringLiteral("Maintain aspect ratio"), pixel);
    m_w = sizeSpin(pixel, suggested.width());
    m_h = sizeSpin(pixel, suggested.height());
    f->addRow(m_aspect);
    f->addRow(QStringLiteral("Width:"), m_w);
    f->addRow(QStringLiteral("Height:"), m_h);
    v->addWidget(pixel);
    auto *res = new QFormLayout;
    m_dpi = new QDoubleSpinBox(this);
    m_dpi->setRange(1, 10000);
    m_dpi->setDecimals(2);
    m_dpi->setValue(dpi);
    m_dpi->setSuffix(QStringLiteral(" pixels/inch"));
    res->addRow(QStringLiteral("Resolution:"), m_dpi);
    v->addLayout(res);
    auto *info = new QLabel(sizeText(suggested), this);
    v->addWidget(info);
    v->addWidget(okCancel(this));
    auto update = [this, info](bool fromW) {
        if (m_updating) return;
        m_updating = true;
        if (m_aspect->isChecked()) {
            if (fromW) m_h->setValue(std::max(1, int(std::lround(m_w->value() / m_ratio))));
            else m_w->setValue(std::max(1, int(std::lround(m_h->value() * m_ratio))));
        }
        info->setText(sizeText(imageSize()));
        m_updating = false;
    };
    connect(m_w, &QSpinBox::valueChanged, this, [update] { update(true); });
    connect(m_h, &QSpinBox::valueChanged, this, [update] { update(false); });
    connect(m_aspect, &QCheckBox::toggled, this, [this] { m_ratio = double(m_w->value()) / m_h->value(); });
}

QSize NewImageDialog::imageSize() const { return {m_w->value(), m_h->value()}; }
double NewImageDialog::dpi() const { return m_dpi->value(); }

// ------------------------------------------------------------ Resize

ResizeImageDialog::ResizeImageDialog(QSize current, double, QWidget *parent) : QDialog(parent), m_orig(current)
{
    setWindowTitle(QStringLiteral("Resize Image"));
    auto *v = new QVBoxLayout(this);
    auto *rf = new QFormLayout;
    m_resampling = new QComboBox(this);
    for (auto r : {Resampling::BestQuality, Resampling::Bicubic, Resampling::Bilinear, Resampling::NearestNeighbor})
        m_resampling->addItem(resamplingName(r), int(r));
    rf->addRow(QStringLiteral("Resampling:"), m_resampling);
    v->addLayout(rf);

    m_byPercent = new QRadioButton(QStringLiteral("By percentage:"), this);
    m_percent = new QSpinBox(this);
    m_percent->setRange(1, 10000);
    m_percent->setValue(100);
    m_percent->setSuffix(QStringLiteral(" %"));
    auto *ph = new QHBoxLayout;
    ph->addWidget(m_byPercent);
    ph->addWidget(m_percent);
    ph->addStretch();
    v->addLayout(ph);

    m_byAbsolute = new QRadioButton(QStringLiteral("By absolute size:"), this);
    v->addWidget(m_byAbsolute);
    auto *abs = new QGroupBox(this);
    auto *af = new QFormLayout(abs);
    m_aspect = new QCheckBox(QStringLiteral("Maintain aspect ratio"), abs);
    m_aspect->setChecked(true);
    m_w = sizeSpin(abs, current.width());
    m_h = sizeSpin(abs, current.height());
    af->addRow(m_aspect);
    af->addRow(QStringLiteral("Width:"), m_w);
    af->addRow(QStringLiteral("Height:"), m_h);
    v->addWidget(abs);
    m_newSize = new QLabel(sizeText(current), this);
    v->addWidget(m_newSize);
    v->addWidget(okCancel(this));

    m_byPercent->setChecked(true);
    abs->setEnabled(false);
    connect(m_byPercent, &QRadioButton::toggled, this, [this, abs](bool on) {
        m_percent->setEnabled(on);
        abs->setEnabled(!on);
        sync(m_percent);
    });
    connect(m_percent, &QSpinBox::valueChanged, this, [this] { sync(m_percent); });
    connect(m_w, &QSpinBox::valueChanged, this, [this] { sync(m_w); });
    connect(m_h, &QSpinBox::valueChanged, this, [this] { sync(m_h); });
}

void ResizeImageDialog::sync(QObject *src)
{
    if (m_updating) return;
    m_updating = true;
    double ratio = double(m_orig.width()) / m_orig.height();
    if (m_byPercent->isChecked()) {
        double p = m_percent->value() / 100.0;
        m_w->setValue(std::max(1, int(std::lround(m_orig.width() * p))));
        m_h->setValue(std::max(1, int(std::lround(m_orig.height() * p))));
    } else if (m_aspect->isChecked()) {
        if (src == m_w) m_h->setValue(std::max(1, int(std::lround(m_w->value() / ratio))));
        else if (src == m_h) m_w->setValue(std::max(1, int(std::lround(m_h->value() * ratio))));
    }
    m_newSize->setText(sizeText(imageSize()));
    m_updating = false;
}

QSize ResizeImageDialog::imageSize() const { return {m_w->value(), m_h->value()}; }
Resampling ResizeImageDialog::resampling() const { return Resampling(m_resampling->currentData().toInt()); }

// ------------------------------------------------------------ Canvas Size

CanvasSizeDialog::CanvasSizeDialog(QSize current, QWidget *parent) : QDialog(parent), m_orig(current)
{
    setWindowTitle(QStringLiteral("Canvas Size"));
    auto *v = new QVBoxLayout(this);
    m_byPercent = new QRadioButton(QStringLiteral("By percentage:"), this);
    m_percent = new QSpinBox(this);
    m_percent->setRange(1, 10000);
    m_percent->setValue(100);
    m_percent->setSuffix(QStringLiteral(" %"));
    auto *ph = new QHBoxLayout;
    ph->addWidget(m_byPercent);
    ph->addWidget(m_percent);
    ph->addStretch();
    v->addLayout(ph);
    m_byAbsolute = new QRadioButton(QStringLiteral("By absolute size:"), this);
    v->addWidget(m_byAbsolute);
    auto *abs = new QGroupBox(this);
    auto *af = new QFormLayout(abs);
    m_aspect = new QCheckBox(QStringLiteral("Maintain aspect ratio"), abs);
    m_w = sizeSpin(abs, current.width());
    m_h = sizeSpin(abs, current.height());
    af->addRow(m_aspect);
    af->addRow(QStringLiteral("Width:"), m_w);
    af->addRow(QStringLiteral("Height:"), m_h);
    v->addWidget(abs);

    auto *anchorBox = new QGroupBox(QStringLiteral("Anchor"), this);
    auto *grid = new QGridLayout(anchorBox);
    grid->setSpacing(2);
    m_anchor = new QButtonGroup(this);
    static const char32_t arrows[9] = {U'↖', U'↑', U'↗', U'←', U'•', U'→', U'↙', U'↓', U'↘'};
    for (int i = 0; i < 9; ++i) {
        auto *b = new QToolButton(anchorBox);
        b->setCheckable(true);
        b->setFixedSize(30, 30);
        b->setText(QString::fromUcs4(&arrows[i], 1));
        m_anchor->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    m_anchor->button(4)->setChecked(true);
    auto *ah = new QHBoxLayout;
    ah->addStretch();
    ah->addWidget(anchorBox);
    ah->addStretch();
    v->addLayout(ah);
    v->addWidget(okCancel(this));

    m_byAbsolute->setChecked(true);
    m_percent->setEnabled(false);
    connect(m_byPercent, &QRadioButton::toggled, this, [this, abs](bool on) {
        m_percent->setEnabled(on);
        abs->setEnabled(!on);
        sync(m_percent);
    });
    connect(m_percent, &QSpinBox::valueChanged, this, [this] { sync(m_percent); });
    connect(m_w, &QSpinBox::valueChanged, this, [this] { sync(m_w); });
    connect(m_h, &QSpinBox::valueChanged, this, [this] { sync(m_h); });
}

void CanvasSizeDialog::sync(QObject *src)
{
    if (m_updating) return;
    m_updating = true;
    double ratio = double(m_orig.width()) / m_orig.height();
    if (m_byPercent->isChecked()) {
        double p = m_percent->value() / 100.0;
        m_w->setValue(std::max(1, int(std::lround(m_orig.width() * p))));
        m_h->setValue(std::max(1, int(std::lround(m_orig.height() * p))));
    } else if (m_aspect->isChecked()) {
        if (src == m_w) m_h->setValue(std::max(1, int(std::lround(m_w->value() / ratio))));
        else if (src == m_h) m_w->setValue(std::max(1, int(std::lround(m_h->value() * ratio))));
    }
    m_updating = false;
}

QSize CanvasSizeDialog::canvasSize() const { return {m_w->value(), m_h->value()}; }
int CanvasSizeDialog::anchor() const { return m_anchor->checkedId(); }

// ------------------------------------------------------------ Layer Properties

LayerPropertiesDialog::LayerPropertiesDialog(Document &doc, int layer, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_layer(layer), m_orig(doc.layer(layer).props)
{
    setWindowTitle(QStringLiteral("Layer Properties"));
    auto *v = new QVBoxLayout(this);
    auto *gen = new QGroupBox(QStringLiteral("General"), this);
    auto *gf = new QFormLayout(gen);
    m_name = new QLineEdit(m_orig.name, gen);
    m_visible = new QCheckBox(QStringLiteral("Visible"), gen);
    m_visible->setChecked(m_orig.visible);
    gf->addRow(QStringLiteral("Name:"), m_name);
    gf->addRow(m_visible);
    v->addWidget(gen);
    auto *blend = new QGroupBox(QStringLiteral("Blending"), this);
    auto *bf = new QFormLayout(blend);
    m_blend = new QComboBox(blend);
    for (int i = 0; i < BlendModeCount; ++i) m_blend->addItem(blendModeName(BlendMode(i)));
    m_blend->setCurrentIndex(int(m_orig.blendMode));
    m_opacity = new QSlider(Qt::Horizontal, blend);
    m_opacity->setRange(0, 255);
    m_opacity->setValue(m_orig.opacity);
    m_opacitySpin = new QSpinBox(blend);
    m_opacitySpin->setRange(0, 255);
    m_opacitySpin->setValue(m_orig.opacity);
    auto *oh = new QHBoxLayout;
    oh->addWidget(m_opacity, 1);
    oh->addWidget(m_opacitySpin);
    bf->addRow(QStringLiteral("Mode:"), m_blend);
    bf->addRow(QStringLiteral("Opacity:"), oh);
    v->addWidget(blend);
    v->addWidget(okCancel(this));
    connect(m_opacity, &QSlider::valueChanged, m_opacitySpin, &QSpinBox::setValue);
    connect(m_opacitySpin, &QSpinBox::valueChanged, m_opacity, &QSlider::setValue);
    connect(m_opacity, &QSlider::valueChanged, this, &LayerPropertiesDialog::preview);
    connect(m_blend, &QComboBox::currentIndexChanged, this, &LayerPropertiesDialog::preview);
    connect(m_visible, &QCheckBox::toggled, this, &LayerPropertiesDialog::preview);
    connect(m_name, &QLineEdit::textChanged, this, &LayerPropertiesDialog::preview);
    m_name->selectAll();
}

LayerProperties LayerPropertiesDialog::result() const
{
    LayerProperties p;
    p.name = m_name->text();
    p.visible = m_visible->isChecked();
    p.opacity = uint8_t(m_opacity->value());
    p.blendMode = BlendMode(m_blend->currentIndex());
    return p;
}

void LayerPropertiesDialog::preview() { m_doc.setLayerProperties(m_layer, result()); }

void LayerPropertiesDialog::reject()
{
    m_doc.setLayerProperties(m_layer, m_orig);
    QDialog::reject();
}

// ------------------------------------------------------------ Save configuration

SaveConfigDialog::SaveConfigDialog(const FileType *type, const Document &doc, const QImage &flat, const QVariantMap &initial,
                                   QWidget *parent)
    : QDialog(parent), m_type(type), m_doc(doc), m_flat(flat), m_alive(std::make_shared<std::atomic<bool>>(true))
{
    setWindowTitle(QStringLiteral("Save Configuration — %1").arg(type->name()));
    auto *v = new QVBoxLayout(this);
    m_editor = new PropertyEditor(type->saveOptions(), initial, this);
    v->addWidget(m_editor);
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(320, 200);
    m_preview->setVisible(type->isLossy());
    v->addWidget(m_preview, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("File size:"), this));
    m_size = new QLabel(QStringLiteral("…"), this);
    row->addWidget(m_size, 1);
    v->addLayout(row);
    v->addWidget(okCancel(this));
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &SaveConfigDialog::encode);
    connect(m_editor, &PropertyEditor::changed, m_timer, qOverload<>(&QTimer::start));
    encode();
}

SaveConfigDialog::~SaveConfigDialog() { *m_alive = false; }

QVariantMap SaveConfigDialog::options() const { return m_editor->params(); }

void SaveConfigDialog::encode()
{
    const int gen = ++m_generation;
    m_size->setText(QStringLiteral("Calculating…"));
    QVariantMap opts = options();
    const FileType *type = m_type;
    QImage flat = m_flat;
    auto alive = m_alive;
    QPointer<SaveConfigDialog> self(this);
    const Document *doc = &m_doc;
    (void)QtConcurrent::run([=] {
        QByteArray data;
        QImage preview;
        QString error;
        try {
            data = type->save(*doc, flat, opts);
            if (type->isLossy()) preview = type->load(data)->flattenToImage();
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (!*alive) return;
        QMetaObject::invokeMethod(qApp, [=] {
            if (!self || gen != self->m_generation) return; // a newer encode superseded this one
            if (!error.isEmpty()) {
                self->m_size->setText(error);
                return;
            }
            self->m_size->setText(QLocale().formattedDataSize(data.size()));
            if (!preview.isNull())
                self->m_preview->setPixmap(QPixmap::fromImage(preview.scaled(self->m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        });
    });
}

// ------------------------------------------------------------ Unsaved changes

UnsavedChangesDialog::UnsavedChangesDialog(const QStringList &names, const QList<QImage> &thumbs, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Unsaved Changes"));
    auto *v = new QVBoxLayout(this);
    auto *title = new QLabel(names.size() == 1 ? QStringLiteral("<b>Save changes to \"%1\" before closing?</b>").arg(names[0].toHtmlEscaped())
                                               : QStringLiteral("<b>Save changes to these %1 images before closing?</b>").arg(names.size()),
                             this);
    v->addWidget(title);
    v->addWidget(new QLabel(QStringLiteral("If you don't save, your changes will be lost."), this));
    for (int i = 0; i < names.size(); ++i) {
        auto *row = new QHBoxLayout;
        auto *thumb = new QLabel(this);
        if (i < thumbs.size() && !thumbs[i].isNull()) thumb->setPixmap(QPixmap::fromImage(thumbs[i].scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        thumb->setFixedSize(52, 52);
        row->addWidget(thumb);
        row->addWidget(new QLabel(names[i], this), 1);
        v->addLayout(row);
    }
    auto *bb = new QDialogButtonBox(this);
    auto *save = bb->addButton(names.size() == 1 ? QStringLiteral("Save") : QStringLiteral("Save All"), QDialogButtonBox::AcceptRole);
    auto *discard = bb->addButton(QStringLiteral("Don't Save"), QDialogButtonBox::DestructiveRole);
    auto *cancel = bb->addButton(QDialogButtonBox::Cancel);
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, [this] { done(Save); });
    connect(discard, &QPushButton::clicked, this, [this] { done(Discard); });
    connect(cancel, &QPushButton::clicked, this, [this] { done(CancelAction); });
    v->addWidget(bb);
}

} // namespace pane
