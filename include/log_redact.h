// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file log_redact.h
 * @brief Privacy-preserving identifiers for network names in log output.
 *
 * The in-memory log ring (see logging_init.cpp) is captured at debug level
 * regardless of the user's configured verbosity, and is uploaded verbatim by
 * the debug bundle, the crash reporter, and the `ctl log` RPC. Anything logged
 * at debug or above leaves the machine.
 *
 * A WiFi SSID is not just a string: a set of nearby SSIDs with signal strengths
 * resolves to a street address through public WiFi-positioning databases, and
 * the neighbouring networks in a scan belong to people who never consented to
 * being in anyone's bug report. A BSSID geolocates even more directly.
 *
 * Downstream scrubbing cannot fix this — an SSID is an arbitrary user-chosen
 * string with no pattern for a regex to match. So the redaction happens here,
 * at the call site, before the text ever reaches a sink.
 *
 * These functions return a short, stable, per-boot token: enough to correlate
 * "the same network" across lines within one session, which is all diagnostics
 * actually need, and useless for identifying the network or its location. The
 * salt is regenerated every boot, so tokens cannot be correlated across
 * sessions or compared against a precomputed table of common SSIDs.
 *
 * Use the raw value only at trace level, which the ring never captures.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace helix::redact {

/**
 * @brief Reduce an SSID to a non-identifying, session-stable token.
 *
 * @param ssid Network name (may be empty).
 * @return Token of the form `net#a3f1`, or `net#<none>` when @p ssid is empty.
 *
 * The same SSID yields the same token for the lifetime of the process and a
 * different one after a restart. Safe to log at any level.
 */
std::string ssid(std::string_view ssid);

/**
 * @brief Reduce a MAC or BSSID to a non-identifying, session-stable token.
 *
 * @param mac Hardware address in any format (may be empty).
 * @return Token of the form `mac#7b2c`, or `mac#<none>` when @p mac is empty.
 *
 * Applies to the adapter's own MAC and to AP BSSIDs alike. A BSSID is the most
 * directly geolocatable field in a scan result — never log the raw value above
 * trace.
 */
std::string mac(std::string_view mac);

/**
 * @brief Deterministic variants that take an explicit salt.
 *
 * `ssid()` and `mac()` are these, applied to the process-wide per-boot salt.
 * Use these directly only when reproducibility matters — resolving an old
 * bundle against a known salt, or asserting on a token in a test. Passing a
 * fixed salt in production would defeat the point of salting.
 */
std::string ssid_with_salt(std::string_view ssid, uint64_t salt);
std::string mac_with_salt(std::string_view mac, uint64_t salt);

} // namespace helix::redact
