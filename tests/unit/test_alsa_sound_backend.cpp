// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_HAS_ALSA

#include "alsa_sound_backend.h"

#include <cstdint>
#include <vector>

#include "../catch_amalgamated.hpp"

TEST_CASE("ALSASoundBackend::mono_to_stereo interleaves L=R", "[sound][alsa]") {
    std::vector<float> mono = {0.1f, 0.5f, -0.3f, 1.0f};
    std::vector<float> stereo(mono.size() * 2);

    ALSASoundBackend::mono_to_stereo(mono.data(), stereo.data(), mono.size());

    REQUIRE(stereo[0] == Catch::Approx(0.1f));
    REQUIRE(stereo[1] == Catch::Approx(0.1f));
    REQUIRE(stereo[2] == Catch::Approx(0.5f));
    REQUIRE(stereo[3] == Catch::Approx(0.5f));
    REQUIRE(stereo[4] == Catch::Approx(-0.3f));
    REQUIRE(stereo[5] == Catch::Approx(-0.3f));
    REQUIRE(stereo[6] == Catch::Approx(1.0f));
    REQUIRE(stereo[7] == Catch::Approx(1.0f));
}

TEST_CASE("ALSASoundBackend::mono_to_stereo empty input produces empty output", "[sound][alsa]") {
    std::vector<float> stereo(16, 0.0f);

    ALSASoundBackend::mono_to_stereo(nullptr, stereo.data(), 0);

    // Output buffer should be untouched (0 frames written)
    for (auto v : stereo) {
        REQUIRE(v == 0.0f);
    }
}

TEST_CASE("ALSASoundBackend::float_to_s16 normal range", "[sound][alsa]") {
    std::vector<float> src = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    std::vector<int16_t> dst(src.size());

    ALSASoundBackend::float_to_s16(src.data(), dst.data(), src.size());

    REQUIRE(dst[0] == 0);
    REQUIRE(dst[1] >= 16381);
    REQUIRE(dst[1] <= 16385);
    REQUIRE(dst[2] <= -16381);
    REQUIRE(dst[2] >= -16385);
    REQUIRE(dst[3] == 32767);
    REQUIRE(dst[4] == -32767);
}

TEST_CASE("ALSASoundBackend::float_to_s16 boundary values", "[sound][alsa]") {
    std::vector<float> src = {1.0f, -1.0f, 0.0f};
    std::vector<int16_t> dst(src.size());

    ALSASoundBackend::float_to_s16(src.data(), dst.data(), src.size());

    REQUIRE(dst[0] == 32767);
    REQUIRE(dst[1] == -32767);
    REQUIRE(dst[2] == 0);
}

TEST_CASE("ALSASoundBackend::float_to_s16 clamps out-of-range values", "[sound][alsa]") {
    std::vector<float> src = {1.5f, -1.5f, 2.0f, -100.0f};
    std::vector<int16_t> dst(src.size());

    ALSASoundBackend::float_to_s16(src.data(), dst.data(), src.size());

    REQUIRE(dst[0] == 32767);
    REQUIRE(dst[1] == -32767);
    REQUIRE(dst[2] == 32767);
    REQUIRE(dst[3] == -32767);
}

TEST_CASE("ALSASoundBackend::float_to_s16 silence stays silent", "[sound][alsa]") {
    std::vector<float> src(8, 0.0f);
    std::vector<int16_t> dst(src.size());

    ALSASoundBackend::float_to_s16(src.data(), dst.data(), src.size());

    for (auto v : dst) {
        REQUIRE(v == 0);
    }
}

#endif // HELIX_HAS_ALSA
