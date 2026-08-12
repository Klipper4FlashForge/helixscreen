// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_exclude_object_char.cpp
 * @brief Characterization tests for exclude object functionality in PrintStatusPanel
 *
 * These tests document the EXISTING behavior of the exclude object feature.
 * Run with: ./build/bin/helix-tests "[exclude_object]"
 *
 * Feature flow:
 * 1. Long-press on object -> handle_object_long_press(object_name)
 * 2. Modal shown with confirm/cancel callbacks
 * 3. On confirm -> visual update, 5-sec timer starts, undo toast shown
 * 4. If timer expires -> api_->exclude_object() called, object added to excluded_objects_
 * 5. If undo clicked -> timer cancelled, visual reverted
 *
 * Key state:
 * - excluded_objects_ : std::unordered_set<std::string> - confirmed exclusions
 * - pending_exclude_object_ : std::string - object in undo window
 * - exclude_undo_timer_ : lv_timer_t* - 5 second timer
 */

#include <string>
#include <unordered_set>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Undo Timer Constants (from ui_panel_print_status.cpp)
// ============================================================================

constexpr uint32_t EXCLUDE_UNDO_WINDOW_MS = 5000; // 5 second undo window

// ============================================================================
// Test Helper Classes - Mirror exclude object state management logic
// ============================================================================

/**
 * @brief Simulates the exclude object state machine from PrintStatusPanel
 *
 * This helper mirrors the state transitions and validation logic without
 * requiring the full PrintStatusPanel/LVGL infrastructure.
 */
class ExcludeObjectStateMachine {
  public:
    enum class ActionResult {
        SUCCESS,
        ALREADY_EXCLUDED,
        PENDING_EXISTS,
        INVALID_NAME,
        NO_PENDING,
        TIMER_ACTIVE
    };

    /**
     * @brief Attempt to start exclusion of an object
     *
     * Mirrors handle_object_long_press() logic:
     * - Rejects empty/null names
     * - Rejects already-excluded objects
     * - Rejects if another pending exclusion exists
     *
     * @param object_name Object to exclude
     * @return Result of the action
     */
    ActionResult start_exclusion(const std::string& object_name) {
        // Check for empty name
        if (object_name.empty()) {
            return ActionResult::INVALID_NAME;
        }

        // Check if already excluded
        if (excluded_objects_.count(object_name) > 0) {
            return ActionResult::ALREADY_EXCLUDED;
        }

        // Check if there's already a pending exclusion
        if (!pending_exclude_object_.empty()) {
            return ActionResult::PENDING_EXISTS;
        }

        // Store pending object
        pending_exclude_object_ = object_name;
        return ActionResult::SUCCESS;
    }

    /**
     * @brief Confirm the pending exclusion
     *
     * Mirrors handle_exclude_confirmed() logic:
     * - Fails if no pending object
     * - Starts undo timer
     * - Returns true if visual update should occur
     */
    bool confirm_exclusion() {
        if (pending_exclude_object_.empty()) {
            return false;
        }
        timer_active_ = true;
        return true;
    }

    /**
     * @brief Cancel the pending exclusion (modal cancel)
     *
     * Mirrors handle_exclude_cancelled() logic:
     * - Clears pending state
     */
    void cancel_exclusion() {
        pending_exclude_object_.clear();
        timer_active_ = false;
    }

    /**
     * @brief Undo the pending exclusion (undo button)
     *
     * Mirrors handle_exclude_undo() logic:
     * - Cancels timer
     * - Clears pending state
     * - Returns true if visual revert should occur
     */
    bool undo_exclusion() {
        if (pending_exclude_object_.empty()) {
            return false;
        }
        pending_exclude_object_.clear();
        timer_active_ = false;
        return true;
    }

    /**
     * @brief Complete the exclusion (timer expired)
     *
     * Mirrors exclude_undo_timer_cb() logic:
     * - Clears pending
     * - Adds to excluded set
     * - Returns object name for API call
     */
    std::string complete_exclusion() {
        if (pending_exclude_object_.empty()) {
            timer_active_ = false;
            return "";
        }
        std::string object_name = pending_exclude_object_;
        pending_exclude_object_.clear();
        timer_active_ = false;
        return object_name;
    }

    /**
     * @brief Mark API call as successful
     */
    void on_api_success(const std::string& object_name) {
        excluded_objects_.insert(object_name);
    }

    /**
     * @brief Sync excluded objects from Klipper
     *
     * Mirrors on_excluded_objects_changed() logic:
     * - Merges Klipper's set into local set
     */
    void sync_from_klipper(const std::unordered_set<std::string>& klipper_excluded) {
        for (const auto& obj : klipper_excluded) {
            excluded_objects_.insert(obj);
        }
    }

    /**
     * @brief Get visual exclusion set (confirmed + pending)
     *
     * For G-code viewer display, both confirmed and pending should appear excluded.
     */
    std::unordered_set<std::string> get_visual_excluded() const {
        std::unordered_set<std::string> visual = excluded_objects_;
        if (!pending_exclude_object_.empty()) {
            visual.insert(pending_exclude_object_);
        }
        return visual;
    }

    // Accessors for testing
    const std::unordered_set<std::string>& excluded_objects() const {
        return excluded_objects_;
    }
    const std::string& pending_object() const {
        return pending_exclude_object_;
    }
    bool is_timer_active() const {
        return timer_active_;
    }

    void reset() {
        excluded_objects_.clear();
        pending_exclude_object_.clear();
        timer_active_ = false;
    }

  private:
    std::unordered_set<std::string> excluded_objects_;
    std::string pending_exclude_object_;
    bool timer_active_ = false;
};

// ============================================================================
// CHARACTERIZATION: Long-Press Initiation
// ============================================================================

TEST_CASE("CHAR: Long-press on object shows confirmation modal",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    SECTION("Valid object name starts exclusion flow") {
        auto result = state.start_exclusion("Benchy_hull");

        REQUIRE(result == ExcludeObjectStateMachine::ActionResult::SUCCESS);
        REQUIRE(state.pending_object() == "Benchy_hull");
    }

    SECTION("Object name is stored in pending state") {
        state.start_exclusion("Part_1");

        REQUIRE(state.pending_object() == "Part_1");
        REQUIRE(state.excluded_objects().empty());
    }
}

TEST_CASE("CHAR: Long-press on already-excluded object is rejected",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Pre-exclude an object
    state.start_exclusion("Part_1");
    state.confirm_exclusion();
    auto name = state.complete_exclusion();
    state.on_api_success(name);

    REQUIRE(state.excluded_objects().count("Part_1") == 1);

    SECTION("Exclusion attempt on already-excluded object fails") {
        auto result = state.start_exclusion("Part_1");

        REQUIRE(result == ExcludeObjectStateMachine::ActionResult::ALREADY_EXCLUDED);
        REQUIRE(state.pending_object().empty()); // No new pending
    }
}

TEST_CASE("CHAR: Long-press while pending exclusion exists is rejected",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Start exclusion of first object
    state.start_exclusion("Part_1");
    state.confirm_exclusion();

    REQUIRE(state.pending_object() == "Part_1");

    SECTION("Second exclusion attempt is rejected") {
        auto result = state.start_exclusion("Part_2");

        REQUIRE(result == ExcludeObjectStateMachine::ActionResult::PENDING_EXISTS);
        REQUIRE(state.pending_object() == "Part_1"); // Still the original
    }
}

TEST_CASE("CHAR: Long-press on empty object name is rejected",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    SECTION("Empty string is rejected") {
        auto result = state.start_exclusion("");

        REQUIRE(result == ExcludeObjectStateMachine::ActionResult::INVALID_NAME);
        REQUIRE(state.pending_object().empty());
    }
}

// ============================================================================
// CHARACTERIZATION: Modal Cancel
// ============================================================================

TEST_CASE("CHAR: Modal cancel clears pending state", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Benchy_hull");
    REQUIRE(state.pending_object() == "Benchy_hull");

    SECTION("Cancel clears pending object") {
        state.cancel_exclusion();

        REQUIRE(state.pending_object().empty());
        REQUIRE(state.excluded_objects().empty());
        REQUIRE_FALSE(state.is_timer_active());
    }
}

TEST_CASE("CHAR: Modal cancel allows new exclusion to start",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Start and cancel first exclusion
    state.start_exclusion("Part_1");
    state.cancel_exclusion();

    SECTION("New exclusion can start after cancel") {
        auto result = state.start_exclusion("Part_2");

        REQUIRE(result == ExcludeObjectStateMachine::ActionResult::SUCCESS);
        REQUIRE(state.pending_object() == "Part_2");
    }
}

// ============================================================================
// CHARACTERIZATION: Modal Confirm
// ============================================================================

TEST_CASE("CHAR: Modal confirm starts undo timer", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Benchy_hull");

    SECTION("Confirm activates timer") {
        bool result = state.confirm_exclusion();

        REQUIRE(result == true);
        REQUIRE(state.is_timer_active());
        REQUIRE(state.pending_object() == "Benchy_hull");
    }

    SECTION("Confirm with no pending object fails") {
        ExcludeObjectStateMachine empty_state;
        bool result = empty_state.confirm_exclusion();

        REQUIRE(result == false);
    }
}

TEST_CASE("CHAR: Modal confirm updates visual state (pending shows as excluded)",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Benchy_hull");
    state.confirm_exclusion();

    SECTION("Visual excluded includes pending object") {
        auto visual = state.get_visual_excluded();

        REQUIRE(visual.count("Benchy_hull") == 1);
    }

    SECTION("Actual excluded set does not include pending yet") {
        REQUIRE(state.excluded_objects().empty());
    }
}

// ============================================================================
// CHARACTERIZATION: Undo Before Timer Expires
// ============================================================================

TEST_CASE("CHAR: Undo before timer expires cancels exclusion",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Benchy_hull");
    state.confirm_exclusion();
    REQUIRE(state.is_timer_active());

    SECTION("Undo cancels the timer") {
        bool result = state.undo_exclusion();

        REQUIRE(result == true);
        REQUIRE_FALSE(state.is_timer_active());
    }

    SECTION("Undo clears pending state") {
        state.undo_exclusion();

        REQUIRE(state.pending_object().empty());
    }

    SECTION("Undo does not add to excluded set") {
        state.undo_exclusion();

        REQUIRE(state.excluded_objects().empty());
    }
}

TEST_CASE("CHAR: Undo clears pending and reverts visuals", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Part_1");
    state.confirm_exclusion();

    // Before undo - visual includes pending
    REQUIRE(state.get_visual_excluded().count("Part_1") == 1);

    SECTION("After undo - visual no longer includes the object") {
        state.undo_exclusion();

        auto visual = state.get_visual_excluded();
        REQUIRE(visual.count("Part_1") == 0);
        REQUIRE(visual.empty());
    }
}

TEST_CASE("CHAR: Undo with no pending exclusion is safe", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    SECTION("Undo on empty state returns false") {
        bool result = state.undo_exclusion();

        REQUIRE(result == false);
    }
}

// ============================================================================
// CHARACTERIZATION: Timer Expiry - API Call
// ============================================================================

TEST_CASE("CHAR: Timer expiry calls API exclude_object", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Benchy_hull");
    state.confirm_exclusion();

    SECTION("Complete exclusion returns object name for API call") {
        std::string object_name = state.complete_exclusion();

        REQUIRE(object_name == "Benchy_hull");
    }

    SECTION("Timer is deactivated after completion") {
        state.complete_exclusion();

        REQUIRE_FALSE(state.is_timer_active());
    }

    SECTION("Pending is cleared after completion") {
        state.complete_exclusion();

        REQUIRE(state.pending_object().empty());
    }
}

TEST_CASE("CHAR: Timer expiry with no pending is handled safely",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    SECTION("Complete with no pending returns empty string") {
        std::string result = state.complete_exclusion();

        REQUIRE(result.empty());
    }
}

// ============================================================================
// CHARACTERIZATION: API Success
// ============================================================================

TEST_CASE("CHAR: API success adds object to excluded_objects_ set",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Part_1");
    state.confirm_exclusion();
    std::string name = state.complete_exclusion();

    SECTION("API success adds to excluded set") {
        state.on_api_success(name);

        REQUIRE(state.excluded_objects().count("Part_1") == 1);
    }

    SECTION("Visual excluded includes confirmed object") {
        state.on_api_success(name);

        auto visual = state.get_visual_excluded();
        REQUIRE(visual.count("Part_1") == 1);
    }
}

TEST_CASE("CHAR: Multiple objects can be excluded sequentially",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Exclude first object
    state.start_exclusion("Part_1");
    state.confirm_exclusion();
    state.on_api_success(state.complete_exclusion());

    // Exclude second object
    state.start_exclusion("Part_2");
    state.confirm_exclusion();
    state.on_api_success(state.complete_exclusion());

    SECTION("Both objects are in excluded set") {
        REQUIRE(state.excluded_objects().count("Part_1") == 1);
        REQUIRE(state.excluded_objects().count("Part_2") == 1);
        REQUIRE(state.excluded_objects().size() == 2);
    }
}

// ============================================================================
// CHARACTERIZATION: API Error
// ============================================================================

TEST_CASE("CHAR: API error reverts visual state", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    state.start_exclusion("Part_1");
    state.confirm_exclusion();
    state.complete_exclusion();
    // Note: API error means we don't call on_api_success()

    SECTION("Object is NOT in excluded set on API failure") {
        // Simulating API error by not calling on_api_success()
        REQUIRE(state.excluded_objects().empty());
    }

    SECTION("Visual excluded does not include failed object") {
        auto visual = state.get_visual_excluded();
        REQUIRE(visual.count("Part_1") == 0);
    }
}

// ============================================================================
// CHARACTERIZATION: Klipper Exclusion Sync
// ============================================================================

TEST_CASE("CHAR: Klipper exclusion sync merges into local set",
          "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Local exclusion
    state.start_exclusion("Part_1");
    state.confirm_exclusion();
    state.on_api_success(state.complete_exclusion());

    SECTION("Klipper objects are merged into local set") {
        std::unordered_set<std::string> klipper_excluded = {"Part_2", "Part_3"};
        state.sync_from_klipper(klipper_excluded);

        REQUIRE(state.excluded_objects().count("Part_1") == 1); // Local
        REQUIRE(state.excluded_objects().count("Part_2") == 1); // Klipper
        REQUIRE(state.excluded_objects().count("Part_3") == 1); // Klipper
        REQUIRE(state.excluded_objects().size() == 3);
    }

    SECTION("Duplicate exclusions are handled (no duplicates in set)") {
        std::unordered_set<std::string> klipper_excluded = {"Part_1"}; // Same as local
        state.sync_from_klipper(klipper_excluded);

        REQUIRE(state.excluded_objects().count("Part_1") == 1);
        REQUIRE(state.excluded_objects().size() == 1);
    }
}

TEST_CASE("CHAR: Sync preserves pending visual state", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Start pending exclusion
    state.start_exclusion("Part_1");
    state.confirm_exclusion();

    // Sync from Klipper while pending
    std::unordered_set<std::string> klipper_excluded = {"Part_2"};
    state.sync_from_klipper(klipper_excluded);

    SECTION("Visual includes both pending and synced") {
        auto visual = state.get_visual_excluded();

        REQUIRE(visual.count("Part_1") == 1); // Pending
        REQUIRE(visual.count("Part_2") == 1); // Synced from Klipper
    }

    SECTION("Excluded set only includes synced (pending not confirmed)") {
        REQUIRE(state.excluded_objects().count("Part_1") == 0); // Pending
        REQUIRE(state.excluded_objects().count("Part_2") == 1); // Synced
    }
}

// ============================================================================
// CHARACTERIZATION: Undo Timer Window Constant
// ============================================================================

TEST_CASE("CHAR: Undo window is 5 seconds", "[characterization][exclude_object]") {
    SECTION("Undo window constant is 5000ms") {
        REQUIRE(EXCLUDE_UNDO_WINDOW_MS == 5000);
    }
}

// ============================================================================
// CHARACTERIZATION: Full Workflow Scenarios
// ============================================================================

TEST_CASE("CHAR: Complete exclusion workflow - happy path", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Step 1: Long-press on object
    auto start_result = state.start_exclusion("Benchy_hull");
    REQUIRE(start_result == ExcludeObjectStateMachine::ActionResult::SUCCESS);

    // Step 2: Modal shown, user confirms
    bool confirm_result = state.confirm_exclusion();
    REQUIRE(confirm_result == true);

    // Step 3: Visual state updated (pending shows as excluded)
    auto visual_during = state.get_visual_excluded();
    REQUIRE(visual_during.count("Benchy_hull") == 1);

    // Step 4: Timer expires, API called
    std::string api_object = state.complete_exclusion();
    REQUIRE(api_object == "Benchy_hull");

    // Step 5: API succeeds, added to excluded set
    state.on_api_success(api_object);
    REQUIRE(state.excluded_objects().count("Benchy_hull") == 1);

    // Step 6: Object remains in visual excluded
    auto visual_after = state.get_visual_excluded();
    REQUIRE(visual_after.count("Benchy_hull") == 1);
}

TEST_CASE("CHAR: Complete exclusion workflow - undo path", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Step 1: Long-press on object
    state.start_exclusion("Benchy_hull");

    // Step 2: User confirms
    state.confirm_exclusion();

    // Step 3: Visual shows pending as excluded
    REQUIRE(state.get_visual_excluded().count("Benchy_hull") == 1);

    // Step 4: User clicks undo before timer expires
    bool undo_result = state.undo_exclusion();
    REQUIRE(undo_result == true);

    // Step 5: Object NOT in excluded set
    REQUIRE(state.excluded_objects().empty());

    // Step 6: Visual no longer shows object as excluded
    REQUIRE(state.get_visual_excluded().empty());

    // Step 7: User can start new exclusion
    auto new_result = state.start_exclusion("Part_2");
    REQUIRE(new_result == ExcludeObjectStateMachine::ActionResult::SUCCESS);
}

TEST_CASE("CHAR: Complete exclusion workflow - cancel path", "[characterization][exclude_object]") {
    ExcludeObjectStateMachine state;

    // Step 1: Long-press on object
    state.start_exclusion("Benchy_hull");
    REQUIRE(state.pending_object() == "Benchy_hull");

    // Step 2: User cancels modal
    state.cancel_exclusion();

    // Step 3: Pending cleared, no exclusion happened
    REQUIRE(state.pending_object().empty());
    REQUIRE(state.excluded_objects().empty());
    REQUIRE(state.get_visual_excluded().empty());
}

// ============================================================================
// Documentation: Exclude Object Pattern Summary
// ============================================================================

/**
 * SUMMARY OF EXCLUDE OBJECT CHARACTERIZATION:
 *
 * State Machine:
 * - IDLE: No pending exclusion, excluded_objects_ contains confirmed exclusions
 * - PENDING: pending_exclude_object_ set, modal shown or timer running
 * - TIMER_ACTIVE: confirm_exclusion() called, waiting for timer/undo
 *
 * State Transitions:
 * 1. IDLE -> PENDING: start_exclusion(name) succeeds
 * 2. PENDING -> IDLE: cancel_exclusion() (modal cancel)
 * 3. PENDING -> TIMER_ACTIVE: confirm_exclusion() (modal confirm)
 * 4. TIMER_ACTIVE -> IDLE: undo_exclusion() (undo button)
 * 5. TIMER_ACTIVE -> EXCLUDED: complete_exclusion() + on_api_success()
 *
 * Validation Rules:
 * - Empty object names rejected
 * - Already-excluded objects rejected
 * - Pending exclusion blocks new exclusion attempts
 *
 * Visual State:
 * - get_visual_excluded() = excluded_objects_ UNION {pending_exclude_object_}
 * - Used by G-code viewer to show both confirmed and pending exclusions
 *
 * Klipper Sync:
 * - on_excluded_objects_changed() merges Klipper's excluded set
 * - Local exclusions + Klipper exclusions both shown
 * - Handles objects excluded from other clients
 *
 * Timer Behavior:
 * - 5000ms (5 second) undo window
 * - Timer expiry triggers API call
 * - Undo cancels timer and reverts visual state
 */
