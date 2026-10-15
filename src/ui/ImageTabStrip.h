// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QImage>
#include <QWidget>

namespace pane {

// paint.net's image list: thumbnails in the title/menu row.
class ImageTabStrip : public QWidget {
    Q_OBJECT
public:
    struct Tab {
        QString title;
        QImage thumbnail;
        bool dirty = false;
    };
    explicit ImageTabStrip(QWidget *parent = nullptr);

    void setTabs(const std::vector<Tab> &tabs, int current);
    void updateTab(int index, const Tab &tab);
    QSize sizeHint() const override { return {200, 40}; }
    QSize minimumSizeHint() const override { return {40, 40}; }

signals:
    void currentChanged(int index);
    void closeRequested(int index);
    void moved(int from, int to);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    static constexpr int TabWidth = 56;
    QRect tabRect(int i) const;
    QRect closeRect(int i) const;
    int tabAt(QPoint p) const;
    std::vector<Tab> m_tabs;
    int m_current = -1, m_hover = -1;
    int m_dragFrom = -1, m_dragTo = -1;
    QPoint m_press;
    int m_scroll = 0;
};

} // namespace pane
