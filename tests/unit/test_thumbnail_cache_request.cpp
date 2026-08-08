// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_thumbnail_cache_request.cpp
 * @brief ThumbnailRequest and the unified guarded fetch core.
 *
 * ThumbnailCache::fetch(ThumbnailRequest, ThumbnailLoadContext, ...) is the one
 * entry point every consumer is being moved onto. Its whole reason to exist is
 * the staleness guard: a load that a newer request has superseded must never
 * reach the caller's success callback, because that callback writes a widget or
 * a subject describing a file the UI has already moved off.
 *
 * The trap when testing that is proving a negative against a path that could
 * never have fired anyway. A fetch with a null api and nothing in the cache
 * calls on_error and returns - "on_success did not fire" is then true whether
 * or not the guard exists at all. So these tests plant a real pre-scaled .bin
 * first, which makes fetch() resolve synchronously with an actual success to
 * deliver, and assert BOTH sides: a live context receives that path, a
 * superseded one does not.
 */

#include "../../include/async_lifetime_guard.h"
#include "../../include/thumbnail_cache.h"
#include "../../include/thumbnail_load_context.h"
#include "../../include/thumbnail_processor.h"
#include "../lvgl_test_fixture.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

/// Smallest valid PNG the cache and the processor will both accept: a 10x10
/// solid-colour square, 75 bytes. Same bytes as tests/unit/test_thumbnail_scaling.cpp.
// clang-format off
const std::vector<uint8_t> kTinyPng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x02, 0x50, 0x58, 0xEA, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x68, 0x70, 0x50, 0xC0,
    0x83, 0x18, 0x46, 0xA5, 0xB1, 0x21, 0x00, 0x24, 0x51, 0x57, 0x81, 0xF7,
    0xEC, 0xA3, 0x23, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};
// clang-format on

/// Fixed target rather than get_target_for_display(): the pre-scaled key must
/// be identical between the plant and the lookup, and must not depend on which
/// display the test binary happens to have created.
helix::ThumbnailTarget target_120() {
    helix::ThumbnailTarget t;
    t.width = 120;
    t.height = 120;
    return t;
}

/// Unique per process so a leftover from another shard cannot answer for us.
std::string unique_key(const char* tag) {
    return std::string("thumb_request_") + tag + "_" + std::to_string(::getpid()) + ".png";
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "ThumbnailRequest: fetch delivers when the context is live",
                 "[thumbnail][request]") {
    ThumbnailCache cache;
    auto& processor = helix::ThumbnailProcessor::instance();
    const helix::ThumbnailTarget target = target_120();
    const std::string key = unique_key("live");

    const auto planted = processor.process_sync(kTinyPng, key, target);
    REQUIRE(planted.success);
    REQUIRE(ThumbnailCache::is_lvgl_path(planted.output_path));

    ThumbnailRequest req;
    req.key = key;
    req.target = target;

    // The request overload of get_if_cached must see the same pre-scaled file
    // the fetch path will resolve to.
    CHECK(cache.get_if_cached(req) == planted.output_path);

    std::atomic<uint32_t> gen{0};
    helix::AsyncLifetimeGuard guard;
    auto ctx = ThumbnailLoadContext::create(guard, &gen);

    std::string delivered;
    cache.fetch(
        req, ctx, [&delivered](const std::string& path) { delivered = path; },
        [](const std::string& error) { FAIL_CHECK("unexpected fetch error: " << error); });

    CHECK(delivered == planted.output_path);

    cache.invalidate(key);
}

TEST_CASE_METHOD(LVGLTestFixture, "ThumbnailRequest: fetch suppresses a superseded context",
                 "[thumbnail][request]") {
    ThumbnailCache cache;
    auto& processor = helix::ThumbnailProcessor::instance();
    const helix::ThumbnailTarget target = target_120();
    const std::string key = unique_key("stale");

    const auto planted = processor.process_sync(kTinyPng, key, target);
    REQUIRE(planted.success);

    ThumbnailRequest req;
    req.key = key;
    req.target = target;

    // Pinned: without this the "no callback" assertion below would hold for the
    // trivial reason that there was nothing to deliver.
    REQUIRE_FALSE(cache.get_if_cached(req).empty());

    std::atomic<uint32_t> gen{0};
    helix::AsyncLifetimeGuard guard;
    auto ctx = ThumbnailLoadContext::create(guard, &gen);

    // A newer request supersedes ctx before its callback can run.
    ++gen;
    REQUIRE_FALSE(ctx.is_valid());

    bool success_fired = false;
    cache.fetch(req, ctx, [&success_fired](const std::string&) { success_fired = true; }, nullptr);

    CHECK_FALSE(success_fired);

    cache.invalidate(key);
}

/// fetch_for_detail_view is the path both active-print consumers use, and it had
/// no source_modified parameter at all - so mtime validation never ran on it.
/// Re-slice a model under the same filename and the cache kept serving the old
/// image forever. The two blocks below are deliberately paired: the first proves
/// the planted entry IS servable, so the second's "not served" is about freshness
/// and not about an empty cache. With no api the re-fetch cannot proceed, which
/// is what makes the difference observable - fresh source means on_error, a stale
/// cache hit means on_success carrying the old path.
TEST_CASE_METHOD(LVGLTestFixture,
                 "ThumbnailRequest: detail view honors a source newer than the cached file",
                 "[thumbnail][request]") {
    ThumbnailCache cache;
    auto& processor = helix::ThumbnailProcessor::instance();

    // fetch_for_detail_view picks its own target internally, so the plant has to
    // use the same one or there is no entry for the fetch to hit.
    const helix::ThumbnailTarget target =
        helix::ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Detail);
    const std::string key = unique_key("detail_mtime");

    const auto planted = processor.process_sync(kTinyPng, key, target);
    REQUIRE(planted.success);
    REQUIRE(ThumbnailCache::is_lvgl_path(planted.output_path));

    std::atomic<uint32_t> gen{0};
    helix::AsyncLifetimeGuard guard;

    {
        auto ctx = ThumbnailLoadContext::create(guard, &gen);
        std::string delivered;
        cache.fetch_for_detail_view(
            nullptr, key, ctx, [&delivered](const std::string& path) { delivered = path; },
            [](const std::string& error) {
                FAIL_CHECK("unexpected detail view error: " << error);
            });
        REQUIRE(delivered == planted.output_path);
    }

    {
        auto ctx = ThumbnailLoadContext::create(guard, &gen);
        std::string delivered;
        std::string reported_error;
        cache.fetch_for_detail_view(
            nullptr, key, ctx, [&delivered](const std::string& path) { delivered = path; },
            [&reported_error](const std::string& error) { reported_error = error; },
            /*source_modified=*/4102444800); // 2100-01-01, newer than any cached file

        CHECK(delivered.empty());
        CHECK_FALSE(reported_error.empty());
    }

    cache.invalidate(key);
}
