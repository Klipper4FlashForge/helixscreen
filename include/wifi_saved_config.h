// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

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

/**
 * @brief HelixScreen-owned credential store, independent of the vendor config.
 *
 * mirror_to_persistent() above only helps when we can identify a durable
 * target to copy onto — that requires the volatile path to have started life
 * as a symlink. A real Adventurer 5M report showed a SAVE_CONFIG that replied
 * "OK" and still never reached wpa_supplicant's own config file, with no
 * symlink in sight to mirror from. Whatever the cause, this store makes
 * persistence work without needing to know it: every successful connect is
 * recorded here too, and reconcile_saved_networks() (in the wpa_supplicant
 * backend) re-adds anything missing from wpa_supplicant's LIST_NETWORKS at
 * the next startup.
 *
 * The file lives at store_path(), mode 0600 — it holds every PSK the user has
 * ever entered, so it must never be group- or world-readable, and it must
 * never be swept into a debug bundle (see debug_bundle_collector.cpp's
 * allowlist, which does not name it).
 */
namespace store {

/// A network HelixScreen remembers on its own, independent of whatever the
/// vendor's wpa_supplicant config does with it.
struct SavedNetwork {
    std::string ssid;
    std::string psk; ///< NEVER logged, at any level, in any form.
};

/// Path to the store: <user config dir>/wifi_networks.json.
std::string store_path();

/// Persist @p net, replacing any existing entry for the same SSID rather than
/// duplicating it. Written atomically (temp file + rename, via the same
/// helper mirror_to_persistent() uses) at mode 0600 — forced, not inherited
/// from the parent directory, because unlike the vendor config this file is
/// ours and holds every saved PSK on the device.
///
/// @return false on I/O failure (nothing was written); the previous store
///         contents, if any, are left untouched.
bool save(const SavedNetwork& net);

/// Remove the entry for @p ssid, if present.
///
/// @return false only on I/O failure. Removing an SSID that was never stored
///         still returns true — the store already reflects the desired state.
bool remove(const std::string& ssid);

/// All networks currently in the store, in no particular order.
///
/// @return An empty vector when the file is missing, empty, or fails to
///         parse as the expected JSON array — corruption here must never
///         propagate as an exception into a caller that did not expect one.
std::vector<SavedNetwork> load();

} // namespace store

} // namespace helix::wifi
