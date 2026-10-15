#pragma once

#include "core/History.h"
#include "core/Layer.h"
#include "core/Selection.h"

#include <QMap>
#include <QObject>
#include <QSize>
#include <vector>

namespace pane {

// A complete, cheap-to-copy snapshot of a document's content (tiles are shared).
struct DocumentState {
    QSize size;
    std::vector<Layer> layers;
    int activeLayer = 0;
    Selection selection;
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QSize size, QObject *parent = nullptr);
    // A detached copy of the current content for background work (autosave).
    // Shares tiles; does not consume an "Untitled N" name.
    std::unique_ptr<Document> snapshot() const;
    ~Document() override;

    // Creates a document with one opaque white "Background" layer.
    static std::unique_ptr<Document> createBlank(QSize size, Pixel background = 0xFFFFFFFF);

    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QRect bounds() const { return {QPoint(), m_size}; }

    double dpiX() const { return m_dpiX; }
    double dpiY() const { return m_dpiY; }
    void setDpi(double x, double y) { m_dpiX = x; m_dpiY = y; }

    // Layers: index 0 is the bottom layer.
    int layerCount() const { return int(m_layers.size()); }
    Layer &layer(int i) { return m_layers[size_t(i)]; }
    const Layer &layer(int i) const { return m_layers[size_t(i)]; }
    int activeLayerIndex() const { return m_active; }
    Layer &activeLayer() { return m_layers[size_t(m_active)]; }
    void setActiveLayerIndex(int i);

    // Raw mutators; callers record history themselves (see Commands.h).
    void insertLayer(int index, Layer layer);
    Layer removeLayer(int index);
    void moveLayer(int from, int to);
    void setLayerProperties(int index, const LayerProperties &p);
    QString nextLayerName() const;

    Selection &selection() { return m_selection; }
    const Selection &selection() const { return m_selection; }
    void setSelection(const Selection &s);
    void notifySelectionChanged() { emit selectionChanged(); }

    History &history() { return m_history; }

    DocumentState captureState() const;
    void restoreState(const DocumentState &s);

    // Flattens all visible layers into a new surface (straight alpha).
    Surface flatten() const;
    QImage flattenToImage() const;

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &p);
    QString displayName() const;
    QString fileTypeId() const { return m_fileTypeId; }
    void setFileTypeId(const QString &id) { m_fileTypeId = id; }
    bool isDirty() const { return !m_history.isClean(); }
    void markClean() { m_history.markClean(); }
    void markDirty() { m_history.markDirty(); }
    void setUntitledName(const QString &n) { m_untitledName = n; }
    // Increments on every history change (edit, undo, redo).
    uint64_t revision() const { return m_revision; }

    QMap<QString, QString> &metadata() { return m_metadata; }

    // Notify listeners that pixels in `region` (image coordinates) changed.
    void notifyPixelsChanged(const QRect &region) { emit pixelsChanged(region); }
    void notifyAllPixelsChanged() { emit pixelsChanged(bounds()); }

signals:
    void pixelsChanged(const QRect &region);
    void layersChanged();            // count/order/properties changed
    void activeLayerChanged(int index);
    void selectionChanged();
    void sizeChanged(QSize size);
    void dirtyChanged();
    void filePathChanged();

private:
    QSize m_size;
    double m_dpiX = 96, m_dpiY = 96;
    std::vector<Layer> m_layers;
    int m_active = 0;
    Selection m_selection;
    History m_history;
    QString m_filePath, m_fileTypeId;
    QString m_untitledName;
    uint64_t m_revision = 0;
    struct NoName {};
    Document(QSize size, NoName);
    QMap<QString, QString> m_metadata;
};

// History item that swaps whole-document snapshots. Used for structural changes
// (layers, resize, rotate, ...). Snapshots share tiles, so this is cheap.
class StateHistoryItem final : public HistoryItem {
public:
    StateHistoryItem(QString name, QString glyph, DocumentState before, DocumentState after)
        : HistoryItem(std::move(name), std::move(glyph)), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo(Document &doc) override { doc.restoreState(m_before); }
    void redo(Document &doc) override { doc.restoreState(m_after); }

private:
    DocumentState m_before, m_after;
};

// History item for a selection-only change.
class SelectionHistoryItem final : public HistoryItem {
public:
    SelectionHistoryItem(QString name, QString glyph, Selection before, Selection after)
        : HistoryItem(std::move(name), std::move(glyph)), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo(Document &doc) override { doc.setSelection(m_before); }
    void redo(Document &doc) override { doc.setSelection(m_after); }

private:
    Selection m_before, m_after;
};

} // namespace pane
