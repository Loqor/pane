// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QByteArray>
#include <QString>
#include <map>
#include <vector>

namespace pane::zip {

struct Entry {
    QString name;
    QByteArray data;
    bool compress = true; // false = stored (required for OpenRaster's "mimetype")
};

// Minimal ZIP (PKZIP 2.0) writer/reader: stored + deflate entries, no zip64.
QByteArray write(const std::vector<Entry> &entries);
// Throws std::runtime_error on malformed archives.
std::map<QString, QByteArray> read(const QByteArray &archive);

} // namespace pane::zip
