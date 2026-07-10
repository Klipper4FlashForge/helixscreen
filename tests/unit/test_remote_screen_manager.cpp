// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_remote_screen_manager.cpp
 * @brief Routing tests for RemoteScreenManager.
 *
 * A FakeSink records on_frame() call count and the last frame it saw, and
 * models the real activation rule: it wants frames only when both enabled
 * (wants_) and started (started_). These tests assert the manager forwards
 * exactly to willing, started sinks and early-outs when empty.
 */

#include "remote_screen_manager.h"
#include "remote_screen_sink.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

// Records routing behavior. wants_frames() == (wants_ && started_) so a sink
// whose start() left it unstarted never receives frames.
class FakeSink : public RemoteScreenSink {
  public:
    bool  start_returns_ = true; // what start() reports
    bool  wants_         = true; // "enabled" flag
    bool  started_       = false;

    int              on_frame_count_ = 0;
    RemoteScreenFrame last_frame_{};

    bool start() override {
        started_ = start_returns_;
        return start_returns_;
    }

    void stop() override {
        started_ = false;
    }

    bool wants_frames() const override {
        return wants_ && started_;
    }

    void on_frame(const RemoteScreenFrame& frame) override {
        ++on_frame_count_;
        last_frame_ = frame;
    }

    const char* name() const override {
        return "FakeSink";
    }
};

RemoteScreenFrame make_frame() {
    RemoteScreenFrame f;
    static const uint8_t px = 0xAB;
    f.px_map       = &px;
    f.x1           = 10;
    f.y1           = 20;
    f.x2           = 41;
    f.y2           = 51;
    f.disp_w       = 480;
    f.disp_h       = 320;
    f.color_format = 16; // arbitrary lv_color_format_t value
    f.src_stride   = 128;
    return f;
}

} // namespace

TEST_CASE("RemoteScreenManager: empty manager is a no-op", "[remote_screen]") {
    RemoteScreenManager mgr;
    REQUIRE_FALSE(mgr.any_active());
    // Must not crash / must be safe with no sinks.
    mgr.on_frame(make_frame());
    REQUIRE_FALSE(mgr.any_active());
}

TEST_CASE("RemoteScreenManager: forwards to a willing started sink", "[remote_screen]") {
    RemoteScreenManager mgr;
    auto sink    = std::make_unique<FakeSink>();
    auto* raw    = sink.get();
    sink->wants_ = true;
    mgr.add_sink(std::move(sink));
    mgr.start(); // sets started_ = true

    REQUIRE(raw->started_);
    REQUIRE(mgr.any_active());

    RemoteScreenFrame f = make_frame();
    mgr.on_frame(f);

    REQUIRE(raw->on_frame_count_ == 1);
    // Frame fields are preserved verbatim.
    REQUIRE(raw->last_frame_.px_map == f.px_map);
    REQUIRE(raw->last_frame_.x1 == f.x1);
    REQUIRE(raw->last_frame_.y1 == f.y1);
    REQUIRE(raw->last_frame_.x2 == f.x2);
    REQUIRE(raw->last_frame_.y2 == f.y2);
    REQUIRE(raw->last_frame_.disp_w == f.disp_w);
    REQUIRE(raw->last_frame_.disp_h == f.disp_h);
    REQUIRE(raw->last_frame_.color_format == f.color_format);
    REQUIRE(raw->last_frame_.src_stride == f.src_stride);
}

TEST_CASE("RemoteScreenManager: does not forward to an unwilling sink", "[remote_screen]") {
    RemoteScreenManager mgr;
    auto sink    = std::make_unique<FakeSink>();
    auto* raw    = sink.get();
    sink->wants_ = false; // enabled flag off
    mgr.add_sink(std::move(sink));
    mgr.start();

    REQUIRE_FALSE(mgr.any_active());
    mgr.on_frame(make_frame());
    REQUIRE(raw->on_frame_count_ == 0);
}

TEST_CASE("RemoteScreenManager: routes only to the willing sink of two", "[remote_screen]") {
    RemoteScreenManager mgr;

    auto willing     = std::make_unique<FakeSink>();
    auto* willing_r  = willing.get();
    willing->wants_  = true;

    auto quiet       = std::make_unique<FakeSink>();
    auto* quiet_r    = quiet.get();
    quiet->wants_    = false;

    mgr.add_sink(std::move(willing));
    mgr.add_sink(std::move(quiet));
    mgr.start();

    REQUIRE(mgr.any_active());
    mgr.on_frame(make_frame());

    REQUIRE(willing_r->on_frame_count_ == 1);
    REQUIRE(quiet_r->on_frame_count_ == 0);
}

TEST_CASE("RemoteScreenManager: a sink whose start() failed never receives frames",
          "[remote_screen]") {
    RemoteScreenManager mgr;
    auto sink            = std::make_unique<FakeSink>();
    auto* raw            = sink.get();
    sink->wants_         = true;  // "enabled"
    sink->start_returns_ = false; // but start() fails -> stays unstarted
    mgr.add_sink(std::move(sink));
    mgr.start();

    REQUIRE_FALSE(raw->started_);
    REQUIRE_FALSE(mgr.any_active());

    mgr.on_frame(make_frame());
    REQUIRE(raw->on_frame_count_ == 0);

    // Even if the enabled flag is flipped later, without a successful start()
    // wants_frames() stays false, so still no frames.
    raw->wants_ = true;
    REQUIRE_FALSE(mgr.any_active());
    mgr.on_frame(make_frame());
    REQUIRE(raw->on_frame_count_ == 0);
}
