// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix::test {

/// Absolute path of this process's private config sandbox.
///
/// Created by a static initializer before main() and torn down at exit. The
/// same initializer points HELIX_CONFIG_DIR (so helix::writable_path() and
/// everything built on it lands here) and the rolling-backup tiers
/// (AppConstants::Update::state_dir() / backup_fallback_dir()) into it, so a
/// test binary structurally cannot read or write the developer's real
/// settings.json, $HOME/.helixscreen/, or /var/lib/helixscreen/.
const std::string& config_sandbox_dir();

/// Return the Config singleton to a clean, sandboxed state: path inside the
/// sandbox, empty data, no active printer, not read-only, and no leftover
/// settings file on disk. Called before every TEST_CASE by the isolation
/// listener and by every HelixTestFixture ctor/dtor (which also covers each
/// SECTION leaf).
void reset_config_singleton();

} // namespace helix::test

// Base fixture for every HelixScreen test. Deterministically resets
// process-wide singletons tests are known to mutate so ordering cannot
// mask bugs. Expand reset_all() reactively as flakiness surfaces.
//
// Derive test-specific fixtures from this (LVGLTestFixture does). Plain
// non-LVGL unit tests can use it directly via TEST_CASE_METHOD.
//
// Note: the first reset_all() call initializes SystemSettingsManager's subjects,
// which self-register with StaticSubjectRegistry for process-lifetime. Once
// initialized, they stay initialized — that's intentional and harmless, but
// worth knowing for future derived fixtures.
class HelixTestFixture {
  public:
    HelixTestFixture();          // calls reset_all() on entry — idempotent
    virtual ~HelixTestFixture(); // calls reset_all() on exit — leaves clean slate

    HelixTestFixture(const HelixTestFixture&) = delete;
    HelixTestFixture& operator=(const HelixTestFixture&) = delete;
    HelixTestFixture(HelixTestFixture&&) = delete;
    HelixTestFixture& operator=(HelixTestFixture&&) = delete;

  protected:
    // List expands reactively. Keep small; don't over-reset.
    static void reset_all();
};
