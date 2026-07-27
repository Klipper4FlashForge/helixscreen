// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

namespace helix::ui {

class UpdateQueueTestAccess {
  public:
    static void drain(UpdateQueue& q) {
        q.process_pending();
    }

    /// Drop queued callbacks WITHOUT running them.
    ///
    /// The cross-test counterpart to drain(). Once a test has returned, its
    /// fixture is destroyed and anything it left queued closes over dead state,
    /// so *executing* those callbacks is precisely the use-after-free — draining
    /// is only safe while the owning objects are still alive (i.e. inside the
    /// fixture's own destructor body). Between tests, discard.
    static size_t discard_pending(UpdateQueue& q) {
        std::queue<TaggedCallback> dropped;
        {
            std::lock_guard<std::mutex> lock(q.mutex_);
            std::swap(dropped, q.pending_);
        }
        // Destroy the callbacks outside the lock. They are never invoked, so the
        // dead state they capture is only released, never dereferenced.
        return dropped.size();
    }

    /// Drain repeatedly until the queue is fully empty (handles nested queue_update calls)
    static void drain_all(UpdateQueue& q, int max_iterations = 10) {
        for (int i = 0; i < max_iterations; ++i) {
            {
                std::lock_guard<std::mutex> lock(q.mutex_);
                if (q.pending_.empty())
                    return;
            }
            q.process_pending();
        }
    }
};

} // namespace helix::ui

// Convenience alias for use with 'using namespace helix'
