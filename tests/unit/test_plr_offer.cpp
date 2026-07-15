// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../include/plr_offer.h"

#include "moonraker_client.h" // ConnectionState

#include "../catch_amalgamated.hpp"

using helix::ConnectionState;
using helix::PlrOfferSignals;
using helix::plr_should_offer;
using helix::plr_should_rearm;

namespace {

// All-true baseline; individual tests flip one field at a time.
PlrOfferSignals all_true_signals() {
    PlrOfferSignals s;
    s.pl_env_valid = true;
    s.printer_idle = true;
    s.is_snapmaker = true;
    s.already_prompted = false;
    s.wizard_active = false;
    return s;
}

} // namespace

TEST_CASE("plr_should_offer: all signals true => offer", "[plr][offer]") {
    REQUIRE(plr_should_offer(all_true_signals()) == true);
}

TEST_CASE("plr_should_offer: pl_env_valid false => no offer", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.pl_env_valid = false;
    REQUIRE(plr_should_offer(s) == false);
}

TEST_CASE("plr_should_offer: printer not idle => no offer", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.printer_idle = false;
    REQUIRE(plr_should_offer(s) == false);
}

TEST_CASE("plr_should_offer: non-Snapmaker printer never offers", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.is_snapmaker = false;
    REQUIRE(plr_should_offer(s) == false);
}

TEST_CASE("plr_should_offer: already_prompted latch blocks re-offer", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.already_prompted = true;
    REQUIRE(plr_should_offer(s) == false);
}

TEST_CASE("plr_should_offer: wizard active blocks offer", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.wizard_active = true;
    REQUIRE(plr_should_offer(s) == false);
}

TEST_CASE("plr_should_offer: all good with wizard inactive still offers", "[plr][offer]") {
    PlrOfferSignals s = all_true_signals();
    s.wizard_active = false;
    REQUIRE(plr_should_offer(s) == true);
}

TEST_CASE("plr_should_rearm: CONNECTED -> DISCONNECTED re-arms", "[plr][offer][rearm]") {
    REQUIRE(plr_should_rearm(static_cast<int>(ConnectionState::CONNECTED),
                             static_cast<int>(ConnectionState::DISCONNECTED)) == true);
}

TEST_CASE("plr_should_rearm: CONNECTED -> RECONNECTING re-arms", "[plr][offer][rearm]") {
    REQUIRE(plr_should_rearm(static_cast<int>(ConnectionState::CONNECTED),
                             static_cast<int>(ConnectionState::RECONNECTING)) == true);
}

TEST_CASE("plr_should_rearm: DISCONNECTED -> CONNECTED does not re-arm", "[plr][offer][rearm]") {
    REQUIRE(plr_should_rearm(static_cast<int>(ConnectionState::DISCONNECTED),
                             static_cast<int>(ConnectionState::CONNECTED)) == false);
}

TEST_CASE("plr_should_rearm: CONNECTED -> CONNECTED does not re-arm", "[plr][offer][rearm]") {
    REQUIRE(plr_should_rearm(static_cast<int>(ConnectionState::CONNECTED),
                             static_cast<int>(ConnectionState::CONNECTED)) == false);
}
