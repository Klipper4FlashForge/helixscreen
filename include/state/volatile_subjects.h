// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file volatile_subjects.h
 * @brief Registry of subjects invalidated by a Klipper restart.
 *
 * Moonraker sends DELTA status updates — changed fields only. A subject fed by a
 * delta-only field keeps its last value indefinitely when Klipper restarts, because
 * the field is simply absent from subsequent payloads until it next changes. A stale
 * value that gates behaviour is a live bug: a cached idle_timeout.state == "Printing"
 * captured mid-G28 made the app treat a freshly-restarted, idle printer as busy and
 * queue LED/fan/temp commands fire-and-forget (#1129).
 *
 * Register such a subject here with the SAME default it was initialized to, and a
 * Klippy state transition restores it. Prefer INIT_SUBJECT_INT_VOLATILE in
 * state/subject_macros.h, which writes that default exactly once.
 *
 * NOT for continuously-streamed state (temperatures, positions): those self-heal
 * within a tick, and resetting them only produces visible flicker.
 */

#pragma once

#include <cstddef>
#include <lvgl.h>
#include <vector>

namespace helix::state {

class VolatileSubjects {
  public:
    /// @param subject       Subject to restore. Must outlive this registry.
    /// @param default_value The value @p subject was initialized to.
    void register_subject(lv_subject_t* subject, int default_value) {
        if (subject == nullptr) {
            return;
        }
        entries_.push_back({subject, default_value});
    }

    /// Restore every registered subject to its recorded default. Idempotent.
    void reset_all() {
        for (const auto& entry : entries_) {
            lv_subject_set_int(entry.subject, entry.default_value);
        }
    }

    /// Drop all registrations. MUST be called before re-running an owner's
    /// init_subjects(), which tests do repeatedly — otherwise entries accumulate.
    void clear() {
        entries_.clear();
    }

    [[nodiscard]] size_t size() const {
        return entries_.size();
    }

  private:
    struct Entry {
        lv_subject_t* subject;
        int default_value;
    };

    std::vector<Entry> entries_;
};

} // namespace helix::state
