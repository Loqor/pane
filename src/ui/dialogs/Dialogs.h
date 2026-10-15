#pragma once

#include "core/Document.h"
#include "core/Resample.h"
#include "io/FileType.h"

#include <atomic>
#include <memory>

#include <QDialog>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QLineEdit;
class QSlider;
class QLabel;
class QButtonGroup;

namespace pane {

class NewImageDialog : public QDialog {
    Q_OBJECT
public:
    NewImageDialog(QSize suggested, double dpi, QWidget *parent = nullptr);
    QSize imageSize() const;
    double dpi() const;

private:
    QSpinBox *m_w, *m_h;
    QDoubleSpinBox *m_dpi;
    QCheckBox *m_aspect;
    double m_ratio;
    bool m_updating = false;
};

class ResizeImageDialog : public QDialog {
    Q_OBJECT
public:
    ResizeImageDialog(QSize current, double dpi, QWidget *parent = nullptr);
    QSize imageSize() const;
    Resampling resampling() const;

private:
    void sync(QObject *source);
    QSize m_orig;
    QRadioButton *m_byPercent, *m_byAbsolute;
    QSpinBox *m_percent, *m_w, *m_h;
    QCheckBox *m_aspect;
    QComboBox *m_resampling;
    QLabel *m_newSize;
    bool m_updating = false;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    CanvasSizeDialog(QSize current, QWidget *parent = nullptr);
    QSize canvasSize() const;
    int anchor() const;

private:
    void sync(QObject *source);
    QSize m_orig;
    QRadioButton *m_byPercent, *m_byAbsolute;
    QSpinBox *m_percent, *m_w, *m_h;
    QCheckBox *m_aspect;
    QButtonGroup *m_anchor;
    bool m_updating = false;
};

// Edits a layer's properties with live preview; restores them on cancel.
class LayerPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    LayerPropertiesDialog(Document &doc, int layer, QWidget *parent = nullptr);
    LayerProperties original() const { return m_orig; }
    LayerProperties result() const;
    void reject() override;

private:
    void preview();
    Document &m_doc;
    int m_layer;
    LayerProperties m_orig;
    QLineEdit *m_name;
    QCheckBox *m_visible;
    QComboBox *m_blend;
    QSlider *m_opacity;
    QSpinBox *m_opacitySpin;
};

// Save Configuration: the file type's options, the encoded file size and (for
// lossy formats) a preview of the decoded result. Encoding runs in the background.
class SaveConfigDialog : public QDialog {
    Q_OBJECT
public:
    SaveConfigDialog(const FileType *type, const Document &doc, const QImage &flat, const QVariantMap &initial,
                     QWidget *parent = nullptr);
    ~SaveConfigDialog() override;
    QVariantMap options() const;

private:
    void encode();
    const FileType *m_type;
    const Document &m_doc;
    QImage m_flat;
    class PropertyEditor *m_editor;
    QLabel *m_size, *m_preview;
    QTimer *m_timer;
    int m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_alive;
};

// Save / Don't Save / Cancel for one or more documents.
class UnsavedChangesDialog : public QDialog {
    Q_OBJECT
public:
    enum Result { Save = 1, Discard = 2, CancelAction = 0 };
    UnsavedChangesDialog(const QStringList &names, const QList<QImage> &thumbs, QWidget *parent = nullptr);
};

} // namespace pane
