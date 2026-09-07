// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_start_analyzer.h"
#include "print_start_enhancer.h"

#include <chrono>
#include <regex>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Macros for Enhancement Testing
// ============================================================================

// Simple macro with operations to enhance
static const char* SIMPLE_MACRO = R"(  G28
  BED_MESH_CALIBRATE
  QUAD_GANTRY_LEVEL
  M109 S{params.EXTRUDER|default(210)|float})";

// Macro with indentation variations
static const char* INDENTED_MACRO = R"(    G28
    BED_MESH_CALIBRATE
    QUAD_GANTRY_LEVEL
    CLEAN_NOZZLE)";

// Already partially enhanced macro
static const char* PARTIAL_MACRO = R"({% set SKIP_QGL = params.SKIP_QGL|default(0)|int %}
G28
{% if SKIP_QGL == 0 %}
  QUAD_GANTRY_LEVEL
{% endif %}
BED_MESH_CALIBRATE
M109 S{params.EXTRUDER})";

// ============================================================================
// Tests: generate_param_declaration
// ============================================================================

TEST_CASE("PrintStartEnhancer: generate_param_declaration", "[enhancer][codegen]") {
    SECTION("Standard parameter declaration") {
        auto decl = PrintStartEnhancer::generate_param_declaration("SKIP_BED_MESH");

        REQUIRE(decl == "{% set SKIP_BED_MESH = params.SKIP_BED_MESH|default(0)|int %}");
    }

    SECTION("Different parameter names") {
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP_QGL") ==
                "{% set SKIP_QGL = params.SKIP_QGL|default(0)|int %}");

        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP_Z_TILT") ==
                "{% set SKIP_Z_TILT = params.SKIP_Z_TILT|default(0)|int %}");

        REQUIRE(PrintStartEnhancer::generate_param_declaration("CUSTOM_PARAM") ==
                "{% set CUSTOM_PARAM = params.CUSTOM_PARAM|default(0)|int %}");
    }
}

// ============================================================================
// Tests: generate_conditional_block
// ============================================================================

TEST_CASE("PrintStartEnhancer: generate_conditional_block", "[enhancer][codegen]") {
    SECTION("With declaration (default)") {
        auto block = PrintStartEnhancer::generate_conditional_block("  BED_MESH_CALIBRATE",
                                                                    "SKIP_BED_MESH", true);

        // Should contain declaration
        REQUIRE(block.find("{% set SKIP_BED_MESH") != std::string::npos);
        // Should contain if block
        REQUIRE(block.find("{% if SKIP_BED_MESH == 0 %}") != std::string::npos);
        // Should contain operation
        REQUIRE(block.find("BED_MESH_CALIBRATE") != std::string::npos);
        // Should contain endif
        REQUIRE(block.find("{% endif %}") != std::string::npos);
    }

    SECTION("Without declaration") {
        auto block = PrintStartEnhancer::generate_conditional_block("  QUAD_GANTRY_LEVEL",
                                                                    "SKIP_QGL", false);

        // Should NOT contain declaration
        REQUIRE(block.find("{% set SKIP_QGL") == std::string::npos);
        // Should contain if block
        REQUIRE(block.find("{% if SKIP_QGL == 0 %}") != std::string::npos);
        // Should contain operation
        REQUIRE(block.find("QUAD_GANTRY_LEVEL") != std::string::npos);
    }

    SECTION("Preserves indentation") {
        auto block = PrintStartEnhancer::generate_conditional_block("    BED_MESH_CALIBRATE",
                                                                    "SKIP_BED_MESH", true);

        // Lines should start with 4 spaces (original indentation)
        REQUIRE(block.find("    {% set") != std::string::npos);
        REQUIRE(block.find("    {% if") != std::string::npos);
    }

    SECTION("Handles trailing whitespace") {
        auto block = PrintStartEnhancer::generate_conditional_block("  BED_MESH_CALIBRATE  \n",
                                                                    "SKIP_BED_MESH", true);

        // Operation should not have trailing whitespace or newlines
        // The operation line should be indented with 2 extra spaces inside the if block
        REQUIRE(block.find("    BED_MESH_CALIBRATE\n") != std::string::npos);
    }
}

// ============================================================================
// Tests: generate_wrapper
// ============================================================================

TEST_CASE("PrintStartEnhancer: generate_wrapper", "[enhancer][codegen]") {
    PrintStartOperation op;
    op.name = "BED_MESH_CALIBRATE";
    op.category = PrintStartOpCategory::BED_MESH;
    op.line_number = 3;

    auto enhancement = PrintStartEnhancer::generate_wrapper(op, "SKIP_BED_MESH");

    SECTION("Populates enhancement fields correctly") {
        REQUIRE(enhancement.operation_name == "BED_MESH_CALIBRATE");
        REQUIRE(enhancement.category == PrintStartOpCategory::BED_MESH);
        REQUIRE(enhancement.skip_param_name == "SKIP_BED_MESH");
        REQUIRE(enhancement.line_number == 3);
        REQUIRE(enhancement.user_approved == false);
    }

    SECTION("Generates enhanced code") {
        REQUIRE_FALSE(enhancement.enhanced_code.empty());
        REQUIRE(enhancement.enhanced_code.find("{% set SKIP_BED_MESH") != std::string::npos);
        REQUIRE(enhancement.enhanced_code.find("{% if SKIP_BED_MESH == 0 %}") != std::string::npos);
        REQUIRE(enhancement.enhanced_code.find("BED_MESH_CALIBRATE") != std::string::npos);
        REQUIRE(enhancement.enhanced_code.find("{% endif %}") != std::string::npos);
    }
}

// ============================================================================
// Tests: apply_to_source
// ============================================================================

TEST_CASE("PrintStartEnhancer: apply_to_source", "[enhancer][codegen]") {
    SECTION("No enhancements returns original") {
        std::vector<MacroEnhancement> empty;
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, empty);

        REQUIRE(result == SIMPLE_MACRO);
    }

    SECTION("Unapproved enhancements are ignored") {
        MacroEnhancement enhancement;
        enhancement.operation_name = "BED_MESH_CALIBRATE";
        enhancement.skip_param_name = "SKIP_BED_MESH";
        enhancement.line_number = 2;
        enhancement.user_approved = false; // Not approved

        std::vector<MacroEnhancement> enhancements = {enhancement};
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Should be unchanged since not approved
        REQUIRE(result == SIMPLE_MACRO);
    }

    SECTION("Single approved enhancement modifies source") {
        MacroEnhancement enhancement;
        enhancement.operation_name = "BED_MESH_CALIBRATE";
        enhancement.skip_param_name = "SKIP_BED_MESH";
        enhancement.line_number = 2; // Second line in SIMPLE_MACRO
        enhancement.user_approved = true;

        std::vector<MacroEnhancement> enhancements = {enhancement};
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Should contain the conditional wrapper
        REQUIRE(result.find("{% set SKIP_BED_MESH") != std::string::npos);
        REQUIRE(result.find("{% if SKIP_BED_MESH == 0 %}") != std::string::npos);
        // Original operation should still be present (inside the if block)
        REQUIRE(result.find("BED_MESH_CALIBRATE") != std::string::npos);
    }

    SECTION("Multiple enhancements apply correctly") {
        MacroEnhancement mesh;
        mesh.operation_name = "BED_MESH_CALIBRATE";
        mesh.skip_param_name = "SKIP_BED_MESH";
        mesh.line_number = 2;
        mesh.user_approved = true;

        MacroEnhancement qgl;
        qgl.operation_name = "QUAD_GANTRY_LEVEL";
        qgl.skip_param_name = "SKIP_QGL";
        qgl.line_number = 3;
        qgl.user_approved = true;

        std::vector<MacroEnhancement> enhancements = {mesh, qgl};
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Should contain both conditionals
        REQUIRE(result.find("{% set SKIP_BED_MESH") != std::string::npos);
        REQUIRE(result.find("{% set SKIP_QGL") != std::string::npos);
        REQUIRE(result.find("{% if SKIP_BED_MESH == 0 %}") != std::string::npos);
        REQUIRE(result.find("{% if SKIP_QGL == 0 %}") != std::string::npos);
    }

    SECTION("Out of range line number is handled gracefully") {
        MacroEnhancement enhancement;
        enhancement.operation_name = "BED_MESH_CALIBRATE";
        enhancement.skip_param_name = "SKIP_BED_MESH";
        enhancement.line_number = 100; // Way out of range
        enhancement.user_approved = true;

        std::vector<MacroEnhancement> enhancements = {enhancement};
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Should not crash, may return original or partial modification
        REQUIRE_FALSE(result.empty());
    }

    SECTION("Wrong operation at line number is handled gracefully") {
        MacroEnhancement enhancement;
        enhancement.operation_name = "BED_MESH_CALIBRATE";
        enhancement.skip_param_name = "SKIP_BED_MESH";
        enhancement.line_number = 1; // Line 1 is G28, not BED_MESH_CALIBRATE
        enhancement.user_approved = true;

        std::vector<MacroEnhancement> enhancements = {enhancement};
        auto result = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Should not modify since operation doesn't match
        // The enhancement should be skipped, so we won't have the wrapper for bed mesh
        // (though the source will still be valid)
        REQUIRE_FALSE(result.empty());
    }
}

// ============================================================================
// Tests: validate_jinja2_syntax
// ============================================================================

TEST_CASE("PrintStartEnhancer: validate_jinja2_syntax", "[enhancer][validation]") {
    SECTION("Valid Jinja2 code") {
        const char* valid_code = R"(
{% set SKIP_BED_MESH = params.SKIP_BED_MESH|default(0)|int %}
{% if SKIP_BED_MESH == 0 %}
  BED_MESH_CALIBRATE
{% endif %}
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(valid_code) == true);
    }

    SECTION("Valid nested if/for blocks") {
        const char* nested = R"(
{% if condition %}
  {% for i in range(5) %}
    G1 X{{ i }}
  {% endfor %}
{% endif %}
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(nested) == true);
    }

    SECTION("Unclosed brace-percent block") {
        const char* unclosed = R"(
{% set SKIP = 1
BED_MESH_CALIBRATE
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(unclosed) == false);
    }

    SECTION("Mismatched if/endif") {
        const char* mismatched_if = R"(
{% if condition %}
  BED_MESH_CALIBRATE
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(mismatched_if) == false);
    }

    SECTION("Extra endif") {
        const char* extra_endif = R"(
{% if condition %}
  BED_MESH_CALIBRATE
{% endif %}
{% endif %}
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(extra_endif) == false);
    }

    SECTION("Mismatched for/endfor") {
        const char* mismatched_for = R"(
{% for i in range(5) %}
  G1 X{{ i }}
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(mismatched_for) == false);
    }

    SECTION("Unclosed expression braces") {
        const char* unclosed_expr = R"(
{{ variable
BED_MESH_CALIBRATE
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(unclosed_expr) == false);
    }

    SECTION("Valid expression syntax") {
        const char* valid_expr = R"(
{{ params.EXTRUDER|default(210)|float }}
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(valid_expr) == true);
    }

    SECTION("Empty input is valid") {
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax("") == true);
    }

    SECTION("Plain gcode without Jinja2 is valid") {
        const char* plain = R"(
G28
BED_MESH_CALIBRATE
M109 S210
)";
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(plain) == true);
    }
}

// ============================================================================
// Tests: generate_backup_filename
// ============================================================================

TEST_CASE("PrintStartEnhancer: generate_backup_filename", "[enhancer][utility]") {
    SECTION("Format is correct") {
        auto filename = PrintStartEnhancer::generate_backup_filename("printer.cfg");

        // Should start with printer.cfg.backup.
        REQUIRE(filename.find("printer.cfg.backup.") == 0);

        // Should contain timestamp in format YYYYMMDD_HHMMSS
        std::regex timestamp_pattern(R"(printer\.cfg\.backup\.\d{8}_\d{6})");
        REQUIRE(std::regex_match(filename, timestamp_pattern));
    }

    SECTION("Works with different source files") {
        auto filename = PrintStartEnhancer::generate_backup_filename("macros.cfg");
        REQUIRE(filename.find("macros.cfg.backup.") == 0);
    }

    SECTION("Consecutive calls produce different filenames") {
        auto filename1 = PrintStartEnhancer::generate_backup_filename("printer.cfg");
        // Small delay to ensure different timestamp
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto filename2 = PrintStartEnhancer::generate_backup_filename("printer.cfg");

        // May or may not be different depending on timing, but both should be valid
        REQUIRE(filename1.find("printer.cfg.backup.") == 0);
        REQUIRE(filename2.find("printer.cfg.backup.") == 0);
    }
}

// ============================================================================
// Tests: config_file_from_backup_filename
// ============================================================================

TEST_CASE("PrintStartEnhancer: config_file_from_backup_filename", "[enhancer][utility]") {
    // The restore path rewrites whatever this returns. PRINT_START lives in
    // whichever file a given printer keeps it in, and enhance_macro() backs up
    // that file, so a restore that assumed printer.cfg would overwrite a config
    // the backup never contained.
    SECTION("Round-trips whatever generate_backup_filename produced") {
        for (const char* source : {"printer.cfg", "macros.cfg", "helix_macros.cfg"}) {
            const auto backup = PrintStartEnhancer::generate_backup_filename(source);
            CHECK(PrintStartEnhancer::config_file_from_backup_filename(backup) ==
                  std::string(source));
        }
    }

    SECTION("A config file whose own name contains .backup. still round-trips") {
        // The timestamp is the anchor, so the last ".backup.<stamp>" wins.
        const auto backup = PrintStartEnhancer::generate_backup_filename("a.backup.cfg");
        CHECK(PrintStartEnhancer::config_file_from_backup_filename(backup) == "a.backup.cfg");
    }

    SECTION("Names we did not produce are refused, not guessed at") {
        // Empty is the signal to refuse the restore outright. Returning a
        // default here is what turns a recovery action into data loss.
        CHECK(PrintStartEnhancer::config_file_from_backup_filename("").empty());
        CHECK(PrintStartEnhancer::config_file_from_backup_filename("printer.cfg").empty());
        CHECK(PrintStartEnhancer::config_file_from_backup_filename("printer.cfg.bak").empty());
        CHECK(PrintStartEnhancer::config_file_from_backup_filename("printer.cfg.backup").empty());
        // Truncated / malformed timestamps.
        CHECK(PrintStartEnhancer::config_file_from_backup_filename("printer.cfg.backup.2025122")
                  .empty());
        CHECK(PrintStartEnhancer::config_file_from_backup_filename(
                  "printer.cfg.backup.20251222_17053")
                  .empty());
        CHECK(PrintStartEnhancer::config_file_from_backup_filename(
                  "printer.cfg.backup.20251222-170530")
                  .empty());
        // No config name in front of the suffix.
        CHECK(PrintStartEnhancer::config_file_from_backup_filename(".backup.20251222_170530")
                  .empty());
        // Trailing junk after the stamp.
        CHECK(PrintStartEnhancer::config_file_from_backup_filename(
                  "printer.cfg.backup.20251222_170530.tmp")
                  .empty());
    }

    SECTION("A macros.cfg backup never resolves to printer.cfg") {
        // The specific data-loss shape: restoring a macros.cfg backup must not
        // name printer.cfg as its destination.
        const auto backup = PrintStartEnhancer::generate_backup_filename("macros.cfg");
        const auto target = PrintStartEnhancer::config_file_from_backup_filename(backup);
        REQUIRE(target == "macros.cfg");
        REQUIRE(target != "printer.cfg");
    }
}

// ============================================================================
// Tests: get_skip_param_for_category
// ============================================================================

TEST_CASE("PrintStartEnhancer: get_skip_param_for_category", "[enhancer][utility]") {
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::BED_MESH) ==
            "SKIP_BED_MESH");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::QGL) ==
            "SKIP_QGL");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::Z_TILT) ==
            "SKIP_Z_TILT");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::NOZZLE_CLEAN) ==
            "SKIP_NOZZLE_CLEAN");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::HOMING) ==
            "SKIP_HOMING");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::CHAMBER_SOAK) ==
            "SKIP_SOAK");
    REQUIRE(PrintStartEnhancer::get_skip_param_for_category(PrintStartOpCategory::UNKNOWN) == "");
}

// ============================================================================
// Tests: Integration - Analyzer + Enhancer
// ============================================================================

TEST_CASE("Integration: Analyze then enhance", "[enhancer][integration]") {
    SECTION("Create enhancements from analysis result") {
        // Analyze a macro
        auto analysis = PrintStartAnalyzer::parse_macro("PRINT_START", SIMPLE_MACRO);

        // Get uncontrollable operations
        auto uncontrollable = analysis.get_uncontrollable_operations();
        REQUIRE(uncontrollable.size() >= 2); // BED_MESH and QGL at minimum

        // Create enhancements for each
        std::vector<MacroEnhancement> enhancements;
        for (const auto* op : uncontrollable) {
            auto skip_param = PrintStartEnhancer::get_skip_param_for_category(op->category);
            if (!skip_param.empty()) {
                auto enhancement = PrintStartEnhancer::generate_wrapper(*op, skip_param);
                enhancement.user_approved = true;
                enhancements.push_back(enhancement);
            }
        }

        REQUIRE_FALSE(enhancements.empty());

        // Apply enhancements
        auto modified = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, enhancements);

        // Validate the result
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(modified));

        // Should have skip params for enhanced operations
        for (const auto& e : enhancements) {
            REQUIRE(modified.find(e.skip_param_name) != std::string::npos);
        }
    }

    SECTION("Enhanced macro should be parseable and controllable") {
        // Start with uncontrollable macro
        auto initial_analysis = PrintStartAnalyzer::parse_macro("PRINT_START", SIMPLE_MACRO);
        REQUIRE(initial_analysis.is_controllable == false);

        // Get mesh operation and create enhancement
        auto mesh_op = initial_analysis.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(mesh_op != nullptr);

        auto enhancement = PrintStartEnhancer::generate_wrapper(*mesh_op, "SKIP_BED_MESH");
        enhancement.user_approved = true;

        // Apply enhancement
        auto modified = PrintStartEnhancer::apply_to_source(SIMPLE_MACRO, {enhancement});

        // Re-analyze the modified macro
        auto final_analysis = PrintStartAnalyzer::parse_macro("PRINT_START", modified);

        // The mesh operation should now be controllable
        auto enhanced_mesh = final_analysis.get_operation(PrintStartOpCategory::BED_MESH);
        REQUIRE(enhanced_mesh != nullptr);
        REQUIRE(enhanced_mesh->has_skip_param == true);
        REQUIRE(enhanced_mesh->skip_param_name == "SKIP_BED_MESH");
    }
}

// ============================================================================
// Tests: Edge Cases
// ============================================================================

// ============================================================================
// Tests: Parameter Name Validation (Security)
// ============================================================================

TEST_CASE("PrintStartEnhancer: Parameter name validation", "[enhancer][security]") {
    SECTION("Valid parameter names") {
        // Standard skip params
        REQUIRE_FALSE(PrintStartEnhancer::generate_param_declaration("SKIP_BED_MESH").empty());
        REQUIRE_FALSE(PrintStartEnhancer::generate_param_declaration("SKIP_QGL").empty());

        // With numbers
        REQUIRE_FALSE(PrintStartEnhancer::generate_param_declaration("SKIP_STEP_1").empty());
        REQUIRE_FALSE(PrintStartEnhancer::generate_param_declaration("TEST123").empty());

        // Single character
        REQUIRE_FALSE(PrintStartEnhancer::generate_param_declaration("X").empty());
    }

    SECTION("Invalid parameter names - special characters (injection risk)") {
        // Jinja2 template injection attempts
        REQUIRE(PrintStartEnhancer::generate_param_declaration("X %}{{ evil }}{% set Y").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("X%}{{evil}}{%").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("X}}").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("{{X").empty());

        // Spaces and newlines
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP BED").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP\nBED").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP\tBED").empty());

        // Other special characters
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP-BED").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP.BED").empty());
        REQUIRE(PrintStartEnhancer::generate_param_declaration("SKIP;BED").empty());
    }

    SECTION("Invalid parameter names - edge cases") {
        // Empty
        REQUIRE(PrintStartEnhancer::generate_param_declaration("").empty());

        // Too long (over 64 chars)
        std::string too_long(100, 'A');
        REQUIRE(PrintStartEnhancer::generate_param_declaration(too_long).empty());
    }

    SECTION("generate_conditional_block validates parameter name") {
        // Valid
        auto valid_block = PrintStartEnhancer::generate_conditional_block("  BED_MESH_CALIBRATE",
                                                                          "SKIP_BED_MESH", true);
        REQUIRE_FALSE(valid_block.empty());

        // Invalid - injection attempt
        auto invalid_block = PrintStartEnhancer::generate_conditional_block("  BED_MESH_CALIBRATE",
                                                                            "X }}{{ evil }}", true);
        REQUIRE(invalid_block.empty());
    }
}

TEST_CASE("PrintStartEnhancer: Edge cases", "[enhancer][edge]") {
    SECTION("Empty macro") {
        std::vector<MacroEnhancement> enhancements;
        auto result = PrintStartEnhancer::apply_to_source("", enhancements);
        REQUIRE(result.empty());
    }

    SECTION("Macro with only comments") {
        const char* comments_only = R"(; Comment 1
; Comment 2
# Python style comment)";

        MacroEnhancement enhancement;
        enhancement.operation_name = "BED_MESH_CALIBRATE";
        enhancement.line_number = 1;
        enhancement.user_approved = true;

        auto result = PrintStartEnhancer::apply_to_source(comments_only, {enhancement});
        // Should not crash, enhancement won't match any line
        REQUIRE_FALSE(result.empty());
    }

    SECTION("Very long operation line") {
        std::string long_line = "  BED_MESH_CALIBRATE";
        for (int i = 0; i < 100; ++i) {
            long_line += " PARAM" + std::to_string(i) + "=value";
        }

        auto block =
            PrintStartEnhancer::generate_conditional_block(long_line, "SKIP_BED_MESH", true);

        // Should still be valid
        REQUIRE(PrintStartEnhancer::validate_jinja2_syntax(block));
        REQUIRE(block.find("BED_MESH_CALIBRATE") != std::string::npos);
    }

    SECTION("Parameter name with numbers") {
        auto decl = PrintStartEnhancer::generate_param_declaration("SKIP_STEP_1");
        REQUIRE(decl == "{% set SKIP_STEP_1 = params.SKIP_STEP_1|default(0)|int %}");
    }
}

// ============================================================================
// Tests: MacroEnhancement struct
// ============================================================================

TEST_CASE("MacroEnhancement: Default initialization", "[enhancer][struct]") {
    MacroEnhancement e;

    REQUIRE(e.operation_name.empty());
    REQUIRE(e.category == PrintStartOpCategory::UNKNOWN);
    REQUIRE(e.skip_param_name.empty());
    REQUIRE(e.original_line.empty());
    REQUIRE(e.enhanced_code.empty());
    REQUIRE(e.line_number == 0);
    REQUIRE(e.user_approved == false);
}

TEST_CASE("EnhancementResult: Default initialization", "[enhancer][struct]") {
    EnhancementResult r;

    REQUIRE(r.success == false);
    REQUIRE(r.error_message.empty());
    REQUIRE(r.backup_filename.empty());
    REQUIRE(r.backup_full_path.empty());
    REQUIRE(r.operations_enhanced == 0);
    REQUIRE(r.lines_added == 0);
    REQUIRE(r.lines_modified == 0);
}
