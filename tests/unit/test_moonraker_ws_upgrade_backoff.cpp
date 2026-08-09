// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_ws_upgrade_backoff.cpp
 * @brief Backoff must survive a peer that accepts TCP but never upgrades.
 *
 * libhv's TcpClient resets the reconnect backoff in its onconnect handler —
 * i.e. when the TCP handshake completes, before the WebSocket upgrade has been
 * attempted. For a plain TCP client that is right. For a WebSocket client it
 * means any peer that accepts a connection and then fails the upgrade resets
 * cur_delay to min_delay on every single cycle, so the exponential backoff
 * never engages and the client reconnects at min_delay forever.
 *
 * That is not exotic: a reverse proxy up with Moonraker down (502/503), a wrong
 * WS path (404), a non-Moonraker server on 7125, or a Sec-WebSocket-Accept
 * mismatch all land here. At the shipped 200ms min_delay that is a 5Hz
 * reconnect loop against a printer, indefinitely.
 *
 * patches/libhv-websocket-backoff-on-upgrade.patch snapshots the backoff at TCP
 * connect and restores it on close unless WS_OPENED was reached. This test
 * drives a listener that accepts and immediately closes, then counts how many
 * connections arrive in a fixed window: pinned-at-min is an order of magnitude
 * more than a backoff that actually grows.
 */

#include "../../include/moonraker_client.h"
#include "hv/EventLoopThread.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Listener that accepts a connection and closes it at once, without ever
/// answering the WebSocket upgrade. Counts how many times it was reached.
class RudeListener {
  public:
    RudeListener() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        // No leading :: on htonl/ntohs — they are macros on macOS.
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ephemeral
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(fd_, 8) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);

        thread_ = std::thread([this]() {
            while (!stop_.load()) {
                int c = ::accept(fd_, nullptr, nullptr);
                if (c < 0) {
                    return;
                }
                accepts_.fetch_add(1);
                ::close(c); // never upgrade
            }
        });
    }

    ~RudeListener() {
        stop_.store(true);
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint16_t port() const {
        return port_;
    }
    int accepts() const {
        return accepts_.load();
    }

  private:
    int fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> stop_{false};
    std::atomic<int> accepts_{0};
    std::thread thread_;
};

} // namespace

TEST_CASE("A failed WebSocket upgrade still backs off",
          "[moonraker][client][regression][libhv][eventloop][slow]") {
    RudeListener server;

    auto loop = std::make_shared<hv::EventLoopThread>();
    loop->start();
    auto client = std::make_unique<MoonrakerClient>(loop->loop());

    // min 100ms, max 1600ms. Unpatched, every cycle resets to 100ms.
    // Patched: 100, 200, 400, 800, 1600, 1600...
    client->configure_timeouts(/*connection*/ 2000, /*request*/ 2000, /*keepalive*/ 10000,
                               /*reconnect_min*/ 100, /*reconnect_max*/ 1600);
    // Keep the initial-connection escalation out of this measurement.
    client->set_initial_connect_failure_timeout(60000);

    const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/websocket";
    client->connect(url.c_str(), []() {}, []() {});

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    const int hits = server.accepts();

    client->disconnect();
    loop->stop();
    loop->join();
    client.reset();

    INFO("connections accepted in 2.5s: " << hits);

    // It must keep trying — a backoff that stops retrying is a different bug.
    CHECK(hits >= 2);

    // Pinned at 100ms this is ~20-25 in the window. Growing 100/200/400/800/1600
    // it is ~6. The gap is wide enough that 12 separates them without being
    // sensitive to scheduler jitter or connect latency.
    CHECK(hits < 12);
}
