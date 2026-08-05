// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

#include <vector>

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
    /// Returns the tag of each dropped callback, in queue order. Naming the
    /// producer is what makes a leak actionable: a tag pointing at a process
    /// singleton (AmsState::…) is benign unflushed work, while one closing over
    /// a per-test object is a real use-after-free waiting for the next drain.
    /// Untagged callbacks report as "<untagged>".
    static std::vector<const char*> discard_pending(UpdateQueue& q) {
        std::queue<TaggedCallback> dropped;
        {
            std::lock_guard<std::mutex> lock(q.mutex_);
            std::swap(dropped, q.pending_);
        }
        // Collect tags and destroy the callbacks outside the lock. They are never
        // invoked, so the state they capture is only released, never dereferenced.
        std::vector<const char*> tags;
        tags.reserve(dropped.size());
        while (!dropped.empty()) {
            const char* t = dropped.front().tag;
            tags.push_back(t != nullptr ? t : "<untagged>");
            dropped.pop();
        }
        return tags;
    }

    /// Number of queued callbacks that have thrown since process start.
    ///
    /// process_pending() swallows callback exceptions on purpose, so a test that
    /// drains the queue sees success whether the callback ran or blew up. Snapshot
    /// this before draining and compare after to assert the callback ran clean.
    static uint32_t callback_exception_count() {
        return UpdateQueue::callback_exception_count_.load(std::memory_order_relaxed);
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
