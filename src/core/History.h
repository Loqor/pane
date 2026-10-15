// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

namespace pane {

class Document;

class HistoryItem {
public:
    HistoryItem(QString name, QString glyph) : m_name(std::move(name)), m_glyph(std::move(glyph)) {}
    virtual ~HistoryItem() = default;

    const QString &name() const { return m_name; }
    const QString &glyph() const { return m_glyph; }

    virtual void undo(Document &doc) = 0;
    virtual void redo(Document &doc) = 0;
    virtual size_t memoryBytes() const { return 0; }

private:
    QString m_name, m_glyph;
};

// The first entry ("Open Image" / "New Image"); it can never be undone.
class BaseHistoryItem final : public HistoryItem {
public:
    using HistoryItem::HistoryItem;
    void undo(Document &) override {}
    void redo(Document &) override {}
};

// Linear undo history, as in paint.net. items()[0] is always the base item.
// currentIndex() is the index of the most recently applied item.
class History : public QObject {
    Q_OBJECT
public:
    explicit History(Document &doc);

    void reset(std::unique_ptr<HistoryItem> base);
    // The item's change has already been applied to the document.
    void push(std::unique_ptr<HistoryItem> item);

    bool canUndo() const { return m_current > 0; }
    bool canRedo() const { return m_current + 1 < int(m_items.size()); }
    void undo();
    void redo();
    void jumpTo(int index);

    int currentIndex() const { return m_current; }
    int count() const { return int(m_items.size()); }
    const HistoryItem &item(int i) const { return *m_items[i]; }

    void markClean() { m_clean = m_current; emit changed(); }
    // No history state counts as saved (e.g. a recovered document).
    void markDirty() { m_clean = -1; emit changed(); }
    bool isClean() const { return m_clean == m_current; }

    // Called before undo/redo/push so in-progress tool sessions can commit first.
    std::function<void()> beforeChange;

signals:
    void changed();

private:
    Document &m_doc;
    std::vector<std::unique_ptr<HistoryItem>> m_items;
    int m_current = -1;
    int m_clean = 0;
};

} // namespace pane
