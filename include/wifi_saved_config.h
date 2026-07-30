// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix::wifi {

/**
 * @file wifi_saved_config.h
 * @brief Keep wpa_supplicant's saved config from landing on volatile storage.
 *
 * Some firmwares point wpa_supplicant's `-c` at a path under /var/run or /tmp
 * and symlink it to persistent storage, so a SAVE_CONFIG follows the link and
 * survives a reboot. wpa_supplicant writes its config ATOMICALLY — temp file
 * plus rename() — and the rename replaces the symlink itself with a regular
 * file. The first save therefore destroys the persistence link, and every
 * credential after it is written to RAM and lost at power-off.
 *
 * Device-verified on a Snapmaker U1 (2026-07-29):
 *   - `-c /var/run/wpa_supplicant/wpa_supplicant.conf` -> /etc/wpa_supplicant.conf
 *   - after one SAVE_CONFIG the symlink is gone, replaced by a 166-byte regular
 *     file on tmpfs; the persistent file still has zero network blocks
 *   - seeding the persistent file by hand and rebooting brings WiFi up with the
 *     vendor's own restore path disabled, proving boot reads that file
 *
 * That is the whole bug behind "WiFi disconnects every time I turn off the
 * printer". The remedy is to copy what wpa_supplicant just wrote back to the
 * location it will read at boot.
 *
 * Nothing here is platform-specific: the volatile path comes from the running
 * daemon's own command line, and the persistent target is whatever that path
 * was a symlink to when we started — captured before the first save can
 * destroy it. On a platform whose config is already persistent, every function
 * here is a no-op.
 */

namespace detail {

/// True if @p path lives on a filesystem whose contents do not survive a
/// reboot (tmpfs, ramfs). Pure apart from the statfs; unit-tested against real
/// paths.
bool is_volatile_path(const std::string& path);

} // namespace detail

/// Remember where @p conf_path currently points, before any SAVE_CONFIG can
/// replace the symlink with a regular file. Call once at backend startup.
///
/// A no-op when @p conf_path is not a symlink, or already resolves onto
/// persistent storage — the platform is fine and nothing needs mirroring.
void remember_persistent_target(const std::string& conf_path);

/// The persistent target captured by remember_persistent_target(), or "" when
/// there is nothing to mirror on this platform.
std::string persistent_target();

/// Copy @p conf_path onto the remembered persistent target so the next boot
/// loads it.
///
/// Call after SAVE_CONFIG when the config landed on volatile storage. The copy
/// is written through a temp file and renamed, mirroring the target's existing
/// permissions and ownership — we are writing the platform's file, and
/// tightening it could lock a vendor UI out of its own config.
///
/// @return false when there is no remembered target, or the copy failed.
bool mirror_to_persistent(const std::string& conf_path);

} // namespace helix::wifi
