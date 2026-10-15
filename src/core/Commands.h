#pragma once

#include "core/Document.h"
#include "core/Resample.h"

// Document operations that record history. Each either fully applies and pushes
// one history item, or throws and leaves the document unchanged.
namespace pane::commands {

void addNewLayer(Document &doc);
void deleteLayer(Document &doc);
void duplicateLayer(Document &doc);
void mergeLayerDown(Document &doc);
void moveLayerUp(Document &doc);
void moveLayerDown(Document &doc);
// Records a property change that the caller already previewed live.
void commitLayerProperties(Document &doc, int index, const LayerProperties &before, const LayerProperties &after,
                           const QString &name = QStringLiteral("Layer Properties"));
void moveLayerTo(Document &doc, int from, int to);
void flatten(Document &doc);
void addLayerFromImage(Document &doc, const QImage &img, const QString &name, const QString &historyName,
                       QPoint at = {});

void resizeImage(Document &doc, QSize size, Resampling mode);
// anchor: 0..8, row-major over a 3x3 grid (0 = top-left, 4 = center).
void canvasSize(Document &doc, QSize size, int anchor, Pixel backgroundFill);
void flipImage(Document &doc, Qt::Orientation o);
void rotateImage(Document &doc, int degreesClockwise); // 90, 180, 270
void flipLayer(Document &doc, Qt::Orientation o);
void rotateLayer180(Document &doc);
void cropToSelection(Document &doc);

void selectAll(Document &doc);
void deselect(Document &doc);
void invertSelection(Document &doc);
void setSelection(Document &doc, const Selection &sel, const QString &name, const QString &glyph);
void eraseSelection(Document &doc);
void fillSelection(Document &doc, Pixel color);

} // namespace pane::commands
