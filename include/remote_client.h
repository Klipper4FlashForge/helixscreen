// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * @file remote_client.h
 * @brief Entry point for the folded remote-control client.
 *
 * The helixctl client is compiled into helix-screen and reached via the `ctl`
 * and `repl` subcommands (main.cpp dispatches before the app initializes).
 * There is no separate helixctl binary. Dev/test-only — compiled out of release
 * builds (see HELIX_ENABLE_REMOTE_CONTROL).
 *
 * @param argc/argv Forwarded from main with the subcommand as argv[0]
 *                  (e.g. {"ctl", "navigate", "controls"} or {"repl"}).
 * @return process exit code
 */

namespace helix {
int remote_client_main(int argc, char** argv);

/**
 * @brief True for a describe_screen locator written without the '@' prefix.
 *
 * An "s" (active screen) or "t" (top layer) root followed by one or more
 * "/<index>" segments — the exact shape `ls` prints. Widget names never contain
 * '/', so accepting this form lets a path pasted out of a listing be used
 * directly as a click/set_value/scroll target.
 *
 * Inline because the test binary does not link remote_client.o (the ctl client
 * is app-only); keeping it header-side lets the tests exercise it directly.
 */
inline bool is_bare_path(const std::string& target) {
    if (target.size() < 3 || (target[0] != 's' && target[0] != 't') || target[1] != '/') {
        return false;
    }
    bool digit_in_segment = false;
    for (size_t i = 2; i < target.size(); ++i) {
        if (target[i] == '/') {
            if (!digit_in_segment) {
                return false; // empty segment: "s//1" or a trailing slash
            }
            digit_in_segment = false;
        } else if (target[i] >= '0' && target[i] <= '9') {
            digit_in_segment = true;
        } else {
            return false;
        }
    }
    return digit_in_segment;
}
} // namespace helix
