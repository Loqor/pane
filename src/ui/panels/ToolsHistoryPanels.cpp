#include "core/Glyphs.h"
#include "ui/panels/Panels.h"
#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace pane {

// ------------------------------------------------------------ Tools

ToolsPanel::ToolsPanel(QWidget *parent) : QWidget(parent), m_group(new QButtonGroup(this))
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(2, 2, 2, 2);
    grid->setSpacing(1);
    m_group->setExclusive(true);
    int i = 0;
    for (Tool *t : ToolRegistry::instance().tools()) {
        auto *b = new QToolButton(this);
        b->setText(t->glyph());
        b->setFont(ThemeEngine::instance().glyphFont(18));
        b->setFixedSize(30, 30);
        b->setCheckable(true);
        b->setEnabled(t->enabled());
        b->setAutoRaise(true);
        b->setToolTip(QStringLiteral("<b>%1</b> (%2)<br>%3").arg(t->name(), QString(t->shortcut()), t->hint()));
        m_group->addButton(b);
        m_buttons[t->id()] = b;
        QString id = t->id();
        connect(b, &QToolButton::clicked, this, [this, id] { emit toolSelected(id); });
        grid->addWidget(b, i / 2, i % 2);
        ++i;
    }
}

void ToolsPanel::setActiveTool(const QString &id)
{
    auto it = m_buttons.find(id);
    if (it != m_buttons.end()) it->second->setChecked(true);
}

// ------------------------------------------------------------ History

namespace {

// Draws the history glyph (Nerd Font) to the left of the item text.
class GlyphItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, idx);
        QString glyph = idx.data(Qt::UserRole).toString();
        o.rect.adjust(22, 0, 0, 0);
        QStyledItemDelegate::paint(p, o, idx);
        if (glyph.isEmpty()) return;
        p->save();
        p->setFont(ThemeEngine::instance().glyphFont(15));
        QVariant fg = idx.data(Qt::ForegroundRole);
        p->setPen(fg.isValid() ? fg.value<QBrush>().color() : ThemeEngine::instance().current().accent);
        p->drawText(QRect(opt.rect.left() + 2, opt.rect.top(), 20, opt.rect.height()), Qt::AlignCenter, glyph);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const override
    {
        return QStyledItemDelegate::sizeHint(o, i).expandedTo(QSize(0, 22));
    }
};

} // namespace

HistoryPanel::HistoryPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setItemDelegate(new GlyphItemDelegate(m_list));
    v->addWidget(m_list, 1);
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("PanelFooter"));
    auto *h = new QHBoxLayout(footer);
    h->setContentsMargins(2, 2, 2, 0);
    h->setSpacing(1);
    auto mk = [&](const QString &glyph, const QString &tip) {
        auto *b = new QToolButton(footer);
        b->setText(glyph);
        b->setFont(ThemeEngine::instance().glyphFont(16));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        h->addWidget(b);
        return b;
    };
    auto *first = mk(glyphs::First, QStringLiteral("Rewind to the start"));
    m_undo = mk(glyphs::Undo, QStringLiteral("Undo (Ctrl+Z)"));
    m_redo = mk(glyphs::Redo, QStringLiteral("Redo (Ctrl+Y)"));
    auto *last = mk(glyphs::Last, QStringLiteral("Fast-forward to the end"));
    h->addStretch();
    v->addWidget(footer);

    auto jump = [this](int idx) {
        if (!m_ws) return;
        emit beforeHistoryJump();
        m_ws->doc->history().jumpTo(idx);
    };
    connect(first, &QToolButton::clicked, this, [jump] { jump(0); });
    connect(last, &QToolButton::clicked, this, [this, jump] { if (m_ws) jump(m_ws->doc->history().count() - 1); });
    connect(m_undo, &QToolButton::clicked, this, [this, jump] { if (m_ws) jump(m_ws->doc->history().currentIndex() - 1); });
    connect(m_redo, &QToolButton::clicked, this, [this, jump] { if (m_ws) jump(m_ws->doc->history().currentIndex() + 1); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this, jump](int row) {
        if (m_updating || row < 0 || !m_ws) return;
        if (row != m_ws->doc->history().currentIndex()) jump(row);
    });
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, this, &HistoryPanel::rebuild);
}

void HistoryPanel::setWorkspace(Workspace *ws)
{
    disconnect(m_conn);
    m_ws = ws;
    if (ws) m_conn = connect(&ws->doc->history(), &History::changed, this, &HistoryPanel::rebuild);
    rebuild();
}

void HistoryPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    if (m_ws) {
        History &h = m_ws->doc->history();
        const Theme &th = ThemeEngine::instance().current();
        for (int i = 0; i < h.count(); ++i) {
            auto *item = new QListWidgetItem(h.item(i).name());
            item->setData(Qt::UserRole, h.item(i).glyph());
            if (i > h.currentIndex()) item->setForeground(th.overlay0);
            m_list->addItem(item);
        }
        m_list->setCurrentRow(h.currentIndex());
        m_list->scrollToItem(m_list->currentItem());
        m_undo->setEnabled(h.canUndo());
        m_redo->setEnabled(h.canRedo());
    } else {
        m_undo->setEnabled(false);
        m_redo->setEnabled(false);
    }
    m_updating = false;
}

} // namespace pane
