#pragma once

#include "core/LayerEdit.h"
#include "effects/Effect.h"

#include <QFuture>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <mutex>

namespace pane {

// Applies an effect to the active layer with live, progressive, cancellable
// preview. The layer is snapshotted when the session starts; every update()
// re-renders from that snapshot on the thread pool.
class EffectSession : public QObject {
    Q_OBJECT
public:
    EffectSession(Document &doc, const Effect &effect, QObject *parent = nullptr);
    ~EffectSession() override;

    const Effect &effect() const { return m_effect; }
    const Surface &source() const { return m_src; }
    const Selection &selection() const { return m_sel; }
    // Extra values merged into every update (e.g. _primary / _secondary colors).
    void setContext(const QVariantMap &ctx) { m_context = ctx; }
    void update(const QVariantMap &params);
    // Waits for rendering, applies everything and pushes one history item.
    void finish();
    // Restores the layer exactly.
    void cancel();
    bool isRendering() const;
    // Blocks until the current render is complete (used by tests and finish()).
    void waitForRender();

signals:
    void progress(int done, int total);

private:
    struct Job {
        std::atomic<bool> cancelled{false};
        std::atomic<int> done{0};
        int total = 0;
        std::mutex mutex;
        std::vector<std::pair<int, ConstTilePtr>> ready;
        std::vector<int> tiles; // owned here: QtConcurrent::map works on the container in place
        QFuture<void> future;
    };
    void drain();

    Document &m_doc;
    const Effect &m_effect;
    LayerEdit m_edit;
    Surface m_src;
    Selection m_sel;
    QRect m_region;
    std::vector<int> m_tiles;
    std::shared_ptr<Job> m_job;
    QTimer m_drain;
    bool m_closed = false;
    QVariantMap m_context;
};

} // namespace pane
