// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unix_socket_transport.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static constexpr size_t MAX_CLIENT_BUFFER = 65536; // 64KB max per request line.

namespace helix {

UnixSocketTransport::UnixSocketTransport(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

int UnixSocketTransport::create_listener() {
    // Remove any stale socket file from a prior unclean exit.
    unlink(socket_path_.c_str());

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        spdlog::error("[RemoteControl] Failed to create socket: {}", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path_.length() >= sizeof(addr.sun_path)) {
        spdlog::error("[RemoteControl] Socket path too long: {}", socket_path_);
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[RemoteControl] Failed to bind socket at {}: {}", socket_path_,
                      strerror(errno));
        close(fd);
        return -1;
    }

    // Restrict access to the owner only.
    chmod(socket_path_.c_str(), 0600);

    if (listen(fd, 1) < 0) {
        spdlog::error("[RemoteControl] Failed to listen on socket: {}", strerror(errno));
        close(fd);
        unlink(socket_path_.c_str());
        return -1;
    }

    return fd;
}

void UnixSocketTransport::serve_client(int client_fd) {
    std::string buffer;
    char chunk[4096];

    // Read timeout so a stalled client can't pin the accept thread forever.
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running_.load()) {
        ssize_t n = read(client_fd, chunk, sizeof(chunk) - 1);
        if (n <= 0) {
            break; // Client disconnected or error.
        }
        chunk[n] = '\0';
        buffer.append(chunk, static_cast<size_t>(n));

        if (buffer.size() > MAX_CLIENT_BUFFER) {
            spdlog::warn("[RemoteControl] Client buffer overflow (>64KB), disconnecting");
            return;
        }

        // Process each complete newline-delimited request.
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (line.empty()) {
                continue;
            }

            std::string response = handler_(line);
            response += '\n';
            if (!write_all(client_fd, response.c_str(), response.length())) {
                spdlog::warn("[RemoteControl] Failed to write response: {}", strerror(errno));
                return;
            }
        }
    }
}

void UnixSocketTransport::on_stopped() {
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        spdlog::debug("[RemoteControl] Removed socket file: {}", socket_path_);
    }
}

} // namespace helix
