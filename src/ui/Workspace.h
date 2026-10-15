// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Document.h"
#include "render/Compositor.h"

#include <QPointF>
#include <memory>

namespace pane {

// One open image: the document, its compositor and per-document view state.
struct Workspace {
    explicit Workspace(std::unique_ptr<Document> d)
        : doc(std::move(d)), compositor(std::make_unique<Compositor>(*doc))
    {
    }
    std::unique_ptr<Document> doc;
    std::unique_ptr<Compositor> compositor;
    double zoom = 1.0;
    QPointF scroll;          // scrollbar values
    bool viewInitialized = false;
    QImage thumbnail;
    QString recoveryId;            // autosave file id (assigned on first autosave)
    uint64_t autosavedRevision = 0; // document revision captured by the last autosave
};

} // namespace pane
