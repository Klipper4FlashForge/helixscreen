// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_settings_machine_limits_char.cpp
 * @brief Characterization tests for Machine Limits functionality in SettingsPanel
 *
 * These tests document the EXISTING behavior of the Machine Limits overlay feature.
 * Run with: ./build/bin/helix-tests "[machine_limits]"
 *
 * Feature flow:
 * 1. Click Machine Limits -> handle_machine_limits_clicked() queries API
 * 2. API returns MachineLimits -> current_limits_ and original_limits_ set
 * 3. Sliders update to show current values, display subjects updated
 * 4. User adjusts sliders -> handle_max_velocity_changed(int), etc.
 * 5. Reset button -> restores original_limits_ to current_limits_
 * 6. Apply button -> sends SET_VELOCITY_LIMIT gcode via API
 *
 * Key state:
 * - current_limits_ : MachineLimits - live slider values
 * - original_limits_ : MachineLimits - values when overlay opened, for reset
 * - 4 display subjects for binding slider labels
 *
 * Limits managed:
 * - max_velocity: Maximum velocity in mm/s
 * - max_accel: Maximum acceleration in mm/s²
 * - max_accel_to_decel: Acceleration to deceleration in mm/s²
 * - square_corner_velocity: Square corner velocity in mm/s
 */

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "../catch_amalgamated.hpp"

// ============================================================================
// MachineLimits Struct (mirrors calibration_types.h)
// ============================================================================

/**
 * @brief Mirror of MachineLimits struct for testing without full includes
 */
struct TestMachineLimits {
    double max_velocity = 0;           ///< Maximum velocity in mm/s
    double max_accel = 0;              ///< Maximum acceleration in mm/s²
    double max_accel_to_decel = 0;     ///< Acceleration to deceleration in mm/s²
    double square_corner_velocity = 0; ///< Square corner velocity in mm/s
    double max_z_velocity = 0;         ///< Maximum Z velocity (read-only)
    double max_z_accel = 0;            ///< Maximum Z acceleration (read-only)

    [[nodiscard]] bool is_valid() const {
        return max_velocity > 0 && max_accel > 0;
    }

    [[nodiscard]] bool operator==(const TestMachineLimits& other) const {
        return max_velocity == other.max_velocity && max_accel == other.max_accel &&
               max_accel_to_decel == other.max_accel_to_decel &&
               square_corner_velocity == other.square_corner_velocity &&
               max_z_velocity == other.max_z_velocity && max_z_accel == other.max_z_accel;
    }

    [[nodiscard]] bool operator!=(const TestMachineLimits& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// Test Helpers: Display Formatting (mirrors ui_panel_settings.cpp)
// ============================================================================

/**
 * @brief Format velocity display string
 *
 * Mirrors the logic in SettingsPanel::update_limits_display() and
 * handle_max_velocity_changed()
 *
 * @param value Velocity value in mm/s
 * @return Formatted string like "500 mm/s"
 */
static std::string format_velocity_display(double value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f mm/s", value);
    return std::string(buf);
}

/**
 * @brief Format velocity display string from integer
 *
 * Mirrors the logic in handle_max_velocity_changed(int value)
 *
 * @param value Velocity value in mm/s (integer from slider)
 * @return Formatted string like "500 mm/s"
 */
static std::string format_velocity_display_int(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d mm/s", value);
    return std::string(buf);
}

/**
 * @brief Format acceleration display string
 *
 * Mirrors the logic in SettingsPanel::update_limits_display() and
 * handle_max_accel_changed()
 *
 * @param value Acceleration value in mm/s²
 * @return Formatted string like "3000 mm/s²"
 */
static std::string format_accel_display(double value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f mm/s²", value);
    return std::string(buf);
}

/**
 * @brief Format acceleration display string from integer
 *
 * Mirrors the logic in handle_max_accel_changed(int value)
 *
 * @param value Acceleration value in mm/s² (integer from slider)
 * @return Formatted string like "3000 mm/s²"
 */
static std::string format_accel_display_int(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d mm/s²", value);
    return std::string(buf);
}

/**
 * @brief Format SET_VELOCITY_LIMIT gcode command
 *
 * Mirrors the logic in MoonrakerAPI::set_machine_limits()
 *
 * @param limits MachineLimits struct with values to set
 * @return G-code string like "SET_VELOCITY_LIMIT VELOCITY=500.0 ACCEL=3000.0 ..."
 */
static std::string format_velocity_limit_gcode(const TestMachineLimits& limits) {
    std::ostringstream cmd;
    cmd << std::fixed;
    cmd.precision(1);
    cmd << "SET_VELOCITY_LIMIT";

    bool has_params = false;

    if (limits.max_velocity > 0) {
        cmd << " VELOCITY=" << limits.max_velocity;
        has_params = true;
    }
    if (limits.max_accel > 0) {
        cmd << " ACCEL=" << limits.max_accel;
        has_params = true;
    }
    if (limits.max_accel_to_decel > 0) {
        cmd << " ACCEL_TO_DECEL=" << limits.max_accel_to_decel;
        has_params = true;
    }
    if (limits.square_corner_velocity > 0) {
        cmd << " SQUARE_CORNER_VELOCITY=" << limits.square_corner_velocity;
        has_params = true;
    }

    if (!has_params) {
        return ""; // No valid parameters
    }

    return cmd.str();
}

// ============================================================================
// State Machine Helper: Simulates Machine Limits overlay behavior
// ============================================================================

/**
 * @brief Simulates the Machine Limits state management from SettingsPanel
 *
 * This helper mirrors the state transitions and logic without requiring
 * the full SettingsPanel/LVGL infrastructure.
 */
class MachineLimitsStateMachine {
  public:
    /**
     * @brief Open overlay with limits from API
     *
     * Mirrors handle_machine_limits_clicked() success callback:
     * - Sets both current and original limits from API response
     */
    void open_with_limits(const TestMachineLimits& limits) {
        current_limits_ = limits;
        original_limits_ = limits;
        overlay_open_ = true;
    }

    /**
     * @brief Handle max velocity slider change
     *
     * Mirrors handle_max_velocity_changed(int value)
     */
    void set_max_velocity(int value) {
        current_limits_.max_velocity = static_cast<double>(value);
    }

    /**
     * @brief Handle max acceleration slider change
     *
     * Mirrors handle_max_accel_changed(int value)
     */
    void set_max_accel(int value) {
        current_limits_.max_accel = static_cast<double>(value);
    }

    /**
     * @brief Handle accel to decel slider change
     *
     * Mirrors handle_accel_to_decel_changed(int value)
     */
    void set_accel_to_decel(int value) {
        current_limits_.max_accel_to_decel = static_cast<double>(value);
    }

    /**
     * @brief Handle square corner velocity slider change
     *
     * Mirrors handle_square_corner_velocity_changed(int value)
     */
    void set_square_corner_velocity(int value) {
        current_limits_.square_corner_velocity = static_cast<double>(value);
    }

    /**
     * @brief Handle reset button click
     *
     * Mirrors handle_limits_reset():
     * - Restores current_limits_ from original_limits_
     */
    void reset() {
        current_limits_ = original_limits_;
    }

    /**
     * @brief Handle apply button click (success path)
     *
     * Mirrors handle_limits_apply() success callback:
     * - Updates original_limits_ to current_limits_ (prevents reset from reverting)
     */
    void apply_success() {
        original_limits_ = current_limits_;
    }

    /**
     * @brief Check if limits have been modified
     */
    [[nodiscard]] bool has_changes() const {
        return current_limits_ != original_limits_;
    }

    // Accessors
    [[nodiscard]] const TestMachineLimits& current_limits() const {
        return current_limits_;
    }
    [[nodiscard]] const TestMachineLimits& original_limits() const {
        return original_limits_;
    }
    [[nodiscard]] bool is_overlay_open() const {
        return overlay_open_;
    }

    /**
     * @brief Get formatted display strings for all 4 values
     */
    [[nodiscard]] std::string velocity_display() const {
        return format_velocity_display(current_limits_.max_velocity);
    }
    [[nodiscard]] std::string accel_display() const {
        return format_accel_display(current_limits_.max_accel);
    }
    [[nodiscard]] std::string accel_to_decel_display() const {
        return format_accel_display(current_limits_.max_accel_to_decel);
    }
    [[nodiscard]] std::string scv_display() const {
        return format_velocity_display(current_limits_.square_corner_velocity);
    }

  private:
    TestMachineLimits current_limits_;
    TestMachineLimits original_limits_;
    bool overlay_open_ = false;
};

// ============================================================================
// CHARACTERIZATION: Display Format Tests
// ============================================================================

TEST_CASE("CHAR: Velocity displays as 'X mm/s'", "[characterization][settings][machine_limits]") {
    SECTION("Typical velocity value") {
        std::string display = format_velocity_display(500.0);
        REQUIRE(display == "500 mm/s");
    }

    SECTION("High velocity value") {
        std::string display = format_velocity_display(1000.0);
        REQUIRE(display == "1000 mm/s");
    }

    SECTION("Low velocity value") {
        std::string display = format_velocity_display(100.0);
        REQUIRE(display == "100 mm/s");
    }

    SECTION("Integer slider value formatting") {
        std::string display = format_velocity_display_int(300);
        REQUIRE(display == "300 mm/s");
    }
}

TEST_CASE("CHAR: Acceleration displays as 'X mm/s²'",
          "[characterization][settings][machine_limits]") {
    SECTION("Typical acceleration value") {
        std::string display = format_accel_display(3000.0);
        REQUIRE(display == "3000 mm/s²");
    }

    SECTION("High acceleration value") {
        std::string display = format_accel_display(10000.0);
        REQUIRE(display == "10000 mm/s²");
    }

    SECTION("Low acceleration value") {
        std::string display = format_accel_display(500.0);
        REQUIRE(display == "500 mm/s²");
    }

    SECTION("Integer slider value formatting") {
        std::string display = format_accel_display_int(5000);
        REQUIRE(display == "5000 mm/s²");
    }
}

TEST_CASE("CHAR: Display updates when slider changes",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    SECTION("Velocity display updates after slider change") {
        REQUIRE(state.velocity_display() == "500 mm/s");

        state.set_max_velocity(600);

        REQUIRE(state.velocity_display() == "600 mm/s");
    }

    SECTION("Acceleration display updates after slider change") {
        REQUIRE(state.accel_display() == "3000 mm/s²");

        state.set_max_accel(4000);

        REQUIRE(state.accel_display() == "4000 mm/s²");
    }

    SECTION("Accel-to-decel display updates after slider change") {
        REQUIRE(state.accel_to_decel_display() == "1500 mm/s²");

        state.set_accel_to_decel(2000);

        REQUIRE(state.accel_to_decel_display() == "2000 mm/s²");
    }

    SECTION("Square corner velocity display updates after slider change") {
        REQUIRE(state.scv_display() == "5 mm/s");

        state.set_square_corner_velocity(8);

        REQUIRE(state.scv_display() == "8 mm/s");
    }
}

// ============================================================================
// CHARACTERIZATION: Dual State Tracking
// ============================================================================

TEST_CASE("CHAR: Current limits updated on slider change",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    SECTION("Max velocity slider updates current_limits_") {
        state.set_max_velocity(750);

        REQUIRE(state.current_limits().max_velocity == Catch::Approx(750.0));
        REQUIRE(state.has_changes());
    }

    SECTION("Max accel slider updates current_limits_") {
        state.set_max_accel(5000);

        REQUIRE(state.current_limits().max_accel == Catch::Approx(5000.0));
        REQUIRE(state.has_changes());
    }

    SECTION("Accel to decel slider updates current_limits_") {
        state.set_accel_to_decel(2500);

        REQUIRE(state.current_limits().max_accel_to_decel == Catch::Approx(2500.0));
        REQUIRE(state.has_changes());
    }

    SECTION("Square corner velocity slider updates current_limits_") {
        state.set_square_corner_velocity(10);

        REQUIRE(state.current_limits().square_corner_velocity == Catch::Approx(10.0));
        REQUIRE(state.has_changes());
    }
}

TEST_CASE("CHAR: Original limits preserved until overlay closes",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    SECTION("Slider changes do not affect original_limits_") {
        state.set_max_velocity(750);
        state.set_max_accel(5000);
        state.set_accel_to_decel(2500);
        state.set_square_corner_velocity(10);

        // Current should have changed
        REQUIRE(state.current_limits().max_velocity == Catch::Approx(750.0));

        // Original should be unchanged
        REQUIRE(state.original_limits().max_velocity == Catch::Approx(500.0));
        REQUIRE(state.original_limits().max_accel == Catch::Approx(3000.0));
        REQUIRE(state.original_limits().max_accel_to_decel == Catch::Approx(1500.0));
        REQUIRE(state.original_limits().square_corner_velocity == Catch::Approx(5.0));
    }
}

TEST_CASE("CHAR: Reset restores current from original",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    // Modify all values
    state.set_max_velocity(750);
    state.set_max_accel(5000);
    state.set_accel_to_decel(2500);
    state.set_square_corner_velocity(10);

    REQUIRE(state.has_changes());

    SECTION("Reset restores all 4 values to original") {
        state.reset();

        REQUIRE(state.current_limits().max_velocity == Catch::Approx(500.0));
        REQUIRE(state.current_limits().max_accel == Catch::Approx(3000.0));
        REQUIRE(state.current_limits().max_accel_to_decel == Catch::Approx(1500.0));
        REQUIRE(state.current_limits().square_corner_velocity == Catch::Approx(5.0));
        REQUIRE_FALSE(state.has_changes());
    }

    SECTION("Display values update after reset") {
        state.reset();

        REQUIRE(state.velocity_display() == "500 mm/s");
        REQUIRE(state.accel_display() == "3000 mm/s²");
        REQUIRE(state.accel_to_decel_display() == "1500 mm/s²");
        REQUIRE(state.scv_display() == "5 mm/s");
    }
}

// ============================================================================
// CHARACTERIZATION: Apply Behavior
// ============================================================================

TEST_CASE("CHAR: Apply sends SET_VELOCITY_LIMIT gcode",
          "[characterization][settings][machine_limits]") {
    SECTION("All 4 parameters included when non-zero") {
        TestMachineLimits limits = {.max_velocity = 500,
                                    .max_accel = 3000,
                                    .max_accel_to_decel = 1500,
                                    .square_corner_velocity = 5};

        std::string gcode = format_velocity_limit_gcode(limits);

        REQUIRE(gcode.find("SET_VELOCITY_LIMIT") != std::string::npos);
        REQUIRE(gcode.find("VELOCITY=500.0") != std::string::npos);
        REQUIRE(gcode.find("ACCEL=3000.0") != std::string::npos);
        REQUIRE(gcode.find("ACCEL_TO_DECEL=1500.0") != std::string::npos);
        REQUIRE(gcode.find("SQUARE_CORNER_VELOCITY=5.0") != std::string::npos);
    }

    SECTION("Parameters with fixed precision (1 decimal)") {
        TestMachineLimits limits = {.max_velocity = 500.5,
                                    .max_accel = 3000.5,
                                    .max_accel_to_decel = 1500.5,
                                    .square_corner_velocity = 5.5};

        std::string gcode = format_velocity_limit_gcode(limits);

        REQUIRE(gcode.find("VELOCITY=500.5") != std::string::npos);
        REQUIRE(gcode.find("ACCEL=3000.5") != std::string::npos);
        REQUIRE(gcode.find("ACCEL_TO_DECEL=1500.5") != std::string::npos);
        REQUIRE(gcode.find("SQUARE_CORNER_VELOCITY=5.5") != std::string::npos);
    }

    SECTION("Zero values are omitted from gcode") {
        TestMachineLimits limits = {.max_velocity = 500,
                                    .max_accel = 3000,
                                    .max_accel_to_decel = 0,
                                    .square_corner_velocity = 0};

        std::string gcode = format_velocity_limit_gcode(limits);

        REQUIRE(gcode.find("VELOCITY=500.0") != std::string::npos);
        REQUIRE(gcode.find("ACCEL=3000.0") != std::string::npos);
        REQUIRE(gcode.find("ACCEL_TO_DECEL") == std::string::npos);
        REQUIRE(gcode.find("SQUARE_CORNER_VELOCITY") == std::string::npos);
    }

    SECTION("All zero returns empty string") {
        TestMachineLimits limits = {.max_velocity = 0,
                                    .max_accel = 0,
                                    .max_accel_to_decel = 0,
                                    .square_corner_velocity = 0};

        std::string gcode = format_velocity_limit_gcode(limits);

        REQUIRE(gcode.empty());
    }
}

TEST_CASE("CHAR: Apply success updates original_limits_",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    // Modify values
    state.set_max_velocity(750);
    state.set_max_accel(5000);
    REQUIRE(state.has_changes());

    SECTION("After apply success, original matches current") {
        state.apply_success();

        REQUIRE(state.original_limits().max_velocity == Catch::Approx(750.0));
        REQUIRE(state.original_limits().max_accel == Catch::Approx(5000.0));
        REQUIRE_FALSE(state.has_changes());
    }

    SECTION("Reset after apply keeps new values") {
        state.apply_success();
        state.reset();

        // Reset should restore to the new "original" (which is now the applied values)
        REQUIRE(state.current_limits().max_velocity == Catch::Approx(750.0));
        REQUIRE(state.current_limits().max_accel == Catch::Approx(5000.0));
    }
}

// ============================================================================
// CHARACTERIZATION: Reset Behavior
// ============================================================================

TEST_CASE("CHAR: Reset restores all 4 values", "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    // Change all values
    state.set_max_velocity(999);
    state.set_max_accel(9999);
    state.set_accel_to_decel(4999);
    state.set_square_corner_velocity(99);

    // Verify changes
    REQUIRE(state.current_limits().max_velocity == Catch::Approx(999.0));
    REQUIRE(state.current_limits().max_accel == Catch::Approx(9999.0));
    REQUIRE(state.current_limits().max_accel_to_decel == Catch::Approx(4999.0));
    REQUIRE(state.current_limits().square_corner_velocity == Catch::Approx(99.0));

    // Reset
    state.reset();

    // All values should be restored
    REQUIRE(state.current_limits().max_velocity == Catch::Approx(500.0));
    REQUIRE(state.current_limits().max_accel == Catch::Approx(3000.0));
    REQUIRE(state.current_limits().max_accel_to_decel == Catch::Approx(1500.0));
    REQUIRE(state.current_limits().square_corner_velocity == Catch::Approx(5.0));
}

TEST_CASE("CHAR: Reset is idempotent", "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500,
                                 .max_accel = 3000,
                                 .max_accel_to_decel = 1500,
                                 .square_corner_velocity = 5};
    state.open_with_limits(initial);

    // Change and reset multiple times
    state.set_max_velocity(999);
    state.reset();
    state.reset();
    state.reset();

    REQUIRE(state.current_limits().max_velocity == Catch::Approx(500.0));
    REQUIRE_FALSE(state.has_changes());
}

// ============================================================================
// CHARACTERIZATION: MachineLimits Struct Validation
// ============================================================================

TEST_CASE("CHAR: MachineLimits is_valid() behavior",
          "[characterization][settings][machine_limits]") {
    SECTION("Valid when max_velocity and max_accel are positive") {
        TestMachineLimits limits = {.max_velocity = 500, .max_accel = 3000};

        REQUIRE(limits.is_valid());
    }

    SECTION("Invalid when max_velocity is zero") {
        TestMachineLimits limits = {.max_velocity = 0, .max_accel = 3000};

        REQUIRE_FALSE(limits.is_valid());
    }

    SECTION("Invalid when max_accel is zero") {
        TestMachineLimits limits = {.max_velocity = 500, .max_accel = 0};

        REQUIRE_FALSE(limits.is_valid());
    }

    SECTION("Invalid when both are zero") {
        TestMachineLimits limits = {};

        REQUIRE_FALSE(limits.is_valid());
    }

    SECTION("Other fields don't affect validity") {
        TestMachineLimits limits = {.max_velocity = 500,
                                    .max_accel = 3000,
                                    .max_accel_to_decel = 0, // Zero is OK
                                    .square_corner_velocity = 0};

        REQUIRE(limits.is_valid());
    }
}

TEST_CASE("CHAR: MachineLimits equality comparison",
          "[characterization][settings][machine_limits]") {
    SECTION("Equal limits compare equal") {
        TestMachineLimits a = {.max_velocity = 500,
                               .max_accel = 3000,
                               .max_accel_to_decel = 1500,
                               .square_corner_velocity = 5};
        TestMachineLimits b = {.max_velocity = 500,
                               .max_accel = 3000,
                               .max_accel_to_decel = 1500,
                               .square_corner_velocity = 5};

        REQUIRE(a == b);
        REQUIRE_FALSE(a != b);
    }

    SECTION("Different velocity compares not equal") {
        TestMachineLimits a = {.max_velocity = 500};
        TestMachineLimits b = {.max_velocity = 600};

        REQUIRE(a != b);
        REQUIRE_FALSE(a == b);
    }

    SECTION("Different accel compares not equal") {
        TestMachineLimits a = {.max_accel = 3000};
        TestMachineLimits b = {.max_accel = 4000};

        REQUIRE(a != b);
    }

    SECTION("Z limits included in comparison") {
        TestMachineLimits a = {.max_velocity = 500, .max_accel = 3000, .max_z_velocity = 10};
        TestMachineLimits b = {.max_velocity = 500, .max_accel = 3000, .max_z_velocity = 15};

        REQUIRE(a != b);
    }
}

// ============================================================================
// CHARACTERIZATION: Slider Value Ranges (typical printer values)
// ============================================================================

TEST_CASE("CHAR: Typical slider value ranges", "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    SECTION("Max velocity range - typical values 50-1000 mm/s") {
        TestMachineLimits initial = {.max_velocity = 500,
                                     .max_accel = 3000,
                                     .max_accel_to_decel = 1500,
                                     .square_corner_velocity = 5};
        state.open_with_limits(initial);

        // Low end
        state.set_max_velocity(50);
        REQUIRE(state.velocity_display() == "50 mm/s");

        // Mid range
        state.set_max_velocity(300);
        REQUIRE(state.velocity_display() == "300 mm/s");

        // High end
        state.set_max_velocity(1000);
        REQUIRE(state.velocity_display() == "1000 mm/s");
    }

    SECTION("Max accel range - typical values 500-20000 mm/s²") {
        TestMachineLimits initial = {.max_velocity = 500,
                                     .max_accel = 3000,
                                     .max_accel_to_decel = 1500,
                                     .square_corner_velocity = 5};
        state.open_with_limits(initial);

        // Low end
        state.set_max_accel(500);
        REQUIRE(state.accel_display() == "500 mm/s²");

        // Mid range
        state.set_max_accel(5000);
        REQUIRE(state.accel_display() == "5000 mm/s²");

        // High end (high-speed printers)
        state.set_max_accel(20000);
        REQUIRE(state.accel_display() == "20000 mm/s²");
    }

    SECTION("Accel to decel range - typically <= max_accel") {
        TestMachineLimits initial = {.max_velocity = 500,
                                     .max_accel = 3000,
                                     .max_accel_to_decel = 1500,
                                     .square_corner_velocity = 5};
        state.open_with_limits(initial);

        // Typical: half of max_accel
        state.set_accel_to_decel(1500);
        REQUIRE(state.accel_to_decel_display() == "1500 mm/s²");

        // Can be equal to max_accel
        state.set_accel_to_decel(3000);
        REQUIRE(state.accel_to_decel_display() == "3000 mm/s²");
    }

    SECTION("Square corner velocity range - typical values 1-20 mm/s") {
        TestMachineLimits initial = {.max_velocity = 500,
                                     .max_accel = 3000,
                                     .max_accel_to_decel = 1500,
                                     .square_corner_velocity = 5};
        state.open_with_limits(initial);

        // Low end (more conservative)
        state.set_square_corner_velocity(1);
        REQUIRE(state.scv_display() == "1 mm/s");

        // Typical
        state.set_square_corner_velocity(5);
        REQUIRE(state.scv_display() == "5 mm/s");

        // High end (aggressive)
        state.set_square_corner_velocity(20);
        REQUIRE(state.scv_display() == "20 mm/s");
    }
}

// ============================================================================
// CHARACTERIZATION: Edge Cases
// ============================================================================

TEST_CASE("CHAR: Edge case - minimum values handled",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {
        .max_velocity = 1, .max_accel = 1, .max_accel_to_decel = 1, .square_corner_velocity = 1};
    state.open_with_limits(initial);

    REQUIRE(state.velocity_display() == "1 mm/s");
    REQUIRE(state.accel_display() == "1 mm/s²");
    REQUIRE(state.accel_to_decel_display() == "1 mm/s²");
    REQUIRE(state.scv_display() == "1 mm/s");
}

TEST_CASE("CHAR: Edge case - maximum values handled",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    // Very high values (theoretical maximums for high-speed printers)
    TestMachineLimits initial = {.max_velocity = 5000,
                                 .max_accel = 50000,
                                 .max_accel_to_decel = 25000,
                                 .square_corner_velocity = 50};
    state.open_with_limits(initial);

    REQUIRE(state.velocity_display() == "5000 mm/s");
    REQUIRE(state.accel_display() == "50000 mm/s²");
    REQUIRE(state.accel_to_decel_display() == "25000 mm/s²");
    REQUIRE(state.scv_display() == "50 mm/s");
}

TEST_CASE("CHAR: Edge case - slider converts to integer",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    TestMachineLimits initial = {.max_velocity = 500.7, // API might return floats
                                 .max_accel = 3000.3,
                                 .max_accel_to_decel = 1500.9,
                                 .square_corner_velocity = 5.5};
    state.open_with_limits(initial);

    // Display uses %.0f format - truncates to integer display
    REQUIRE(state.velocity_display() == "501 mm/s"); // Rounds
    REQUIRE(state.accel_display() == "3000 mm/s²");  // Rounds
}

// ============================================================================
// CHARACTERIZATION: Full Workflow Scenarios
// ============================================================================

TEST_CASE("CHAR: Complete workflow - modify and apply",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    // Step 1: API returns current limits
    TestMachineLimits from_api = {.max_velocity = 500,
                                  .max_accel = 3000,
                                  .max_accel_to_decel = 1500,
                                  .square_corner_velocity = 5};
    state.open_with_limits(from_api);

    REQUIRE(state.is_overlay_open());
    REQUIRE_FALSE(state.has_changes());

    // Step 2: User adjusts sliders
    state.set_max_velocity(600);
    state.set_max_accel(4000);

    REQUIRE(state.has_changes());
    REQUIRE(state.velocity_display() == "600 mm/s");
    REQUIRE(state.accel_display() == "4000 mm/s²");

    // Step 3: User clicks Apply
    std::string gcode = format_velocity_limit_gcode(state.current_limits());
    REQUIRE(gcode.find("VELOCITY=600.0") != std::string::npos);
    REQUIRE(gcode.find("ACCEL=4000.0") != std::string::npos);

    // Step 4: API success callback
    state.apply_success();

    REQUIRE_FALSE(state.has_changes());
    REQUIRE(state.original_limits().max_velocity == Catch::Approx(600.0));
}

TEST_CASE("CHAR: Complete workflow - modify and reset",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    // Step 1: API returns current limits
    TestMachineLimits from_api = {.max_velocity = 500,
                                  .max_accel = 3000,
                                  .max_accel_to_decel = 1500,
                                  .square_corner_velocity = 5};
    state.open_with_limits(from_api);

    // Step 2: User adjusts sliders
    state.set_max_velocity(999);
    state.set_max_accel(9999);
    state.set_accel_to_decel(4999);
    state.set_square_corner_velocity(99);

    REQUIRE(state.has_changes());

    // Step 3: User clicks Reset
    state.reset();

    // Step 4: All values restored
    REQUIRE_FALSE(state.has_changes());
    REQUIRE(state.velocity_display() == "500 mm/s");
    REQUIRE(state.accel_display() == "3000 mm/s²");
    REQUIRE(state.accel_to_decel_display() == "1500 mm/s²");
    REQUIRE(state.scv_display() == "5 mm/s");
}

TEST_CASE("CHAR: Complete workflow - apply then modify again",
          "[characterization][settings][machine_limits]") {
    MachineLimitsStateMachine state;

    // Step 1: Open with initial values
    TestMachineLimits from_api = {.max_velocity = 500,
                                  .max_accel = 3000,
                                  .max_accel_to_decel = 1500,
                                  .square_corner_velocity = 5};
    state.open_with_limits(from_api);

    // Step 2: First modification and apply
    state.set_max_velocity(600);
    state.apply_success();

    // Step 3: Second modification
    state.set_max_velocity(700);
    REQUIRE(state.has_changes());
    REQUIRE(state.velocity_display() == "700 mm/s");

    // Step 4: Reset should go back to 600 (the applied value), not 500
    state.reset();
    REQUIRE(state.velocity_display() == "600 mm/s");
}

// ============================================================================
// Documentation: Machine Limits Pattern Summary
// ============================================================================

/**
 * SUMMARY OF MACHINE LIMITS CHARACTERIZATION:
 *
 * 1. Overlay Opening:
 *    - handle_machine_limits_clicked() queries API for current limits
 *    - On success: current_limits_ and original_limits_ set to same values
 *    - Sliders and displays updated via update_limits_display() and update_limits_sliders()
 *
 * 2. Slider Changes:
 *    - Each slider has a handler: handle_max_velocity_changed(int), etc.
 *    - Updates current_limits_ field with static_cast<double>(value)
 *    - Updates display subject with formatted string
 *
 * 3. Display Formatting:
 *    - Velocity/SCV: "%d mm/s" (from int) or "%.0f mm/s" (from double)
 *    - Acceleration: "%d mm/s²" (from int) or "%.0f mm/s²" (from double)
 *    - Uses snprintf into char buffers, then lv_subject_copy_string()
 *
 * 4. Reset Behavior:
 *    - handle_limits_reset() copies original_limits_ to current_limits_
 *    - Calls update_limits_display() and update_limits_sliders()
 *    - Allows user to discard changes made since overlay opened
 *
 * 5. Apply Behavior:
 *    - handle_limits_apply() calls api_->set_machine_limits(current_limits_, ...)
 *    - API builds SET_VELOCITY_LIMIT gcode with 4 parameters
 *    - On success: original_limits_ = current_limits_ (prevents reset from reverting)
 *    - Shows success toast
 *
 * 6. G-code Format:
 *    - SET_VELOCITY_LIMIT VELOCITY=X ACCEL=X ACCEL_TO_DECEL=X SQUARE_CORNER_VELOCITY=X
 *    - Fixed precision (1 decimal place)
 *    - Zero values are omitted
 *
 * 7. Z Limits (Read-only):
 *    - max_z_velocity and max_z_accel are displayed but not adjustable
 *    - These require config file changes, cannot be set via SET_VELOCITY_LIMIT
 *
 * 8. State Tracking:
 *    - current_limits_: Live values reflecting slider positions
 *    - original_limits_: Snapshot when overlay opened, updated on Apply success
 *    - has_changes(): current_limits_ != original_limits_
 */
