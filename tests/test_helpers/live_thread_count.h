// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// Live thread count for this process.
///
/// A test that spawns a background thread and does not join it before returning
/// leaks it; the loop later fires a callback on freed state and crashes a
/// *different* test. The cross-test isolation listener uses this to name the
/// leaking test, and individual tests use it to assert that a specific
/// operation is thread-neutral so a regression re-fires at the source rather
/// than as a nondeterministic crash somewhere downstream.
///
/// macOS has no /proc, so proc_pidinfo/PROC_PIDTASKINFO is the equivalent:
/// pti_threadnum is the live Mach thread count for the task
/// (prestonbrown/helixscreen#1146).
///
/// Returns -1 if the count could not be determined.

#include <cstdlib>
#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#else
#include <fstream>
#include <string>
#endif

namespace helix::test {

inline int live_thread_count() {
#if defined(__APPLE__)
    struct proc_taskinfo ti;
    const int rc = proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
    if (rc == static_cast<int>(sizeof(ti))) {
        return static_cast<int>(ti.pti_threadnum);
    }
    return -1;
#else
    std::ifstream st("/proc/self/status");
    std::string line;
    while (std::getline(st, line)) {
        if (line.rfind("Threads:", 0) == 0) {
            return std::atoi(line.c_str() + 8);
        }
    }
    return -1;
#endif
}

} // namespace helix::test
