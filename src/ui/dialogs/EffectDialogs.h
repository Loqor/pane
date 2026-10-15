#pragma once

#include "effects/Adjustments.h"
#include "effects/EffectSession.h"
#include "ui/PropertyEditor.h"

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QProgressBar;
class QSpinBox;
class QToolButton;

namespace pane {

// Base for effect dialogs: debounced live preview + progress bar.
class EffectDialogBase : public QDialog {
    Q_OBJECT
public:
    EffectDialogBase(EffectSession &session, QWidget *parent);
    virtual QVariantMap params() const = 0;

protected:
    void finishLayout(QLayout *content); // adds progress + buttons
    void changed() { m_timer.start(); }
    EffectSession &m_session;

private:
    QTimer m_timer;
    QProgressBar *m_progress = nullptr;
};

// Auto-generated from the effect's property list.
class EffectDialog : public EffectDialogBase {
    Q_OBJECT
public:
    EffectDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent = nullptr);
    QVariantMap params() const override;

private:
    PropertyEditor *m_editor;
};

// Draws a 256-bin histogram (optionally per channel).
class HistogramView : public QWidget {
    Q_OBJECT
public:
    explicit HistogramView(QWidget *parent = nullptr);
    void setHistogram(const Histogram &h, std::array<bool, 3> channels);
    QSize sizeHint() const override { return {256, 100}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Histogram m_h;
    std::array<bool, 3> m_ch{true, true, true};
};

class LevelsDialog : public EffectDialogBase {
    Q_OBJECT
public:
    LevelsDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent = nullptr);
    QVariantMap params() const override;

private:
    void syncFields();
    void fieldsEdited();
    void updateOutputHistogram();
    std::array<LevelChannel, 3> m_levels;
    Histogram m_input;
    QCheckBox *m_ch[3];
    QSpinBox *m_inLo, *m_inHi, *m_outLo, *m_outHi;
    QDoubleSpinBox *m_gamma;
    HistogramView *m_inView, *m_outView;
    bool m_updating = false;
};

// Interactive curve editor.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);
    // curves[0] = luminosity or the edited curve set; colors per curve.
    void setCurves(const std::vector<QList<QPointF>> &curves, const std::vector<QColor> &colors, int editable);
    QList<QPointF> editedPoints() const;
    void setHistogram(const Histogram &h) { m_hist = h; update(); }
    QSize sizeHint() const override { return {280, 280}; }
    QSize minimumSizeHint() const override { return {200, 200}; }

signals:
    void edited();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF plot() const;
    QPointF toView(QPointF v) const;
    QPointF toValue(QPointF p) const;
    std::vector<QList<QPointF>> m_curves;
    std::vector<QColor> m_colors;
    int m_editable = 0;
    int m_drag = -1;
    Histogram m_hist;
};

class CurvesDialog : public EffectDialogBase {
    Q_OBJECT
public:
    CurvesDialog(EffectSession &session, const QVariantMap &initial, QWidget *parent = nullptr);
    QVariantMap params() const override;

private:
    void refreshEditor();
    QComboBox *m_mode;
    QCheckBox *m_ch[3];
    CurveEditor *m_editor;
    QList<QPointF> m_lum, m_rgb[3];
};

} // namespace pane
