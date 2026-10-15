// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "core/Document.h"
#include "core/Property.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>
#include <stdexcept>

namespace pane {

struct FileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Save options are a map keyed by the file type's PropertySpec ids.
using SaveOptions = QVariantMap;

class FileType {
public:
    virtual ~FileType() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QStringList extensions() const = 0; // lowercase, without dot; first is default
    virtual bool canLoad() const { return true; }
    virtual bool canSave() const { return true; }
    virtual bool supportsLayers() const { return false; }
    // Options shown in the Save Configuration dialog (empty = no dialog).
    virtual QList<PropertySpec> saveOptions() const { return {}; }
    bool hasSaveOptions() const { return !saveOptions().isEmpty(); }
    // Lossy formats: the save dialog previews the decoded result.
    virtual bool isLossy() const { return false; }

    // Throws FileError.
    virtual std::unique_ptr<Document> load(const QByteArray &data) const = 0;
    // `flat` is the flattened image (Format_ARGB32) for single-layer formats.
    virtual QByteArray save(const Document &doc, const QImage &flat, const SaveOptions &opts) const = 0;
};

namespace filetypes {

const std::vector<const FileType *> &all();
const FileType *byId(const QString &id);
const FileType *forPath(const QString &path);
QString openFilter();
QString saveFilter(const FileType *selectedFirst = nullptr);
const FileType *fromFilter(const QString &filter);

// Loads a document, detecting the type by content first, then extension.
std::unique_ptr<Document> load(const QString &path);
// Writes atomically: temp file in the same directory, fsync, rename.
void save(Document &doc, const QString &path, const FileType *type, const SaveOptions &opts);
void writeFileAtomically(const QString &path, const QByteArray &data);

} // namespace filetypes

// Helper for single-layer image formats.
std::unique_ptr<Document> documentFromImage(const QImage &img, double dpiX = 96, double dpiY = 96);

} // namespace pane
