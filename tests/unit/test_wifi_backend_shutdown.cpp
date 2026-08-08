// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hv/EventLoopThread.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "../catch_amalgamated.hpp"

/**
 * WiFi Backend Shutdown Safety Tests
 *
 * Tests for the use-after-free race condition (GitHub issue #8):
 * When WifiBackendWpaSupplicant::start() times out, the event loop thread
 * is still running init_wpa(). If the backend is destroyed while the thread
 * is blocked (e.g., in wpa_ctrl_attach()), cleanup_wpa() frees resources
 * that the thread is still using → segfault.
 *
 * These tests exercise the EXACT same hv::EventLoopThread pattern used by
 * WifiBackendWpaSupplicant without requiring wpa_supplicant (Linux-only).
 */

// ============================================================================
// Test helper: mimics WifiBackendWpaSupplicant's threading pattern
// ============================================================================

/**
 * Reproduces the exact threading pattern from WifiBackendWpaSupplicant:
 * - Inherits privately from hv::EventLoopThread
 * - start() spawns a thread running a slow init, waits with timeout
 * - Destructor must safely clean up even if thread is still running
 *
 * The "resource" simulates conn/mon_conn pointers that init uses.
 */
class SlowInitBackend : private hv::EventLoopThread {
  public:
    SlowInitBackend() : hv::EventLoopThread(nullptr) {
        // Simulate conn/mon_conn: a resource allocated during init
        resource_ = new std::atomic<int>(0);
    }

    ~SlowInitBackend() {
        // BUG REPRODUCTION: This is what WifiBackendWpaSupplicant does today:
        // 1. stop() returns early because init_complete_ is false
        // 2. cleanup frees the resource while the thread is still using it
        // 3. ~EventLoopThread joins the thread AFTER the resource is freed
        if (!init_complete_.load()) {
            // Mimic: stop() returns early
        }

        // Mimic: cleanup_wpa() frees resources before thread is joined
        cleanup();

        // ~EventLoopThread() will call stop() + join() here
    }

    /**
     * Start with a timeout, just like WifiBackendWpaSupplicant::start().
     * Returns true if init completed in time, false on timeout.
     */
    bool start_with_timeout(int timeout_ms) {
        init_complete_ = false;

        hv::EventLoopThread::start(true, [this]() -> int {
            slow_init();
            return 0;
        });

        std::unique_lock<std::mutex> lock(init_mutex_);
        return init_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [this] { return init_complete_.load(); });
    }

    // Expose for test assertions
    bool init_completed() const {
        return init_complete_.load();
    }
    bool resource_was_accessed_after_free() const {
        return accessed_after_free_.load();
    }
    int init_progress() const {
        return init_progress_.load();
    }

  private:
    void slow_init() {
        // Simulate the blocking init_wpa() → wpa_ctrl_attach() pattern.
        // Accesses the shared resource repeatedly (like the thread accessing
        // conn/mon_conn during wpa_ctrl operations).
        for (int i = 0; i < 50; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            init_progress_ = i;

            // Access the resource (simulates wpa_ctrl_attach using mon_conn)
            if (resource_) {
                resource_->store(i);
            } else {
                // Resource was freed while we were using it!
                accessed_after_free_ = true;
                break;
            }
        }

        init_complete_ = true;
        init_cv_.notify_all();
    }

    void cleanup() {
        // Simulates cleanup_wpa(): frees the resource
        delete resource_;
        resource_ = nullptr;
    }

    std::atomic<int>* resource_{nullptr};
    std::atomic<bool> init_complete_{false};
    std::atomic<bool> accessed_after_free_{false};
    std::atomic<int> init_progress_{0};
    std::mutex init_mutex_;
    std::condition_variable init_cv_;
};

/**
 * Fixed version: waits for thread to finish before freeing resources.
 * This is the pattern the fix should implement.
 */
class SafeShutdownBackend : private hv::EventLoopThread {
  public:
    SafeShutdownBackend() : hv::EventLoopThread(nullptr) {
        resource_ = new std::atomic<int>(0);
    }

    ~SafeShutdownBackend() {
        // FIX: Signal shutdown, stop the event loop, join the thread,
        // THEN free resources.
        shutdown_requested_ = true;

        // Stop the event loop and wait for thread to finish
        hv::EventLoopThread::stop(true);

        // NOW safe to free resources - thread is done
        cleanup();
    }

    bool start_with_timeout(int timeout_ms) {
        init_complete_ = false;
        shutdown_requested_ = false;

        hv::EventLoopThread::start(true, [this]() -> int {
            slow_init();
            return 0;
        });

        std::unique_lock<std::mutex> lock(init_mutex_);
        return init_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [this] { return init_complete_.load(); });
    }

    bool init_completed() const {
        return init_complete_.load();
    }
    bool resource_was_accessed_after_free() const {
        return accessed_after_free_.load();
    }
    int init_progress() const {
        return init_progress_.load();
    }

    /**
     * Process-wide latch set when any worker finds its resource already freed.
     *
     * The per-instance accessed_after_free_ flag is unreadable in the stress
     * loop, because the only interesting moment is after the backend is gone.
     * A static latch survives the destructor, which is what makes "no cycle
     * freed resources out from under its own thread" checkable at all.
     *
     * Note this is what the cycle loop can prove, and joining is not: libhv's
     * ~EventLoopThread always calls stop() + join(), so no destructor ordering
     * leaves a thread running past full destruction. Ordering is the real
     * variable - free-then-join versus join-then-free - and that is exactly
     * what this latch detects.
     */
    static std::atomic<bool>& uaf_detected() {
        static std::atomic<bool> flag{false};
        return flag;
    }

  private:
    void slow_init() {
        for (int i = 0; i < 50 && !shutdown_requested_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            init_progress_ = i;

            if (resource_) {
                resource_->store(i);
            } else {
                accessed_after_free_ = true;
                uaf_detected().store(true);
                break;
            }
        }

        init_complete_ = true;
        init_cv_.notify_all();
    }

    void cleanup() {
        delete resource_;
        resource_ = nullptr;
    }

    std::atomic<int>* resource_{nullptr};
    std::atomic<bool> init_complete_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> accessed_after_free_{false};
    std::atomic<int> init_progress_{0};
    std::mutex init_mutex_;
    std::condition_variable init_cv_;
};

/**
 * Regression for bundle WWZE4K9T (v0.99.96/pi32): SIGSEGV at PC=0x0 inside
 * std::__future_base::_State_baseV2::_M_do_set, on the libhv event-loop
 * background thread. The unsafe pattern was a LOCAL std::promise captured by
 * reference into a runInLoop lambda:
 *
 *   std::promise<void> cleanup_done;
 *   loop()->runInLoop([this, &cleanup_done]() {
 *       cleanup_wpa();
 *       cleanup_done.set_value();   // UAF if stop() already returned
 *   });
 *   if (cleanup_future.wait_for(2s) == timeout) { warn_only(); }
 *
 * Trigger: init_wpa() (queued ahead on the same event loop) blocks 5+ s in
 * wpa_ctrl_attach(); stop()'s 2 s wait_for times out; stop() returns and the
 * local promise is destroyed; init_wpa eventually returns and the event loop
 * runs the cleanup lambda, which calls set_value() on freed memory, leading
 * to a null function pointer call and a crash in _M_do_set.
 *
 * The fix captures a shared_ptr<std::promise<void>> BY VALUE into the lambda
 * so the promise lives until the lambda releases it, regardless of whether
 * stop() has already returned. This class mirrors that fixed pattern so the
 * test exercises the exact runInLoop + wait_for + deferred-cleanup shape.
 */
class SafeDeferredCleanupBackend : private hv::EventLoopThread {
  public:
    SafeDeferredCleanupBackend() : hv::EventLoopThread(nullptr) {}

    ~SafeDeferredCleanupBackend() {
        shutdown_requested_ = true;
        hv::EventLoopThread::stop(true);
    }

    /**
     * Start the event loop with a long-running init task queued ahead of any
     * subsequent runInLoop work. Mirrors init_wpa() blocked on wpa_ctrl_attach()
     * for 5+ seconds while stop() is trying to schedule cleanup_wpa() behind it.
     */
    void start_with_blocked_init(unsigned init_iters = 30) {
        hv::EventLoopThread::start(true, [this, init_iters]() -> int {
            slow_init(init_iters);
            return 0;
        });
    }

    /**
     * Mirrors WifiBackendWpaSupplicant::stop()'s fixed pattern: queue cleanup
     * via runInLoop, wait briefly, return even if cleanup hasn't run yet.
     * Returns the future so callers can observe the deferred set_value().
     */
    std::future<void> stop_with_deferred_cleanup(int timeout_ms) {
        auto cleanup_done = std::make_shared<std::promise<void>>();
        std::future<void> cleanup_future = cleanup_done->get_future();

        loop()->runInLoop([this, cleanup_done]() {
            cleanup_ran_ = true;
            cleanup_done->set_value();
        });

        if (cleanup_future.wait_for(std::chrono::milliseconds(timeout_ms)) ==
            std::future_status::timeout) {
            cleanup_timed_out_ = true;
        }
        // After return, our local shared_ptr is destroyed; the promise lives
        // only via the captured copy inside the still-pending runInLoop lambda.
        return cleanup_future;
    }

    bool cleanup_ran() const {
        return cleanup_ran_.load();
    }
    bool cleanup_timed_out() const {
        return cleanup_timed_out_.load();
    }

  private:
    void slow_init(unsigned iters) {
        for (unsigned i = 0; i < iters && !shutdown_requested_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            init_progress_ = static_cast<int>(i);
        }
    }

    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> cleanup_ran_{false};
    std::atomic<bool> cleanup_timed_out_{false};
    std::atomic<int> init_progress_{0};
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Slow init timeout triggers use-after-free in unsafe backend",
          "[network][backend][shutdown][issue-8][eventloop][slow]") {
    SECTION("Resource accessed after free when init times out") {
        // This reproduces the exact bug from GitHub issue #8:
        // 1. Start init with a short timeout (init takes 5s, timeout is 500ms)
        // 2. Timeout fires, start returns false
        // 3. Backend is destroyed while thread is still in slow_init()
        // 4. cleanup() frees the resource
        // 5. Thread continues accessing the freed resource

        {
            SlowInitBackend backend;
            bool completed = backend.start_with_timeout(500);

            // Init should NOT have completed (500ms timeout, init takes 5s)
            REQUIRE_FALSE(completed);
            REQUIRE_FALSE(backend.init_completed());

            // Destroying backend here while thread is still running.
            // The unsafe destructor frees resources before joining the thread.
            // On the real system (RPi), this causes Signal 11 because the
            // thread is still in wpa_ctrl_attach() using the freed socket.
        }
        // If we get here, the thread happened to not crash (timing dependent).
        // The real test is the SafeShutdownBackend - it GUARANTEES no crash.
    }
}

TEST_CASE("Safe shutdown backend waits for thread before cleanup",
          "[network][backend][shutdown][issue-8][eventloop][slow]") {
    SECTION("No use-after-free with shutdown signal") {
        SafeShutdownBackend backend;
        bool completed = backend.start_with_timeout(500);

        // Init should NOT have completed (500ms timeout, init takes 5s)
        REQUIRE_FALSE(completed);
        REQUIRE_FALSE(backend.init_completed());

        // Destroying backend here - safe version waits for thread
    }
    // If we get here without crash, the safe pattern works!
    SUCCEED("Backend destroyed safely without use-after-free");
}

TEST_CASE("Safe shutdown backend responds to cancellation quickly",
          "[network][backend][shutdown][issue-8][eventloop][slow]") {
    SECTION("Shutdown flag causes init to abort early") {
        auto start = std::chrono::steady_clock::now();

        {
            SafeShutdownBackend backend;
            backend.start_with_timeout(200);

            // Backend will be destroyed here - should be fast due to
            // shutdown_requested_ causing the init loop to break
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Should take well under 5s (the full init time) because
        // shutdown_requested_ causes early exit from the init loop.
        // Allow generous margin for CI but it should be ~300-500ms
        REQUIRE(elapsed_ms < 2000);
        INFO("Shutdown took " << elapsed_ms << "ms");
    }
}

TEST_CASE("Safe shutdown never accesses freed resources",
          "[network][backend][shutdown][issue-8][eventloop][slow]") {
    SECTION("Repeated start/timeout/destroy cycles are safe") {
        SafeShutdownBackend::uaf_detected().store(false);

        // Stress test: rapidly create, timeout, destroy
        for (int i = 0; i < 5; i++) {
            INFO("cycle " << i);
            SafeShutdownBackend backend;
            backend.start_with_timeout(100);

            // slow_init takes 5s; a 100ms timeout must expire against a worker
            // that is still running. If this ever passed, the cycle would be
            // tearing down an already-finished backend and proving nothing about
            // the timeout path this test exists to stress.
            REQUIRE_FALSE(backend.init_completed());
            // Destroy immediately
        }

        // Issue #8 itself: every cycle signalled shutdown and joined its worker
        // BEFORE freeing the resource the worker is still dereferencing. Reorder
        // those two steps and a worker wakes to a null resource_ on its next
        // 100ms tick, which is what this latch records.
        REQUIRE_FALSE(SafeShutdownBackend::uaf_detected().load());
    }
}

TEST_CASE("stop() with shared_ptr promise survives deferred cleanup",
          "[network][backend][shutdown][wwze4k9t][eventloop][slow]") {
    SECTION("Cleanup lambda runs after stop() timeout without UAF") {
        // Reproduces the WWZE4K9T race shape:
        // 1. Event loop is busy with a multi-second init task (init_wpa blocked
        //    on wpa_ctrl_attach).
        // 2. stop_with_deferred_cleanup() queues cleanup behind it and waits
        //    200ms. slow_init is still running, so wait_for times out.
        // 3. stop_with_deferred_cleanup returns, dropping its local shared_ptr.
        //    With the OLD ref-capture pattern this would free the promise; with
        //    the shared_ptr fix the lambda's captured copy keeps it alive.
        // 4. slow_init finishes (~3s) and the event loop runs the cleanup
        //    lambda. set_value() must succeed rather than crash in _M_do_set.

        SafeDeferredCleanupBackend backend;
        backend.start_with_blocked_init();

        // Let slow_init get well underway on the event loop before queueing
        // cleanup behind it.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto cleanup_future = backend.stop_with_deferred_cleanup(200);

        // stop_with_deferred_cleanup returned on timeout — cleanup hasn't run.
        REQUIRE(backend.cleanup_timed_out());
        REQUIRE_FALSE(backend.cleanup_ran());

        // The deferred cleanup eventually fires once slow_init finishes. With
        // the shared_ptr fix, set_value() succeeds (promise still alive); with
        // the original ref-capture bug this would crash in _M_do_set at PC=0.
        REQUIRE(cleanup_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        REQUIRE(backend.cleanup_ran());
    }

    SECTION("Repeated stop-with-deferred-cleanup cycles are safe") {
        for (int i = 0; i < 3; i++) {
            SafeDeferredCleanupBackend backend;
            backend.start_with_blocked_init();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto fut = backend.stop_with_deferred_cleanup(100);
            REQUIRE(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
            REQUIRE(backend.cleanup_ran());
        }
        SUCCEED("All cycles completed; promise stayed alive for deferred set_value");
    }
}
