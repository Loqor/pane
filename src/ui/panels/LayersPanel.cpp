#include "core/Commands.h"
#include "core/Glyphs.h"
#include "ui/panels/Panels.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace pane {

LayerList::LayerList(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, this, qOverload<>(&QWidget::update));
}

void LayerList::setWorkspace(Workspace *ws)
{
    m_ws = ws;
    refreshThumbnails();
}

QSize LayerList::sizeHint() const { return {200, std::max(1, m_ws ? m_ws->doc->layerCount() : 1) * RowHeight}; }

void LayerList::refreshThumbnails()
{
    m_thumbs.clear();
    if (m_ws)
        for (int i = 0; i < m_ws->doc->layerCount(); ++i)
            m_thumbs.push_back(Compositor::layerThumbnail(m_ws->doc->layer(i).surface, RowHeight - 8));
    setMinimumHeight(sizeHint().height());
    updateGeometry();
    update();
}

int LayerList::rowToLayer(int row) const { return m_ws ? m_ws->doc->layerCount() - 1 - row : -1; }
int LayerList::rowAt(int y) const { return y / RowHeight; }
QRect LayerList::checkRect(int row) const { return QRect(width() - 26, row * RowHeight + (RowHeight - 16) / 2, 16, 16); }

void LayerList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    p.fillRect(rect(), th.base);
    if (!m_ws) return;
    Document &doc = *m_ws->doc;
    QColor sel = th.accent;
    sel.setAlpha(th.dark ? 70 : 45);
    for (int row = 0; row < doc.layerCount(); ++row) {
        int li = rowToLayer(row);
        const Layer &l = doc.layer(li);
        QRect r(0, row * RowHeight, width(), RowHeight);
        if (li == doc.activeLayerIndex()) {
            p.setPen(Qt::NoPen);
            p.setBrush(sel);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        }
        // Thumbnail on a checkerboard.
        QImage thumb = m_thumbs.size() > size_t(li) ? m_thumbs[size_t(li)] : QImage();
        QRect tr(r.left() + 6, r.top() + 4, RowHeight - 8, RowHeight - 8);
        if (!thumb.isNull()) {
            QSize ts = thumb.size();
            tr = QRect(tr.left() + (tr.width() - ts.width()) / 2, tr.top() + (tr.height() - ts.height()) / 2, ts.width(), ts.height());
        }
        for (int y = 0; y < tr.height(); y += 4)
            for (int x = 0; x < tr.width(); x += 4)
                p.fillRect(QRect(tr.left() + x, tr.top() + y, 4, 4).intersected(tr), ((x + y) / 4) % 2 ? th.checkerB() : th.checkerA());
        if (!thumb.isNull()) p.drawImage(tr.topLeft(), thumb);
        p.setPen(th.surface2);
        p.setBrush(Qt::NoBrush);
        p.drawRect(tr.adjusted(0, 0, -1, -1));
        // Name.
        p.setPen(l.props.visible ? th.text : th.overlay1);
        QRect nr(tr.right() + 10, r.top(), width() - tr.right() - 44, RowHeight);
        p.drawText(nr, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(l.props.name, Qt::ElideRight, nr.width()));
        // Visibility checkbox.
        QRect cr = checkRect(row);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(l.props.visible ? th.accent : th.overlay1, 1.5));
        p.setBrush(l.props.visible ? th.accent : Qt::transparent);
        p.drawRoundedRect(QRectF(cr).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (l.props.visible) {
            p.setPen(QPen(th.dark ? th.crust : QColor(Qt::white), 2));
            p.drawPolyline(QPolygonF{QPointF(cr.left() + 3.5, cr.center().y() + 0.5), QPointF(cr.left() + 6.5, cr.bottom() - 3.5),
                                     QPointF(cr.right() - 3, cr.top() + 4)});
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    if (m_dragFrom >= 0 && m_dragTo >= 0 && m_dragTo != m_dragFrom) {
        int y = m_dragTo > m_dragFrom ? (m_dragTo + 1) * RowHeight : m_dragTo * RowHeight;
        p.setPen(QPen(th.accent, 2));
        p.drawLine(4, y, width() - 4, y);
    }
}

void LayerList::mousePressEvent(QMouseEvent *e)
{
    if (!m_ws || e->button() != Qt::LeftButton) return;
    int row = rowAt(int(e->position().y()));
    int li = rowToLayer(row);
    if (li < 0 || li >= m_ws->doc->layerCount()) return;
    emit beforeChange();
    Document &doc = *m_ws->doc;
    if (checkRect(row).adjusted(-4, -4, 4, 4).contains(e->position().toPoint())) {
        LayerProperties before = doc.layer(li).props, after = before;
        after.visible = !before.visible;
        commands::commitLayerProperties(doc, li, before, after,
                                        after.visible ? QStringLiteral("Show Layer") : QStringLiteral("Hide Layer"));
        return;
    }
    doc.setActiveLayerIndex(li);
    m_dragFrom = row;
    m_dragTo = -1;
    m_pressPos = e->position().toPoint();
}

void LayerList::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragFrom < 0 || !(e->buttons() & Qt::LeftButton)) return;
    if ((e->position().toPoint() - m_pressPos).manhattanLength() < 6 && m_dragTo < 0) return;
    m_dragTo = std::clamp(rowAt(int(e->position().y())), 0, m_ws->doc->layerCount() - 1);
    update();
}

void LayerList::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragFrom >= 0 && m_dragTo >= 0 && m_dragTo != m_dragFrom && m_ws)
        commands::moveLayerTo(*m_ws->doc, rowToLayer(m_dragFrom), rowToLayer(m_dragTo));
    m_dragFrom = m_dragTo = -1;
    update();
}

void LayerList::mouseDoubleClickEvent(QMouseEvent *e)
{
    int row = rowAt(int(e->position().y()));
    if (!m_ws || rowToLayer(row) < 0 || checkRect(row).contains(e->position().toPoint())) return;
    emit propertiesRequested();
}

LayersPanel::LayersPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_list = new LayerList(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_list);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->addWidget(m_scroll, 1);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("PanelFooter"));
    auto *h = new QHBoxLayout(footer);
    h->setContentsMargins(2, 2, 2, 0);
    h->setSpacing(1);
    struct B { const QString &glyph; const char *action; const char *tip; };
    const B buttons[] = {
        {glyphs::LayerAdd, "add", "Add New Layer (Ctrl+Shift+N)"},
        {glyphs::Delete, "delete", "Delete Layer (Ctrl+Shift+Delete)"},
        {glyphs::Duplicate, "duplicate", "Duplicate Layer (Ctrl+Shift+D)"},
        {glyphs::Merge, "mergeDown", "Merge Layer Down (Ctrl+M)"},
        {glyphs::ArrowUp, "moveUp", "Move Layer Up"},
        {glyphs::ArrowDown, "moveDown", "Move Layer Down"},
        {glyphs::Properties, "properties", "Layer Properties (F4)"},
    };
    for (const B &b : buttons) {
        auto *tb = new QToolButton(footer);
        tb->setText(b.glyph);
        tb->setFont(ThemeEngine::instance().glyphFont(16));
        tb->setToolTip(QString::fromUtf8(b.tip));
        tb->setAutoRaise(true);
        QString action = QString::fromLatin1(b.action);
        connect(tb, &QToolButton::clicked, this, [this, action] { emit actionRequested(action); });
        h->addWidget(tb);
    }
    h->addStretch();
    v->addWidget(footer);

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(250);
    connect(&m_thumbTimer, &QTimer::timeout, m_list, &LayerList::refreshThumbnails);
    connect(m_list, &LayerList::propertiesRequested, this, [this] { emit actionRequested(QStringLiteral("properties")); });
}

void LayersPanel::setWorkspace(Workspace *ws)
{
    for (auto &c : m_conns) disconnect(c);
    m_conns.clear();
    m_ws = ws;
    m_list->setWorkspace(ws);
    if (!ws) return;
    Document *d = ws->doc.get();
    m_conns << connect(d, &Document::layersChanged, m_list, &LayerList::refreshThumbnails);
    m_conns << connect(d, &Document::activeLayerChanged, m_list, qOverload<>(&QWidget::update));
    m_conns << connect(d, &Document::pixelsChanged, this, [this] {
        if (!m_thumbTimer.isActive()) m_thumbTimer.start();
    });
}

} // namespace pane
