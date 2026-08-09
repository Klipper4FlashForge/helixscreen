// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Local evidence about Moonraker, gathered without asking Moonraker
 *
 * Every Moonraker-derived section of a debug bundle is fetched over HTTP from
 * Moonraker itself, so the one failure that most needs explaining produces a
 * bundle that says nothing about it: AD5X bundles TAU4PW4H / 865DXBQ7 carry five
 * `{"error": "No response from ..."}` entries, a `connection_state: failed`, and
 * no way to tell "not running" from "running, bound somewhere we did not dial".
 *
 * These probes read /proc instead. They only mean anything when Moonraker is
 * supposed to be on this machine — on a remote printer our own /proc says
 * nothing about it — so the caller gates on is_moonraker_on_same_host().
 */
namespace helix::diag {

/// A process matched by find_moonraker_processes().
struct ProcMatch {
    long pid = 0;
    std::string cmdline; ///< NULs collapsed to spaces
};

/**
 * @brief Local addresses in LISTEN state on @p port, from /proc/net/tcp content
 *
 * @param proc_net_tcp Raw file contents (/proc/net/tcp or /proc/net/tcp6).
 * @param port         Host-order port to match.
 * @param ipv6         True when parsing tcp6 (32-hex-char addresses).
 * @return Decoded "addr:port" strings, one per listening socket; empty if none.
 *
 * Endianness: the kernel prints the address with %08X applied to a __be32, so
 * the printed number depends on the host's byte order — "0100007F" is 127.0.0.1
 * on little-endian, and the same address prints as "7F000001" on big-endian.
 * The decode recovers the bytes through the host's own representation rather
 * than assuming an order, so it is correct on both. It matters here: the
 * reporter's AD5X is MIPS. The port field is `%04X` of a host-order u16 and is
 * unambiguous either way, which is why matching is done on the port.
 */
std::vector<std::string> parse_listeners_for_port(const std::string& proc_net_tcp, uint16_t port,
                                                  bool ipv6);

/**
 * @brief Split "http://127.0.0.1:7125" into host and port
 *
 * Exists so the probe can read the endpoint off the Moonraker API's base URL
 * instead of from Config: the bundle collects on HttpExecutor::slow(), and
 * Config is main-thread-only (see debug_bundle_collector.cpp's note on
 * collect_update_info()).
 *
 * @param base_url Scheme optional; a bracketed IPv6 host is unwrapped.
 * @param host     Out: host with no scheme, port, or path. Untouched on failure.
 * @param port     Out: explicit port, else 80/443 by scheme, else @p fallback.
 * @return False when no host could be recovered.
 */
bool split_host_port(const std::string& base_url, std::string& host, uint16_t& port,
                     uint16_t fallback = 7125);

/// Collapse a raw /proc/<pid>/cmdline (NUL-separated argv) into one line.
/// Trailing NULs are dropped; embedded ones become single spaces.
std::string decode_proc_cmdline(const std::string& raw);

/// True when @p cmdline contains any of @p needles (case-sensitive substring).
bool cmdline_matches_any(const std::string& cmdline, const std::vector<std::string>& needles);

/// Log-file locations implied by a moonraker/klippy command line. Empty strings
/// where the flag was absent.
struct LogPathHints {
    std::string log_file;    ///< -l / --logfile: the log itself
    std::string data_path;   ///< -d / --datapath: logs live in <data_path>/logs
    std::string config_file; ///< -c / --configfile: logs are typically ../logs
};

/// Extract the log-location flags from a process command line.
LogPathHints parse_log_hints(const std::string& cmdline);

/**
 * @brief Candidate on-disk paths for @p log_name, derived from running processes
 *
 * @param procs    Output of find_moonraker_processes().
 * @param log_name Bare file name, e.g. "moonraker.log" or "klippy.log".
 * @return Absolute paths, most authoritative first, deduplicated.
 *
 * Deliberately derived from the daemon's own argv rather than from a hardcoded
 * list of per-platform data roots. The reporter's platform is an AD5X running
 * ZMOD, whose layout is not documented here and which nobody on this project has
 * a device to check — a guessed path list would be fiction that looks like
 * knowledge, and would silently return nothing on every layout not on the list.
 * argv is ground truth wherever the daemon is actually running. An empty result
 * is a real answer ("we could not tell where the logs are"), not a failure to
 * try harder.
 */
std::vector<std::string> candidate_log_paths(const std::vector<ProcMatch>& procs,
                                             const std::string& log_name);

/// Listening addresses on @p port across /proc/net/tcp and tcp6. Empty when
/// nothing is listening or /proc is unavailable.
std::vector<std::string> listeners_on_port(uint16_t port);

/// Processes whose cmdline mentions Moonraker or Klipper. Empty on a system
/// without /proc, or when neither is running — which is itself the finding.
std::vector<ProcMatch> find_moonraker_processes();

} // namespace helix::diag
