// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "effects/EffectSession.h"

#include <QtConcurrent/QtConcurrentMap>
#include <cstring>

namespace pane {

EffectSession::EffectSession(Document &doc, const Effect &effect, QObject *parent)
    : QObject(parent), m_doc(doc), m_effect(effect), m_edit(doc, doc.activeLayerIndex()),
      m_src(doc.activeLayer().surface), m_sel(doc.selection())
{
    m_region = m_sel.boundsOrImage();
    for (int idx : m_src.tilesIn(m_region))
        if (m_sel.coverage(idx).kind != TileCoverage::None) m_tiles.push_back(idx); // also warms the mask cache
    m_drain.setInterval(40);
    connect(&m_drain, &QTimer::timeout, this, &EffectSession::drain);
}

EffectSession::~EffectSession()
{
    if (!m_closed) cancel();
}

void EffectSession::update(const QVariantMap &userParams)
{
    QVariantMap params = userParams;
    for (auto it = m_context.begin(); it != m_context.end(); ++it) params.insert(it.key(), it.value());
    if (m_closed) return;
    if (m_job) m_job->cancelled = true;
    auto job = std::make_shared<Job>();
    job->total = int(m_tiles.size());
    job->tiles = m_tiles;
    m_job = job;

    std::shared_ptr<EffectInstance> inst = m_effect.instance(params, m_src, m_sel);
    Surface src = m_src;       // shared tiles; captured by value so tasks own what they read
    Selection sel = m_sel;
    QRect region = m_region;
    auto work = [job, inst, src, sel, region](int idx) {
        if (job->cancelled) return;
        QRect tr = src.tileRect(idx);
        QRect r = tr.intersected(region);
        auto out = std::make_shared<Tile>();
        const Tile *before = src.tile(idx);
        if (before) std::memcpy(out->px, before->px, sizeof(Tile::px));
        else std::memset(out->px, 0, sizeof(Tile::px));
        inst->render(src, r, &out->px[(r.top() - tr.top()) * TileSize + (r.left() - tr.left())], TileSize);
        TileCoverage cov = sel.coverage(idx);
        if (cov.kind == TileCoverage::Partial) {
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x) {
                    int i = (y - tr.top()) * TileSize + (x - tr.left());
                    uint32_t c = cov.mask[i];
                    if (c == 255) continue;
                    out->px[i] = lerpPixel(before ? before->px[i] : 0, out->px[i], c);
                }
        }
        if (job->cancelled) return;
        {
            std::lock_guard lock(job->mutex);
            job->ready.emplace_back(idx, std::move(out));
        }
        job->done++;
    };
    job->future = QtConcurrent::map(QThreadPool::globalInstance(), job->tiles, work);
    m_drain.start();
}

void EffectSession::drain()
{
    if (!m_job) return;
    std::vector<std::pair<int, ConstTilePtr>> ready;
    {
        std::lock_guard lock(m_job->mutex);
        ready.swap(m_job->ready);
    }
    QRect dirty;
    for (auto &[idx, tile] : ready) {
        m_edit.setTile(idx, tile);
        dirty |= m_src.tileRect(idx);
    }
    if (!dirty.isEmpty()) m_doc.notifyPixelsChanged(dirty);
    emit progress(m_job->done, m_job->total);
    if (m_job->future.isFinished() && ready.empty()) m_drain.stop();
}

bool EffectSession::isRendering() const { return m_job && !m_job->future.isFinished(); }

void EffectSession::waitForRender()
{
    if (!m_job) return;
    m_job->future.waitForFinished();
    drain();
}

void EffectSession::finish()
{
    if (m_closed) return;
    waitForRender();
    m_drain.stop();
    m_closed = true;
    if (auto item = m_edit.finish(m_effect.name(), m_effect.glyph())) m_doc.history().push(std::move(item));
}

void EffectSession::cancel()
{
    if (m_closed) return;
    if (m_job) m_job->cancelled = true;
    m_drain.stop();
    m_closed = true;
    m_edit.cancel();
}

} // namespace pane
