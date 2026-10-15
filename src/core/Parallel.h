// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QSemaphore>
#include <QThreadPool>
#include <atomic>

namespace pane {

// Runs f(i) for i in [0, n) on the global thread pool plus the calling thread.
// Uses only threads that are free right now (tryStart), so nested calls from
// pool threads can never deadlock: in the worst case the caller does all the work.
template <typename F> void parallelFor(int n, F &&f)
{
    if (n <= 0) return;
    if (n == 1) {
        f(0);
        return;
    }
    std::atomic<int> next{0};
    auto drain = [&] {
        for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) f(i);
    };
    QSemaphore done;
    int helpers = 0;
    QThreadPool *pool = QThreadPool::globalInstance();
    const int want = std::min(n, pool->maxThreadCount()) - 1;
    for (int k = 0; k < want; ++k) {
        if (!pool->tryStart([&] {
                drain();
                done.release();
            }))
            break;
        ++helpers;
    }
    drain();
    done.acquire(helpers);
}

} // namespace pane
