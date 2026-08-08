// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "job_queue_state.h"

#include <utility>
#include <vector>

/// Reaches JobQueueState's private cached-job list so widget tests can seed
/// queued jobs directly. JobQueueState only ever populates cached_jobs_ /
/// is_loaded_ from a real Moonraker response (on_queue_fetched(), private),
/// so there is no public path to a loaded-with-jobs state without a live
/// (or mocked end-to-end) connection.
class JobQueueStateTestAccess {
  public:
    static void set_jobs(JobQueueState& state, std::vector<JobQueueEntry> jobs) {
        state.cached_jobs_ = std::move(jobs);
        state.is_loaded_ = true;
    }
};
