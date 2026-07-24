// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file remote_transport.h
 * @brief Pluggable transport interface for the remote-control server
 *
 * RemoteControlServer owns the transport-agnostic JSON-RPC dispatch (parse,
 * method registry, marshalling to the LVGL main thread). A transport is the
 * wire: it accepts connections, reads request lines, hands each to the
 * dispatcher, and writes the response back.
 *
 * Two backends implement this interface:
 *   - UnixSocketTransport: local AF_UNIX socket (default; safe, no network).
 *   - HttpTransport:       HTTP/TCP via libhv (opt-in; LAN control + serves the
 *                          future web config UI from the same embedded server).
 *
 * The whole subsystem is dev/test-only and compiled out of release builds
 * (see HELIX_ENABLE_REMOTE_CONTROL in the Makefile).
 */

#include <functional>
#include <string>

namespace helix {

class IRemoteTransport {
  public:
    /**
     * @brief Handle one JSON-RPC request line and return its response.
     *
     * The line is a single JSON-RPC request with no trailing newline; the
     * return value is the response JSON (also newline-free). Invoked on a
     * transport-owned thread — the handler itself marshals UI work to the main
     * thread and blocks on the result.
     */
    using RequestHandler = std::function<std::string(const std::string& request_line)>;

    virtual ~IRemoteTransport() = default;

    /**
     * @brief Begin serving. Non-blocking — spawns the transport's own thread(s).
     * @param handler The dispatcher to feed each request line.
     * @return true on success, false on bind/listen failure.
     */
    virtual bool start(RequestHandler handler) = 0;

    /**
     * @brief Stop serving and release resources. Idempotent.
     */
    virtual void stop() = 0;

    /**
     * @brief Human-readable endpoint for logs (socket path or host:port).
     */
    virtual std::string endpoint() const = 0;
};

} // namespace helix
