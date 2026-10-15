// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/dialogs/EffectDialogs.h"

#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QRandomGenerator>
#include <QToolButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace pane {

// ------------------------------------------------------------ base

EffectDialogBase::EffectDialogBase(EffectSession &session, QWidget *parent) : QDialog(parent), m_session(session)
{
    setWindowTitle(session.effect().name());
    m_timer.setSingleShot(true);
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, [this] { m_session.update(params()); });
}

void EffectDialogBase::finishLayout(QLayout *content)
{
    auto *v = new QVBoxLayout(this);
    v->addLayout(content);
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(4);
    v->addWidget(m_progress);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    connect(&m_session, &EffectSession::progress, this, [this](int done, int total) {
        m_progress->setMaximum(std::max(1, total));
        m_progress->setValue(done);
    });
}

// ------------------------------------------------------------ generic

EffectDialog::EffectDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent)
    : EffectDialogBase(session, parent)
{
    m_editor = new PropertyEditor(session.effect().properties(), initial, this);
    connect(m_editor, &PropertyEditor::changed, this, [this] { changed(); });
    auto *l = new QVBoxLayout;
    l->addWidget(m_editor);
    finishLayout(l);
    setMinimumWidth(420);
}

QVariantMap EffectDialog::params() const { return m_editor->params(); }

// ------------------------------------------------------------ histogram view

HistogramView::HistogramView(QWidget *parent) : QWidget(parent) { setMinimumSize(200, 80); }

void HistogramView::setHistogram(const Histogram &h, std::array<bool, 3> channels)
{
    m_h = h;
    m_ch = channels;
    update();
}

void HistogramView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    p.fillRect(rect(), th.surface0);
    p.setPen(th.surface2);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!m_h.count) return;
    uint64_t maxv = 1;
    for (int c = 0; c < 3; ++c)
        if (m_ch[size_t(c)])
            for (int i = 0; i < 256; ++i) maxv = std::max(maxv, m_h.rgb[size_t(c)][size_t(i)]);
    const QColor colors[3] = {QColor(230, 60, 60, 140), QColor(60, 190, 60, 140), QColor(60, 100, 240, 140)};
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    for (int c = 0; c < 3; ++c) {
        if (!m_ch[size_t(c)]) continue;
        QPainterPath path;
        path.moveTo(r.bottomLeft());
        for (int i = 0; i < 256; ++i) {
            double h = std::log1p(double(m_h.rgb[size_t(c)][size_t(i)])) / std::log1p(double(maxv)); // log scale shows detail
            path.lineTo(r.left() + r.width() * i / 255.0, r.bottom() - h * r.height());
        }
        path.lineTo(r.bottomRight());
        path.closeSubpath();
        p.fillPath(path, colors[c]);
    }
}

// ------------------------------------------------------------ Levels

LevelsDialog::LevelsDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent)
    : EffectDialogBase(session, parent)
{
    m_input = Histogram::compute(session.source(), session.selection());
    QVariantList lv = initial.value(QStringLiteral("levels")).toList();
    if (!lv.isEmpty()) m_levels = levelsFromVariant(lv);

    auto *main = new QVBoxLayout;
    auto *top = new QHBoxLayout;
    auto *inBox = new QGroupBox(QStringLiteral("Input"), this);
    auto *inL = new QVBoxLayout(inBox);
    m_inView = new HistogramView(inBox);
    inL->addWidget(m_inView);
    auto *inRow = new QHBoxLayout;
    m_inLo = new QSpinBox(inBox);
    m_inLo->setRange(0, 254);
    m_inHi = new QSpinBox(inBox);
    m_inHi->setRange(1, 255);
    inRow->addWidget(new QLabel(QStringLiteral("Black:"), inBox));
    inRow->addWidget(m_inLo);
    inRow->addStretch();
    inRow->addWidget(new QLabel(QStringLiteral("White:"), inBox));
    inRow->addWidget(m_inHi);
    inL->addLayout(inRow);
    top->addWidget(inBox);

    auto *outBox = new QGroupBox(QStringLiteral("Output"), this);
    auto *outL = new QVBoxLayout(outBox);
    m_outView = new HistogramView(outBox);
    outL->addWidget(m_outView);
    auto *outRow = new QHBoxLayout;
    m_outLo = new QSpinBox(outBox);
    m_outLo->setRange(0, 255);
    m_gamma = new QDoubleSpinBox(outBox);
    m_gamma->setRange(0.1, 10.0);
    m_gamma->setDecimals(2);
    m_gamma->setSingleStep(0.05);
    m_outHi = new QSpinBox(outBox);
    m_outHi->setRange(0, 255);
    outRow->addWidget(new QLabel(QStringLiteral("Black:"), outBox));
    outRow->addWidget(m_outLo);
    outRow->addWidget(new QLabel(QStringLiteral("Gamma:"), outBox));
    outRow->addWidget(m_gamma);
    outRow->addWidget(new QLabel(QStringLiteral("White:"), outBox));
    outRow->addWidget(m_outHi);
    outL->addLayout(outRow);
    top->addWidget(outBox);
    main->addLayout(top);

    auto *bottom = new QHBoxLayout;
    const char *names[3] = {"Red", "Green", "Blue"};
    for (int i = 0; i < 3; ++i) {
        m_ch[i] = new QCheckBox(QString::fromLatin1(names[i]), this);
        m_ch[i]->setChecked(true);
        bottom->addWidget(m_ch[i]);
        connect(m_ch[i], &QCheckBox::toggled, this, [this] {
            syncFields();
            m_inView->setHistogram(m_input, {m_ch[0]->isChecked(), m_ch[1]->isChecked(), m_ch[2]->isChecked()});
            updateOutputHistogram();
        });
    }
    bottom->addStretch();
    auto *autoBtn = new QPushButton(QStringLiteral("Auto"), this);
    auto *resetBtn = new QPushButton(QStringLiteral("Reset"), this);
    bottom->addWidget(autoBtn);
    bottom->addWidget(resetBtn);
    main->addLayout(bottom);
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        m_levels = autoLevels(m_input);
        syncFields();
        updateOutputHistogram();
        changed();
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        m_levels = {};
        syncFields();
        updateOutputHistogram();
        changed();
    });
    for (QSpinBox *s : {m_inLo, m_inHi, m_outLo, m_outHi}) connect(s, &QSpinBox::valueChanged, this, &LevelsDialog::fieldsEdited);
    connect(m_gamma, &QDoubleSpinBox::valueChanged, this, &LevelsDialog::fieldsEdited);

    finishLayout(main);
    m_inView->setHistogram(m_input, {true, true, true});
    syncFields();
    updateOutputHistogram();
}

void LevelsDialog::syncFields()
{
    int first = 0;
    for (int i = 0; i < 3; ++i)
        if (m_ch[i]->isChecked()) { first = i; break; }
    const LevelChannel &l = m_levels[size_t(first)];
    m_updating = true;
    m_inLo->setValue(l.inLo);
    m_inHi->setValue(l.inHi);
    m_outLo->setValue(l.outLo);
    m_outHi->setValue(l.outHi);
    m_gamma->setValue(1.0 / l.exponent);
    m_updating = false;
}

void LevelsDialog::fieldsEdited()
{
    if (m_updating) return;
    for (int i = 0; i < 3; ++i) {
        if (!m_ch[i]->isChecked()) continue;
        LevelChannel &l = m_levels[size_t(i)];
        l.inLo = m_inLo->value();
        l.inHi = std::max(m_inHi->value(), l.inLo + 1);
        l.outLo = m_outLo->value();
        l.outHi = m_outHi->value();
        l.exponent = 1.0 / m_gamma->value();
    }
    updateOutputHistogram();
    changed();
}

void LevelsDialog::updateOutputHistogram()
{
    Histogram out;
    out.count = m_input.count;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) out.rgb[size_t(c)][m_levels[size_t(c)].apply(i)] += m_input.rgb[size_t(c)][size_t(i)];
    m_outView->setHistogram(out, {m_ch[0]->isChecked(), m_ch[1]->isChecked(), m_ch[2]->isChecked()});
}

QVariantMap LevelsDialog::params() const { return {{QStringLiteral("levels"), levelsToVariant(m_levels)}}; }

// ------------------------------------------------------------ Curve editor

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CurveEditor::setCurves(const std::vector<QList<QPointF>> &curves, const std::vector<QColor> &colors, int editable)
{
    m_curves = curves;
    m_colors = colors;
    m_editable = editable;
    update();
}

QList<QPointF> CurveEditor::editedPoints() const
{
    return m_curves.empty() ? QList<QPointF>{} : m_curves[size_t(m_editable)];
}

QRectF CurveEditor::plot() const
{
    double side = std::min(width(), height()) - 12;
    return QRectF((width() - side) / 2, (height() - side) / 2, side, side);
}

QPointF CurveEditor::toView(QPointF v) const
{
    QRectF r = plot();
    return {r.left() + v.x() / 255.0 * r.width(), r.bottom() - v.y() / 255.0 * r.height()};
}

QPointF CurveEditor::toValue(QPointF p) const
{
    QRectF r = plot();
    return {(p.x() - r.left()) / r.width() * 255.0, (r.bottom() - p.y()) / r.height() * 255.0};
}

void CurveEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    QRectF r = plot();
    p.fillRect(r, th.surface0);
    // Histogram backdrop (luminosity-ish: sum of channels).
    if (m_hist.count) {
        uint64_t maxv = 1;
        std::array<uint64_t, 256> sum{};
        for (int i = 0; i < 256; ++i) {
            sum[size_t(i)] = m_hist.rgb[0][size_t(i)] + m_hist.rgb[1][size_t(i)] + m_hist.rgb[2][size_t(i)];
            maxv = std::max(maxv, sum[size_t(i)]);
        }
        QColor hc = th.overlay0;
        hc.setAlpha(70);
        for (int i = 0; i < 256; ++i) {
            double h = std::log1p(double(sum[size_t(i)])) / std::log1p(double(maxv)) * r.height();
            p.fillRect(QRectF(r.left() + r.width() * i / 256.0, r.bottom() - h, r.width() / 256.0 + 0.5, h), hc);
        }
    }
    p.setPen(QPen(th.surface2, 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.drawLine(r.bottomLeft(), r.topRight());
    p.drawRect(r);
    p.setRenderHint(QPainter::Antialiasing);
    for (size_t c = 0; c < m_curves.size(); ++c) {
        auto table = splineTable(m_curves[c]);
        QPainterPath path;
        for (int i = 0; i < 256; ++i) {
            QPointF pt = toView(QPointF(i, table[size_t(i)]));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        QColor col = c < m_colors.size() ? m_colors[c] : th.text;
        p.setPen(QPen(col, int(c) == m_editable ? 2 : 1));
        p.drawPath(path);
    }
    if (!m_curves.empty()) {
        for (QPointF v : m_curves[size_t(m_editable)]) {
            p.setPen(QPen(th.text, 1));
            p.setBrush(th.base);
            p.drawEllipse(toView(v), 4, 4);
        }
    }
}

void CurveEditor::mousePressEvent(QMouseEvent *e)
{
    if (m_curves.empty()) return;
    auto &pts = m_curves[size_t(m_editable)];
    for (int i = 0; i < pts.size(); ++i)
        if (QLineF(toView(pts[i]), e->position()).length() < 7) {
            if (e->button() == Qt::RightButton && pts.size() > 2) {
                pts.removeAt(i);
                update();
                emit edited();
                return;
            }
            m_drag = i;
            return;
        }
    if (e->button() != Qt::LeftButton || !plot().contains(e->position())) return;
    QPointF v = toValue(e->position());
    v = QPointF(std::clamp(std::round(v.x()), 0.0, 255.0), std::clamp(std::round(v.y()), 0.0, 255.0));
    pts << v;
    std::sort(pts.begin(), pts.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    m_drag = int(pts.indexOf(v));
    update();
    emit edited();
}

void CurveEditor::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag < 0 || m_curves.empty()) return;
    auto &pts = m_curves[size_t(m_editable)];
    QPointF v = toValue(e->position());
    // Dragging a middle point far outside the graph removes it.
    QRectF outside = plot().adjusted(-30, -30, 30, 30);
    if (!outside.contains(e->position()) && pts.size() > 2 && m_drag > 0 && m_drag < pts.size() - 1) {
        pts.removeAt(m_drag);
        m_drag = -1;
        update();
        emit edited();
        return;
    }
    double lo = m_drag > 0 ? pts[m_drag - 1].x() + 1 : 0;
    double hi = m_drag < pts.size() - 1 ? pts[m_drag + 1].x() - 1 : 255;
    pts[m_drag] = QPointF(std::clamp(std::round(v.x()), lo, hi), std::clamp(std::round(v.y()), 0.0, 255.0));
    update();
    emit edited();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *) { m_drag = -1; }

// ------------------------------------------------------------ Curves dialog

CurvesDialog::CurvesDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent)
    : EffectDialogBase(session, parent)
{
    const QList<QPointF> identity{{0, 0}, {255, 255}};
    auto load = [&](const char *k) {
        QVariantList v = initial.value(QString::fromLatin1(k)).toList();
        return v.isEmpty() ? identity : pointsFromVariant(v);
    };
    m_lum = load("luminosity");
    m_rgb[0] = load("red");
    m_rgb[1] = load("green");
    m_rgb[2] = load("blue");

    auto *main = new QVBoxLayout;
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("Transfer map:"), this));
    m_mode = new QComboBox(this);
    m_mode->addItems({QStringLiteral("Luminosity"), QStringLiteral("RGB")});
    m_mode->setCurrentIndex(initial.value(QStringLiteral("mode")).toInt());
    top->addWidget(m_mode);
    top->addStretch();
    main->addLayout(top);
    m_editor = new CurveEditor(this);
    m_editor->setHistogram(Histogram::compute(session.source(), session.selection()));
    main->addWidget(m_editor, 1);
    auto *chRow = new QHBoxLayout;
    const char *names[3] = {"Red", "Green", "Blue"};
    for (int i = 0; i < 3; ++i) {
        m_ch[i] = new QCheckBox(QString::fromLatin1(names[i]), this);
        m_ch[i]->setChecked(true);
        chRow->addWidget(m_ch[i]);
        connect(m_ch[i], &QCheckBox::toggled, this, &CurvesDialog::refreshEditor);
    }
    chRow->addStretch();
    auto *reset = new QPushButton(QStringLiteral("Reset"), this);
    chRow->addWidget(reset);
    main->addLayout(chRow);
    connect(reset, &QPushButton::clicked, this, [this, identity] {
        m_lum = identity;
        for (auto &c : m_rgb) c = identity;
        refreshEditor();
        changed();
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        refreshEditor();
        changed();
    });
    connect(m_editor, &CurveEditor::edited, this, [this] {
        QList<QPointF> pts = m_editor->editedPoints();
        if (m_mode->currentIndex() == 0) m_lum = pts;
        else
            for (int i = 0; i < 3; ++i)
                if (m_ch[i]->isChecked()) m_rgb[i] = pts;
        changed();
    });
    finishLayout(main);
    resize(380, 460);
    refreshEditor();
}

void CurvesDialog::refreshEditor()
{
    bool rgb = m_mode->currentIndex() == 1;
    for (QCheckBox *c : m_ch) c->setVisible(rgb);
    const Theme &th = ThemeEngine::instance().current();
    if (!rgb) {
        m_editor->setCurves({m_lum}, {th.text}, 0);
        return;
    }
    const QColor colors[3] = {QColor(230, 60, 60), QColor(60, 190, 60), QColor(60, 110, 240)};
    std::vector<QList<QPointF>> curves;
    std::vector<QColor> cols;
    int editable = -1;
    for (int i = 0; i < 3; ++i) {
        curves.push_back(m_rgb[i]);
        cols.push_back(colors[i]);
        if (editable < 0 && m_ch[i]->isChecked()) editable = i;
    }
    m_editor->setCurves(curves, cols, std::max(0, editable));
}

QVariantMap CurvesDialog::params() const
{
    return {{QStringLiteral("mode"), m_mode->currentIndex()},
            {QStringLiteral("luminosity"), pointsToVariant(m_lum)},
            {QStringLiteral("red"), pointsToVariant(m_rgb[0])},
            {QStringLiteral("green"), pointsToVariant(m_rgb[1])},
            {QStringLiteral("blue"), pointsToVariant(m_rgb[2])}};
}

} // namespace pane
