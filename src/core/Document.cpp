// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Document.h"

#include "core/Glyphs.h"
#include "core/Parallel.h"

#include <QFileInfo>

namespace pane {

namespace {
int g_untitledCounter = 0;
}

Document::Document(QSize size, QObject *parent)
    : QObject(parent), m_size(size), m_history(*this)
{
    m_selection.setImageSize(size);
    m_untitledName = g_untitledCounter++ == 0 ? QStringLiteral("Untitled")
                                              : QStringLiteral("Untitled %1").arg(g_untitledCounter);
    connect(&m_history, &History::changed, this, [this] {
        ++m_revision;
        emit dirtyChanged();
    });
}

Document::Document(QSize size, NoName) : QObject(nullptr), m_size(size), m_history(*this)
{
    m_selection.setImageSize(size);
}

std::unique_ptr<Document> Document::snapshot() const
{
    std::unique_ptr<Document> d(new Document(m_size, NoName{}));
    d->m_layers = m_layers;
    d->m_active = m_active;
    d->m_dpiX = m_dpiX;
    d->m_dpiY = m_dpiY;
    d->m_filePath = m_filePath;
    d->m_fileTypeId = m_fileTypeId;
    d->m_untitledName = m_untitledName;
    return d;
}

Document::~Document() = default;

std::unique_ptr<Document> Document::createBlank(QSize size, Pixel background)
{
    auto doc = std::make_unique<Document>(size);
    Layer bg;
    bg.props.name = QStringLiteral("Background");
    bg.surface = Surface(size.width(), size.height());
    if (background) bg.surface.fill(background);
    doc->m_layers.push_back(std::move(bg));
    doc->m_history.reset(std::make_unique<BaseHistoryItem>(QStringLiteral("New Image"), glyphs::NewImage));
    return doc;
}

void Document::setActiveLayerIndex(int i)
{
    i = std::clamp(i, 0, layerCount() - 1);
    if (i == m_active) return;
    m_active = i;
    emit activeLayerChanged(i);
}

void Document::insertLayer(int index, Layer layer)
{
    index = std::clamp(index, 0, layerCount());
    m_layers.insert(m_layers.begin() + index, std::move(layer));
    m_active = index;
    emit layersChanged();
    emit activeLayerChanged(m_active);
    notifyAllPixelsChanged();
}

Layer Document::removeLayer(int index)
{
    Layer l = std::move(m_layers[size_t(index)]);
    m_layers.erase(m_layers.begin() + index);
    m_active = std::clamp(m_active >= index ? m_active - 1 : m_active, 0, layerCount() - 1);
    emit layersChanged();
    emit activeLayerChanged(m_active);
    notifyAllPixelsChanged();
    return l;
}

void Document::moveLayer(int from, int to)
{
    if (from == to) return;
    Layer l = std::move(m_layers[size_t(from)]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(l));
    m_active = to;
    emit layersChanged();
    emit activeLayerChanged(m_active);
    notifyAllPixelsChanged();
}

void Document::setLayerProperties(int index, const LayerProperties &p)
{
    m_layers[size_t(index)].props = p;
    emit layersChanged();
    notifyAllPixelsChanged();
}

QString Document::nextLayerName() const
{
    for (int n = layerCount() + 1;; ++n) {
        QString name = QStringLiteral("Layer %1").arg(n);
        bool taken = std::any_of(m_layers.begin(), m_layers.end(), [&](const Layer &l) { return l.props.name == name; });
        if (!taken) return name;
    }
}

void Document::setSelection(const Selection &s)
{
    m_selection = s;
    m_selection.setImageSize(m_size);
    emit selectionChanged();
}

DocumentState Document::captureState() const
{
    return DocumentState{m_size, m_layers, m_active, m_selection};
}

void Document::restoreState(const DocumentState &s)
{
    bool resized = s.size != m_size;
    m_size = s.size;
    m_layers = s.layers;
    m_active = std::clamp(s.activeLayer, 0, layerCount() - 1);
    m_selection = s.selection;
    m_selection.setImageSize(m_size);
    if (resized) emit sizeChanged(m_size);
    emit layersChanged();
    emit activeLayerChanged(m_active);
    emit selectionChanged();
    notifyAllPixelsChanged();
}

Surface Document::flatten() const
{
    Surface out(width(), height());
    // Tiles are independent; mutableTile() on distinct indices is thread-safe.
    parallelFor(out.tileCount(), [&](int idx) {
        bool any = std::any_of(m_layers.begin(), m_layers.end(), [idx](const Layer &l) {
            return l.props.visible && l.props.opacity && l.surface.tile(idx);
        });
        if (!any) return;
        Tile *dst = out.mutableTile(idx);
        QRect tr = out.tileRect(idx);
        for (const Layer &l : m_layers) {
            if (!l.props.visible || l.props.opacity == 0) continue;
            const Tile *src = l.surface.tile(idx);
            if (!src) continue;
            for (int y = 0; y < tr.height(); ++y)
                blendRow(l.props.blendMode, &dst->px[y * TileSize], &src->px[y * TileSize], tr.width(), l.props.opacity);
        }
    });
    return out;
}

QImage Document::flattenToImage() const { return flatten().toImage(); }

void Document::setFilePath(const QString &p)
{
    m_filePath = p;
    emit filePathChanged();
}

QString Document::displayName() const
{
    return m_filePath.isEmpty() ? m_untitledName : QFileInfo(m_filePath).fileName();
}

} // namespace pane
