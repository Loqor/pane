#pragma once

#include <QRectF>
#include <QSize>

namespace pane {

// Where to draw an image on a printed page (in device pixels): at its physical
// size (from the image DPI) when that fits on the page, otherwise scaled down to
// fit; centered either way.
inline QRectF printTargetRect(QSize imagePx, double imageDpiX, double imageDpiY, const QRectF &page, double deviceDpiX,
                              double deviceDpiY)
{
    QSizeF physical(imagePx.width() / std::max(1.0, imageDpiX) * deviceDpiX, imagePx.height() / std::max(1.0, imageDpiY) * deviceDpiY);
    if (physical.width() > page.width() || physical.height() > page.height()) physical.scale(page.size(), Qt::KeepAspectRatio);
    return QRectF(page.left() + (page.width() - physical.width()) / 2, page.top() + (page.height() - physical.height()) / 2,
                  physical.width(), physical.height());
}

} // namespace pane
