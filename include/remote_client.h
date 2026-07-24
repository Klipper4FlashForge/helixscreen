// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
}
