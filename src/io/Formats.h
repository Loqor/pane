// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include "io/FileType.h"

namespace pane {

const FileType *pngFileType();
const FileType *jpegFileType();
const FileType *bmpFileType();
const FileType *pdnFileType();
const FileType *webpFileType();
const FileType *avifFileType();
const FileType *heicFileType();
const FileType *tiffFileType();
const FileType *tgaFileType();
const FileType *ddsFileType();
const FileType *gifFileType();
const FileType *icoFileType();
const FileType *oraFileType();

} // namespace pane
