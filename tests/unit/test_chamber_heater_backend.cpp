// tests/unit/test_chamber_heater_backend.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "chamber_heater_backend.h"

#include "../catch_amalgamated.hpp"

using namespace helix::chamber;

TEST_CASE("generic backend keeps keyword tiers", "[chamber][backend]") {
    const auto* generic = backend_by_id("generic");
    REQUIRE(generic != nullptr);
    // Tiers preserved from printer_discovery.h chamber_keyword_confidence
    CHECK(generic->discovery_confidence("chamber") == 100);
    CHECK(generic->discovery_confidence("chamber_heater") == 99); // compound penalty
    CHECK(generic->discovery_confidence("enclosure") == 90);
    CHECK(generic->discovery_confidence("cavity") == 85);
    CHECK(generic->discovery_confidence("box") == 60);
    CHECK(generic->discovery_confidence("heater_box1") ==
          0); // "BOX1" not standalone BOX (AFC dryer)
    CHECK(generic->discovery_confidence("hotend") == 0);
    CHECK(generic->discovery_confidence("chamber_humidity") ==
          59); // 100 -1 compound -40 air-quality
    // Original tokenizer splits ONLY on _/whitespace — hyphen is not a separator.
    CHECK(generic->discovery_confidence("chamber-tvoc") == 99); // no air-quality penalty
    CHECK(generic->discovery_confidence("my-box") == 0);        // BOX not standalone
}

TEST_CASE("registry exposes generic as default", "[chamber][backend]") {
    CHECK(registry().empty() == false);
    CHECK(backend_by_id("generic") == registry().front());
}

TEST_CASE("match dispatches to best backend", "[chamber][backend]") {
    CHECK(match("heater_generic chamber").backend == backend_by_id("generic"));
    CHECK(match("heater_generic chamber").confidence > 0);
    CHECK(match("hotend").backend == nullptr); // nothing claims it
}
