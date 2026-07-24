// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file unix_socket_transport.h
 * @brief Local AF_UNIX transport for the remote-control server (default).
 *
 * Newline-delimited JSON-RPC over a Unix domain socket, restricted to the
 * owner (0600). This is the safe local default — no network exposure.
 */

#include "socket_server_base.h"

#include <string>

namespace helix {

class UnixSocketTransport : public SocketServerBase {
  public:
    explicit UnixSocketTransport(std::string socket_path);

    std::string endpoint() const override {
        return socket_path_;
    }

  protected:
    int create_listener() override;
    void serve_client(int client_fd) override;
    void on_stopped() override;

  private:
    std::string socket_path_;
};

} // namespace helix
