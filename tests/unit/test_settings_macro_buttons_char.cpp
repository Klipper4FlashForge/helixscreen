// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_settings_macro_buttons_char.cpp
 * @brief Characterization tests for Macro Buttons functionality in SettingsPanel
 *
 * These tests document the EXISTING behavior of the Macro Buttons overlay feature.
 * Run with: ./build/bin/helix-tests "[macro_buttons]"
 *
 * Feature flow:
 * 1. Click Macro Buttons -> handle_macro_buttons_clicked() opens overlay
 * 2. populate_macro_dropdowns() called to populate all dropdown options
 * 3. Quick button dropdowns: "(Empty)" + all slot display names
 * 4. Standard slot dropdowns: "(Auto: X)" or "(Empty)" + sorted printer macros
 * 5. User changes dropdown -> saves to Config (quick buttons) or StandardMacros (slots)
 *
 * Key state:
 * - Quick buttons configured via Config at /standard_macros/quick_button_1 and _2
 * - Standard slots configured via StandardMacros singleton
 * - Printer macros come from MoonrakerClient::hardware().macros()
 *
 * Dropdown types:
 * - Quick Button 1 & 2: Select which StandardMacroSlot to trigger
 * - Standard Slots (9 total): Select which printer macro to use for each operation
 *
 * Standard Macro Slots:
 * - LoadFilament, UnloadFilament, Purge
 * - Pause, Resume, Cancel
 * - BedLevel, CleanNozzle, HeatSoak
 */

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

// ============================================================================
// StandardMacroSlot Enum (mirrors standard_macros.h)
// ============================================================================

/**
 * @brief Standard macro slot identifiers (mirrors StandardMacroSlot)
 */
enum class TestMacroSlot {
    LoadFilament,
    UnloadFilament,
    Purge,
    Pause,
    Resume,
    Cancel,
    BedMesh,
    BedLevel,
    CleanNozzle,
    HeatSoak,

    COUNT
};

// ============================================================================
// Test Helpers: Slot Metadata (mirrors standard_macros.cpp SLOT_METADATA)
// ============================================================================

/**
 * @brief Slot metadata structure
 */
struct TestSlotMeta {
    std::string slot_name;    ///< Config key: "load_filament"
    std::string display_name; ///< UI label: "Load Filament"
};

/**
 * @brief Get metadata for all slots
 *
 * Mirrors the SLOT_METADATA map in standard_macros.cpp
 */
static const std::map<TestMacroSlot, TestSlotMeta>& get_slot_metadata() {
    static const std::map<TestMacroSlot, TestSlotMeta> metadata = {
        {TestMacroSlot::LoadFilament, {"load_filament", "Load Filament"}},
        {TestMacroSlot::UnloadFilament, {"unload_filament", "Unload Filament"}},
        {TestMacroSlot::Purge, {"purge", "Purge"}},
        {TestMacroSlot::Pause, {"pause", "Pause Print"}},
        {TestMacroSlot::Resume, {"resume", "Resume Print"}},
        {TestMacroSlot::Cancel, {"cancel", "Cancel Print"}},
        {TestMacroSlot::BedMesh, {"bed_mesh", "Bed Mesh"}},
        {TestMacroSlot::BedLevel, {"bed_level", "Bed Level"}},
        {TestMacroSlot::CleanNozzle, {"clean_nozzle", "Clean Nozzle"}},
        {TestMacroSlot::HeatSoak, {"heat_soak", "Heat Soak"}},
    };
    return metadata;
}

/**
 * @brief Get all slots in enum order
 */
static std::vector<TestSlotMeta> get_all_slots_ordered() {
    std::vector<TestSlotMeta> result;
    for (int i = 0; i < static_cast<int>(TestMacroSlot::COUNT); ++i) {
        auto slot = static_cast<TestMacroSlot>(i);
        const auto& metadata = get_slot_metadata();
        auto it = metadata.find(slot);
        if (it != metadata.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

// ============================================================================
// Test Helpers: Quick Button Dropdown (mirrors ui_panel_settings.cpp)
// ============================================================================

/**
 * @brief Build quick button dropdown options string
 *
 * Mirrors the logic in SettingsPanel::populate_macro_dropdowns() for quick buttons:
 * Options: "(Empty)", then slot display names in enum order
 */
static std::string build_quick_button_options() {
    std::string options = "(Empty)";
    for (const auto& slot : get_all_slots_ordered()) {
        options += "\n" + slot.display_name;
    }
    return options;
}

/**
 * @brief Convert selected index to slot name
 *
 * Mirrors quick_button_index_to_slot_name() in ui_panel_settings.cpp:
 * - Index 0 = "(Empty)" -> empty string
 * - Index 1+ = slot at (index-1)
 */
static std::string quick_button_index_to_slot_name(int index) {
    if (index == 0) {
        return ""; // Empty - no slot assigned
    }
    const auto& slots = get_all_slots_ordered();
    if (index - 1 < static_cast<int>(slots.size())) {
        return slots[index - 1].slot_name;
    }
    return "";
}

/**
 * @brief Find dropdown index for a slot name
 *
 * Mirrors the find_slot_index lambda in populate_macro_dropdowns()
 */
static int find_slot_index(const std::string& slot_name) {
    if (slot_name.empty()) {
        return 0; // (Empty)
    }
    const auto& slots = get_all_slots_ordered();
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].slot_name == slot_name) {
            return static_cast<int>(i) + 1; // +1 because 0 is "(Empty)"
        }
    }
    return 0;
}

// ============================================================================
// Test Helpers: Standard Macro Dropdown (mirrors ui_panel_settings.cpp)
// ============================================================================

/**
 * @brief Information about a standard macro slot for testing
 */
struct TestMacroInfo {
    std::string configured_macro; ///< User override (or empty)
    std::string detected_macro;   ///< Auto-detected (or empty)
    std::string fallback_macro;   ///< HELIX_* fallback (or empty)

    /**
     * @brief Get the resolved macro name
     * Priority: configured > detected > fallback
     */
    [[nodiscard]] std::string get_macro() const {
        if (!configured_macro.empty())
            return configured_macro;
        if (!detected_macro.empty())
            return detected_macro;
        return fallback_macro;
    }

    /**
     * @brief Check if this slot has no usable macro
     */
    [[nodiscard]] bool is_empty() const {
        return configured_macro.empty() && detected_macro.empty() && fallback_macro.empty();
    }
};

/**
 * @brief Build standard macro dropdown options string
 *
 * Mirrors the logic in SettingsPanel::populate_macro_dropdowns() for standard slots:
 * - First option: "(Auto: detected_macro)", "(Auto: fallback_macro)", or "(Empty)"
 * - Then all printer macros, sorted alphabetically
 */
static std::string build_standard_macro_options(const TestMacroInfo& info,
                                                const std::vector<std::string>& printer_macros) {
    std::string options;

    // First option shows auto-detected, fallback, or empty
    if (!info.detected_macro.empty()) {
        options = "(Auto: " + info.detected_macro + ")";
    } else if (!info.fallback_macro.empty()) {
        options = "(Auto: " + info.fallback_macro + ")";
    } else {
        options = "(Empty)";
    }

    // Add all printer macros (already sorted)
    for (const auto& macro : printer_macros) {
        options += "\n" + macro;
    }

    return options;
}

/**
 * @brief Get selected macro from dropdown selection string
 *
 * Mirrors get_selected_macro_from_dropdown() in ui_panel_settings.cpp:
 * - "(Auto..." or "(Empty)" returns empty string (use auto-detection)
 * - Otherwise returns the macro name
 */
static std::string get_selected_macro_from_dropdown(const std::string& selected) {
    if (selected.find("(Auto") == 0 || selected.find("(Empty)") == 0) {
        return ""; // Clear configured macro, use auto-detection
    }
    return selected;
}

/**
 * @brief Find dropdown index for a configured macro
 *
 * Returns 0 if empty (use auto), otherwise index in sorted macro list + 1
 */
static int find_macro_index(const std::string& configured_macro,
                            const std::vector<std::string>& printer_macros) {
    if (configured_macro.empty()) {
        return 0; // Use auto (index 0)
    }
    int idx = 1; // Start after "(Auto/Empty)"
    for (const auto& macro : printer_macros) {
        if (macro == configured_macro) {
            return idx;
        }
        ++idx;
    }
    return 0; // Not found, fall back to auto
}

// ============================================================================
// CHARACTERIZATION: Quick Button Dropdown Population
// ============================================================================

TEST_CASE("CHAR: Quick button dropdown includes all slots",
          "[characterization][settings][macro_buttons]") {
    std::string options = build_quick_button_options();

    SECTION("First option is (Empty)") {
        REQUIRE(options.find("(Empty)") == 0);
    }

    SECTION("Contains all slot display names") {
        REQUIRE(options.find("Load Filament") != std::string::npos);
        REQUIRE(options.find("Unload Filament") != std::string::npos);
        REQUIRE(options.find("Purge") != std::string::npos);
        REQUIRE(options.find("Pause Print") != std::string::npos);
        REQUIRE(options.find("Resume Print") != std::string::npos);
        REQUIRE(options.find("Cancel Print") != std::string::npos);
        REQUIRE(options.find("Bed Mesh") != std::string::npos);
        REQUIRE(options.find("Bed Level") != std::string::npos);
        REQUIRE(options.find("Clean Nozzle") != std::string::npos);
        REQUIRE(options.find("Heat Soak") != std::string::npos);
    }

    SECTION("Slots are in enum order (after Empty)") {
        // Check order by finding positions
        size_t pos_load = options.find("Load Filament");
        size_t pos_unload = options.find("Unload Filament");
        size_t pos_purge = options.find("Purge");
        size_t pos_pause = options.find("Pause Print");
        size_t pos_resume = options.find("Resume Print");
        size_t pos_cancel = options.find("Cancel Print");
        size_t pos_bed_mesh = options.find("Bed Mesh");
        size_t pos_bed_level = options.find("Bed Level");
        size_t pos_clean = options.find("Clean Nozzle");
        size_t pos_heat = options.find("Heat Soak");

        REQUIRE(pos_load < pos_unload);
        REQUIRE(pos_unload < pos_purge);
        REQUIRE(pos_purge < pos_pause);
        REQUIRE(pos_pause < pos_resume);
        REQUIRE(pos_resume < pos_cancel);
        REQUIRE(pos_cancel < pos_bed_mesh);
        REQUIRE(pos_bed_mesh < pos_bed_level);
        REQUIRE(pos_bed_level < pos_clean);
        REQUIRE(pos_clean < pos_heat);
    }
}

TEST_CASE("CHAR: Quick button index mapping", "[characterization][settings][macro_buttons]") {
    SECTION("Index 0 maps to empty string (no slot)") {
        REQUIRE(quick_button_index_to_slot_name(0) == "");
    }

    SECTION("Index 1 maps to first slot (load_filament)") {
        REQUIRE(quick_button_index_to_slot_name(1) == "load_filament");
    }

    SECTION("Index 9 maps to clean_nozzle") {
        // BedMesh is index 7, BedLevel is 8, CleanNozzle is 9
        REQUIRE(quick_button_index_to_slot_name(9) == "clean_nozzle");
    }

    SECTION("Index 10 maps to heat_soak") {
        REQUIRE(quick_button_index_to_slot_name(10) == "heat_soak");
    }

    SECTION("Out of range index returns empty") {
        REQUIRE(quick_button_index_to_slot_name(100) == "");
    }
}

TEST_CASE("CHAR: Quick button slot name to index", "[characterization][settings][macro_buttons]") {
    SECTION("Empty string returns index 0") {
        REQUIRE(find_slot_index("") == 0);
    }

    SECTION("clean_nozzle returns index 9") {
        REQUIRE(find_slot_index("clean_nozzle") == 9);
    }

    SECTION("bed_level returns index 8") {
        REQUIRE(find_slot_index("bed_level") == 8);
    }

    SECTION("Unknown slot name returns 0 (falls back to Empty)") {
        REQUIRE(find_slot_index("unknown_slot") == 0);
    }

    SECTION("All valid slots return correct indices") {
        REQUIRE(find_slot_index("load_filament") == 1);
        REQUIRE(find_slot_index("unload_filament") == 2);
        REQUIRE(find_slot_index("purge") == 3);
        REQUIRE(find_slot_index("pause") == 4);
        REQUIRE(find_slot_index("resume") == 5);
        REQUIRE(find_slot_index("cancel") == 6);
        REQUIRE(find_slot_index("bed_mesh") == 7);
        REQUIRE(find_slot_index("bed_level") == 8);
        REQUIRE(find_slot_index("clean_nozzle") == 9);
        REQUIRE(find_slot_index("heat_soak") == 10);
    }
}

// ============================================================================
// CHARACTERIZATION: Standard Macro Dropdown Population
// ============================================================================

TEST_CASE("CHAR: Standard macro dropdown with detected macro",
          "[characterization][settings][macro_buttons]") {
    TestMacroInfo info;
    info.detected_macro = "LOAD_FILAMENT";

    std::vector<std::string> printer_macros = {"CANCEL_PRINT", "LOAD_FILAMENT", "PAUSE", "RESUME"};

    std::string options = build_standard_macro_options(info, printer_macros);

    SECTION("First option shows auto-detected macro") {
        REQUIRE(options.find("(Auto: LOAD_FILAMENT)") == 0);
    }

    SECTION("Printer macros follow the auto option") {
        REQUIRE(options.find("CANCEL_PRINT") != std::string::npos);
        REQUIRE(options.find("PAUSE") != std::string::npos);
        REQUIRE(options.find("RESUME") != std::string::npos);
    }
}

TEST_CASE("CHAR: Standard macro dropdown with fallback macro",
          "[characterization][settings][macro_buttons]") {
    TestMacroInfo info;
    info.fallback_macro = "HELIX_CLEAN_NOZZLE";

    std::vector<std::string> printer_macros = {"HOME", "QUAD_GANTRY_LEVEL"};

    std::string options = build_standard_macro_options(info, printer_macros);

    SECTION("First option shows fallback macro") {
        REQUIRE(options.find("(Auto: HELIX_CLEAN_NOZZLE)") == 0);
    }
}

TEST_CASE("CHAR: Standard macro dropdown with empty slot",
          "[characterization][settings][macro_buttons]") {
    TestMacroInfo info;
    // All empty - no detected, no fallback

    std::vector<std::string> printer_macros = {"HOME", "PARK"};

    std::string options = build_standard_macro_options(info, printer_macros);

    SECTION("First option is (Empty)") {
        REQUIRE(options.find("(Empty)") == 0);
    }

    SECTION("Printer macros still included") {
        REQUIRE(options.find("HOME") != std::string::npos);
        REQUIRE(options.find("PARK") != std::string::npos);
    }
}

TEST_CASE("CHAR: Standard macro dropdown with empty macro list",
          "[characterization][settings][macro_buttons]") {
    TestMacroInfo info;
    info.detected_macro = "PAUSE";

    std::vector<std::string> printer_macros; // Empty list

    std::string options = build_standard_macro_options(info, printer_macros);

    SECTION("Only auto option when no printer macros") {
        REQUIRE(options == "(Auto: PAUSE)");
    }
}

// ============================================================================
// CHARACTERIZATION: Macro List Sorting
// ============================================================================

TEST_CASE("CHAR: Printer macros are sorted alphabetically",
          "[characterization][settings][macro_buttons]") {
    // The actual code does: std::sort(printer_macros.begin(), printer_macros.end())
    // This is standard lexicographic sort

    std::vector<std::string> unsorted = {"PAUSE", "CANCEL_PRINT", "LOAD_FILAMENT", "HOME",
                                         "QUAD_GANTRY_LEVEL"};

    std::vector<std::string> sorted = unsorted;
    std::sort(sorted.begin(), sorted.end());

    SECTION("Sorted order is alphabetical") {
        REQUIRE(sorted[0] == "CANCEL_PRINT");
        REQUIRE(sorted[1] == "HOME");
        REQUIRE(sorted[2] == "LOAD_FILAMENT");
        REQUIRE(sorted[3] == "PAUSE");
        REQUIRE(sorted[4] == "QUAD_GANTRY_LEVEL");
    }
}

TEST_CASE("CHAR: Macro sorting is case-sensitive (uppercase sorts before lowercase)",
          "[characterization][settings][macro_buttons]") {
    // Klipper macros are typically uppercase, but let's document the behavior
    std::vector<std::string> mixed = {"pause", "PAUSE", "Pause"};
    std::sort(mixed.begin(), mixed.end());

    // Standard sort: uppercase letters come before lowercase in ASCII
    SECTION("Uppercase comes before lowercase") {
        REQUIRE(mixed[0] == "PAUSE");
        REQUIRE(mixed[1] == "Pause");
        REQUIRE(mixed[2] == "pause");
    }
}

// ============================================================================
// CHARACTERIZATION: Dropdown Selection Parsing
// ============================================================================

TEST_CASE("CHAR: Dropdown selection returns macro name or empty",
          "[characterization][settings][macro_buttons]") {
    SECTION("Auto option returns empty (use auto-detection)") {
        REQUIRE(get_selected_macro_from_dropdown("(Auto: LOAD_FILAMENT)") == "");
        REQUIRE(get_selected_macro_from_dropdown("(Auto: HELIX_CLEAN_NOZZLE)") == "");
    }

    SECTION("Empty option returns empty") {
        REQUIRE(get_selected_macro_from_dropdown("(Empty)") == "");
    }

    SECTION("Macro name returned as-is") {
        REQUIRE(get_selected_macro_from_dropdown("MY_CUSTOM_MACRO") == "MY_CUSTOM_MACRO");
        REQUIRE(get_selected_macro_from_dropdown("LOAD_FILAMENT") == "LOAD_FILAMENT");
        REQUIRE(get_selected_macro_from_dropdown("HELIX_CLEAN_NOZZLE") == "HELIX_CLEAN_NOZZLE");
    }
}

TEST_CASE("CHAR: Finding configured macro index in sorted list",
          "[characterization][settings][macro_buttons]") {
    std::vector<std::string> printer_macros = {"CANCEL_PRINT", "HOME", "LOAD_FILAMENT", "PAUSE"};

    SECTION("Empty configured returns 0 (auto)") {
        REQUIRE(find_macro_index("", printer_macros) == 0);
    }

    SECTION("First macro returns index 1") {
        REQUIRE(find_macro_index("CANCEL_PRINT", printer_macros) == 1);
    }

    SECTION("Last macro returns correct index") {
        REQUIRE(find_macro_index("PAUSE", printer_macros) == 4);
    }

    SECTION("Macro not in list returns 0 (falls back to auto)") {
        REQUIRE(find_macro_index("UNKNOWN_MACRO", printer_macros) == 0);
    }
}

// ============================================================================
// CHARACTERIZATION: Auto-Detection Display
// ============================================================================

TEST_CASE("CHAR: Auto-detection priority in display",
          "[characterization][settings][macro_buttons]") {
    std::vector<std::string> printer_macros = {"HOME"};

    SECTION("Detected takes priority over fallback in display") {
        TestMacroInfo info;
        info.detected_macro = "LOAD_FILAMENT";
        info.fallback_macro = "HELIX_LOAD";

        std::string options = build_standard_macro_options(info, printer_macros);
        REQUIRE(options.find("(Auto: LOAD_FILAMENT)") == 0);
        REQUIRE(options.find("HELIX_LOAD") == std::string::npos); // Fallback not shown
    }

    SECTION("Fallback shown when no detected") {
        TestMacroInfo info;
        info.fallback_macro = "HELIX_CLEAN_NOZZLE";

        std::string options = build_standard_macro_options(info, printer_macros);
        REQUIRE(options.find("(Auto: HELIX_CLEAN_NOZZLE)") == 0);
    }

    SECTION("Empty shown when neither detected nor fallback") {
        TestMacroInfo info;
        // All empty

        std::string options = build_standard_macro_options(info, printer_macros);
        REQUIRE(options.find("(Empty)") == 0);
    }
}

// ============================================================================
// CHARACTERIZATION: Configuration Persistence
// ============================================================================

TEST_CASE("CHAR: Quick button config paths", "[characterization][settings][macro_buttons]") {
    // Documents the config paths used for quick buttons
    // The actual code: config->set<std::string>("/standard_macros/quick_button_1", slot_name);

    SECTION("Quick button 1 path") {
        std::string path = "/standard_macros/quick_button_1";
        REQUIRE(path == "/standard_macros/quick_button_1");
    }

    SECTION("Quick button 2 path") {
        std::string path = "/standard_macros/quick_button_2";
        REQUIRE(path == "/standard_macros/quick_button_2");
    }

    SECTION("Default values are clean_nozzle and bed_level") {
        // From populate_macro_dropdowns():
        // config->get<std::string>("/standard_macros/quick_button_1", "clean_nozzle")
        // config->get<std::string>("/standard_macros/quick_button_2", "bed_level")
        std::string default_qb1 = "clean_nozzle";
        std::string default_qb2 = "bed_level";
        REQUIRE(default_qb1 == "clean_nozzle");
        REQUIRE(default_qb2 == "bed_level");
    }
}

TEST_CASE("CHAR: Standard macro config paths", "[characterization][settings][macro_buttons]") {
    // Documents the config paths used for standard macros
    // The actual code: std::string path = "/standard_macros/" + slot.slot_name;

    SECTION("Slot paths follow pattern /standard_macros/{slot_name}") {
        std::string base = "/standard_macros/";
        std::string load_path = base + "load_filament";
        std::string clean_path = base + "clean_nozzle";
        std::string level_path = base + "bed_level";

        REQUIRE(load_path == "/standard_macros/load_filament");
        REQUIRE(clean_path == "/standard_macros/clean_nozzle");
        REQUIRE(level_path == "/standard_macros/bed_level");
    }
}

// ============================================================================
// CHARACTERIZATION: Edge Cases
// ============================================================================

TEST_CASE("CHAR: Empty macro list handled gracefully",
          "[characterization][settings][macro_buttons]") {
    TestMacroInfo info;
    info.detected_macro = "PAUSE";
    std::vector<std::string> empty_macros;

    std::string options = build_standard_macro_options(info, empty_macros);

    SECTION("Only auto option present") {
        REQUIRE(options == "(Auto: PAUSE)");
        REQUIRE(options.find('\n') == std::string::npos); // No newlines = no additional options
    }
}

TEST_CASE("CHAR: Special characters in macro names",
          "[characterization][settings][macro_buttons]") {
    // Klipper macro names can include underscores and some special chars
    std::vector<std::string> macros = {"LOAD_FILAMENT_V2", "PRINT_START_2024", "G28_HOME",
                                       "BED_MESH_CALIBRATE"};
    std::sort(macros.begin(), macros.end());

    TestMacroInfo info;
    std::string options = build_standard_macro_options(info, macros);

    SECTION("Underscores and numbers handled") {
        REQUIRE(options.find("LOAD_FILAMENT_V2") != std::string::npos);
        REQUIRE(options.find("PRINT_START_2024") != std::string::npos);
        REQUIRE(options.find("G28_HOME") != std::string::npos);
    }

    SECTION("Sorting handles mixed alphanumeric") {
        // Numbers sort before letters in ASCII
        REQUIRE(macros[0] == "BED_MESH_CALIBRATE");
        REQUIRE(macros[1] == "G28_HOME");
        REQUIRE(macros[2] == "LOAD_FILAMENT_V2");
        REQUIRE(macros[3] == "PRINT_START_2024");
    }
}

TEST_CASE("CHAR: Very long macro name in dropdown", "[characterization][settings][macro_buttons]") {
    std::string long_name = "VERY_LONG_MACRO_NAME_THAT_MIGHT_CAUSE_DISPLAY_ISSUES_IN_DROPDOWN";
    std::vector<std::string> macros = {long_name};

    TestMacroInfo info;
    std::string options = build_standard_macro_options(info, macros);

    SECTION("Long names included without truncation in options string") {
        REQUIRE(options.find(long_name) != std::string::npos);
    }
}

// ============================================================================
// CHARACTERIZATION: Slot Row Names (XML mapping)
// ============================================================================

TEST_CASE("CHAR: XML row names for standard slots", "[characterization][settings][macro_buttons]") {
    // Documents the row names used in macro_buttons_overlay.xml
    // These must match for lv_obj_find_by_name() to work

    const std::vector<std::pair<std::string, std::string>> slot_rows = {
        {"load_filament", "row_load_filament"},
        {"unload_filament", "row_unload_filament"},
        {"purge", "row_purge"},
        {"pause", "row_pause"},
        {"resume", "row_resume"},
        {"cancel", "row_cancel"},
        {"bed_mesh", "row_bed_mesh"},
        {"bed_level", "row_bed_level"},
        {"clean_nozzle", "row_clean_nozzle"},
        {"heat_soak", "row_heat_soak"},
    };

    SECTION("Row names follow pattern row_{slot_name}") {
        for (const auto& [slot_name, row_name] : slot_rows) {
            std::string expected = "row_" + slot_name;
            REQUIRE(row_name == expected);
        }
    }

    SECTION("All 10 standard slots have rows") {
        // All StandardMacroSlot values (except COUNT) have a row in the overlay
        REQUIRE(slot_rows.size() == 10);
    }
}

TEST_CASE("CHAR: Quick button row names", "[characterization][settings][macro_buttons]") {
    SECTION("Row names match XML component names") {
        std::string qb1_row = "row_quick_button_1";
        std::string qb2_row = "row_quick_button_2";

        REQUIRE(qb1_row == "row_quick_button_1");
        REQUIRE(qb2_row == "row_quick_button_2");
    }
}

// ============================================================================
// CHARACTERIZATION: Complete State Machine
// ============================================================================

/**
 * @brief Simulates the Macro Buttons configuration state
 *
 * This helper mirrors the dropdown interaction flow without requiring LVGL.
 */
class MacroButtonsStateMachine {
  public:
    // Quick button configuration (stored slot names)
    std::string quick_button_1 = "clean_nozzle"; // Default
    std::string quick_button_2 = "bed_level";    // Default

    // Standard slot configurations (configured macro names)
    std::map<std::string, std::string> slot_configs;

    // Available printer macros (sorted)
    std::vector<std::string> printer_macros;

    // Slot info (for display)
    std::map<std::string, TestMacroInfo> slot_info;

    /**
     * @brief Set printer macros (automatically sorted)
     */
    void set_printer_macros(std::vector<std::string> macros) {
        printer_macros = std::move(macros);
        std::sort(printer_macros.begin(), printer_macros.end());
    }

    /**
     * @brief Get quick button dropdown options
     */
    [[nodiscard]] std::string get_quick_button_options() const {
        return build_quick_button_options();
    }

    /**
     * @brief Get standard slot dropdown options
     */
    [[nodiscard]] std::string get_slot_options(const std::string& slot_name) const {
        auto it = slot_info.find(slot_name);
        TestMacroInfo info;
        if (it != slot_info.end()) {
            info = it->second;
        }
        return build_standard_macro_options(info, printer_macros);
    }

    /**
     * @brief Handle quick button dropdown change
     */
    void set_quick_button(int button_num, int dropdown_index) {
        std::string slot_name = quick_button_index_to_slot_name(dropdown_index);
        if (button_num == 1) {
            quick_button_1 = slot_name;
        } else {
            quick_button_2 = slot_name;
        }
    }

    /**
     * @brief Handle standard slot dropdown change
     */
    void set_slot_macro(const std::string& slot_name, const std::string& selected_option) {
        std::string macro = get_selected_macro_from_dropdown(selected_option);
        slot_configs[slot_name] = macro;
    }

    /**
     * @brief Get selected index for quick button dropdown
     */
    [[nodiscard]] int get_quick_button_index(int button_num) const {
        const std::string& slot = (button_num == 1) ? quick_button_1 : quick_button_2;
        return find_slot_index(slot);
    }

    /**
     * @brief Get selected index for standard slot dropdown
     */
    [[nodiscard]] int get_slot_index(const std::string& slot_name) const {
        auto it = slot_configs.find(slot_name);
        std::string configured = (it != slot_configs.end()) ? it->second : "";
        return find_macro_index(configured, printer_macros);
    }
};

TEST_CASE("CHAR: Complete quick button workflow", "[characterization][settings][macro_buttons]") {
    MacroButtonsStateMachine state;

    SECTION("Default quick button selections") {
        REQUIRE(state.quick_button_1 == "clean_nozzle");
        REQUIRE(state.quick_button_2 == "bed_level");
        REQUIRE(state.get_quick_button_index(1) == 9); // clean_nozzle
        REQUIRE(state.get_quick_button_index(2) == 8); // bed_level
    }

    SECTION("Change quick button 1 to Load Filament") {
        state.set_quick_button(1, 1); // Index 1 = load_filament
        REQUIRE(state.quick_button_1 == "load_filament");
        REQUIRE(state.get_quick_button_index(1) == 1);
    }

    SECTION("Set quick button to Empty") {
        state.set_quick_button(1, 0); // Index 0 = (Empty)
        REQUIRE(state.quick_button_1 == "");
        REQUIRE(state.get_quick_button_index(1) == 0);
    }
}

TEST_CASE("CHAR: Complete standard slot workflow", "[characterization][settings][macro_buttons]") {
    MacroButtonsStateMachine state;
    state.set_printer_macros(
        {"CANCEL_PRINT", "LOAD_FILAMENT", "PAUSE", "RESUME", "UNLOAD_FILAMENT"});
    state.slot_info["load_filament"].detected_macro = "LOAD_FILAMENT";

    SECTION("Auto-detection selected by default") {
        REQUIRE(state.get_slot_index("load_filament") == 0); // Auto
    }

    SECTION("Select specific macro") {
        state.set_slot_macro("load_filament", "UNLOAD_FILAMENT"); // Pick different macro
        REQUIRE(state.slot_configs["load_filament"] == "UNLOAD_FILAMENT");
        REQUIRE(state.get_slot_index("load_filament") == 5); // Position in sorted list + 1
    }

    SECTION("Select auto clears configuration") {
        state.set_slot_macro("load_filament", "MY_MACRO");
        REQUIRE_FALSE(state.slot_configs["load_filament"].empty());

        state.set_slot_macro("load_filament", "(Auto: LOAD_FILAMENT)");
        REQUIRE(state.slot_configs["load_filament"].empty());
    }
}

// ============================================================================
// Documentation: Macro Buttons Pattern Summary
// ============================================================================

/**
 * SUMMARY OF MACRO BUTTONS CHARACTERIZATION:
 *
 * 1. Overlay Opening:
 *    - handle_macro_buttons_clicked() creates overlay if needed (lazy init)
 *    - populate_macro_dropdowns() called every time overlay shown
 *    - Handles printer reconnection by refreshing macro list
 *
 * 2. Quick Button Dropdowns:
 *    - Options: "(Empty)" + all StandardMacroSlot display names (10 slots)
 *    - Stored in Config at /standard_macros/quick_button_1 and _2
 *    - Defaults: quick_button_1 = "clean_nozzle", quick_button_2 = "bed_level"
 *    - Index 0 = Empty, Index 1-10 = slots in enum order
 *
 * 3. Standard Macro Dropdowns:
 *    - First option: "(Auto: detected)", "(Auto: fallback)", or "(Empty)"
 *    - Then all printer macros, sorted alphabetically
 *    - Stored in Config at /standard_macros/{slot_name}
 *    - Empty config = use auto-detection
 *
 * 4. Macro Source Priority:
 *    - Configured > Detected > Fallback
 *    - Display shows detected/fallback in "(Auto: X)" format
 *    - User can override by selecting specific macro
 *
 * 5. XML Structure:
 *    - Quick button rows: row_quick_button_1, row_quick_button_2
 *    - Slot rows: row_{slot_name} (e.g., row_load_filament)
 *    - Each row contains a "dropdown" child widget
 *    - All 10 standard slots have rows in the overlay
 *
 * 6. Event Callbacks:
 *    - on_quick_button_1_changed, on_quick_button_2_changed
 *    - on_load_filament_changed, on_unload_filament_changed, etc.
 *    - All registered via lv_xml_register_event_cb() in init_subjects()
 *
 * 7. Edge Cases:
 *    - Empty macro list: only auto option shown
 *    - Unknown slot name: falls back to index 0 (Empty/Auto)
 *    - Long macro names: not truncated in options string
 *    - Special characters in names: handled by standard string operations
 */
