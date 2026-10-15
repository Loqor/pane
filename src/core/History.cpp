#include "core/History.h"

#include "core/Document.h"

namespace pane {

History::History(Document &doc) : m_doc(doc) {}

void History::reset(std::unique_ptr<HistoryItem> base)
{
    m_items.clear();
    m_items.push_back(std::move(base));
    m_current = 0;
    m_clean = 0;
    emit changed();
}

void History::push(std::unique_ptr<HistoryItem> item)
{
    m_items.resize(size_t(m_current + 1)); // discard the redo branch
    if (m_clean > m_current) m_clean = -1;
    m_items.push_back(std::move(item));
    m_current = int(m_items.size()) - 1;
    emit changed();
}

void History::undo()
{
    if (beforeChange) beforeChange();
    if (!canUndo()) return;
    m_items[m_current]->undo(m_doc);
    --m_current;
    emit changed();
}

void History::redo()
{
    if (beforeChange) beforeChange();
    if (!canRedo()) return;
    ++m_current;
    m_items[m_current]->redo(m_doc);
    emit changed();
}

void History::jumpTo(int index)
{
    if (beforeChange) beforeChange();
    index = std::clamp(index, 0, count() - 1);
    while (m_current > index) {
        m_items[m_current]->undo(m_doc);
        --m_current;
    }
    while (m_current < index) {
        ++m_current;
        m_items[m_current]->redo(m_doc);
    }
    emit changed();
}

} // namespace pane
