// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_afc_fault_path_modal.cpp
 * @brief The AFC position graphic actually reaches the modals (#1184).
 *
 * Run with: ./build/bin/helix-tests "[afc][fault][modal]"
 *
 * Two modals can show an AFC lane fault and both are wired:
 *   - RecoveryModalPresenter -> ActionPromptModal   (the `!!` / GcodeErrorRouter path)
 *   - AmsPanel -> AmsLoadingErrorModal              (the AmsAction::ERROR path)
 *
 * Both hand their text through helix::ui::afc_fault_path_apply(), which publishes
 * the stop point to `afc_fault_segment`. `<afc_fault_path/>` in each modal's XML
 * binds `hidden` to 0, so the safety property — an unrecognised message renders
 * exactly as it did before — is one assertion away.
 *
 * Mutation checks (each must break the listed test):
 *   - drop the afc_fault_path_apply() call from RecoveryModalPresenter::present()
 *     -> "the recovery modal shows the graphic for a recognised AFC fault" fails
 *   - make afc_fault_path_apply() leave the subject alone when unrecognised
 *     -> "an unrecognised fault leaves no graphic behind" fails
 *   - remove <afc_fault_path/> from ams_loading_error_modal.xml
 *     -> "the loading-error modal carries the graphic" fails
 *   - drop a caption's bind_flag_if_not_eq, or point it at the wrong ref_value
 *     -> "the fault position is named in words, not only in colour" fails
 */

#include "ui_afc_fault_path.h"
#include "ui_ams_loading_error_modal.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_types.h"
#include "error_event.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "recovery_modal_presenter.h"
#include "theme_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

namespace {

/// AFC.py:1345, exactly as it arrives — lane prefix, sentence, art, labels.
const std::string PRE_GEAR_FAULT =
    "lane1 filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH\n"
    "||=====||====||==>--||\n"
    "TRG   LOAD   HUB   TOOL";

const std::string PRE_GEAR_SENTENCE =
    "lane1 filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH";

/// Not one of AFC's five diagram-bearing faults.
const std::string UNKNOWN_FAULT = "!! Move out of range: 300.000 0.000 10.000 [0.000]";

int fault_segment() {
    lv_subject_t* s = lv_xml_get_subject(nullptr, "afc_fault_segment");
    REQUIRE(s != nullptr);
    return lv_subject_get_int(s);
}

/// The graphic is present in the tree but hides itself; "shown" means both.
bool graphic_visible(lv_obj_t* root) {
    lv_obj_t* g = lv_obj_find_by_name(root, "fault_path");
    return g != nullptr && !lv_obj_has_flag(g, LV_OBJ_FLAG_HIDDEN);
}

/// Every caption in afc_fault_path.xml, so "exactly one is visible" is checkable.
const char* const ALL_CAPTIONS[] = {
    "afc_stop_caption_spool",
    "afc_stop_caption_hub",
    "afc_stop_caption_output",
    "afc_stop_caption_toolhead",
};

helix::ErrorEvent make_event(const std::string& detail) {
    helix::ErrorEvent e;
    e.source = helix::ErrorSource::AFC;
    e.severity = helix::ErrorSeverity::CRITICAL;
    e.title = "Filament System Error";
    e.detail = detail;
    e.recovery_actions = {
        {"Resume", "RESUME", "afc::resume", "primary", /*needs_hot_nozzle=*/true},
    };
    return e;
}

/// Text of the first label the modal created for PromptData::text_lines.
std::string prompt_body_text(lv_obj_t* screen) {
    lv_obj_t* container = lv_obj_find_by_name(screen, "content_container");
    REQUIRE(container != nullptr);
    REQUIRE(lv_obj_get_child_count(container) > 0);
    return lv_label_get_text(lv_obj_get_child(container, 0));
}

} // namespace

// ============================================================================
// The subject, which is what the XML binds
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "afc_fault_path_apply publishes and clears the stop point",
                 "[afc][fault][modal]") {
    // OUTPUT (5) — past the hub, short of the toolhead.
    const std::string shown = helix::ui::afc_fault_path_apply(PRE_GEAR_FAULT);
    CHECK(fault_segment() == static_cast<int>(PathSegment::OUTPUT));
    CHECK(shown == PRE_GEAR_SENTENCE);

    // An unrecognised message must CLEAR the previous marker, not inherit it,
    // and must come back byte-for-byte.
    const std::string passthrough = helix::ui::afc_fault_path_apply(UNKNOWN_FAULT);
    CHECK(fault_segment() == 0);
    CHECK(passthrough == UNKNOWN_FAULT);
}

// ============================================================================
// The caption — the non-colour channel (#1196)
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "the fault position is named in words, not only in colour",
                 "[afc][fault][modal][1196]") {
    auto* graphic =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "afc_fault_path", nullptr));
    REQUIRE(graphic != nullptr);

    struct Case {
        const char* message;
        PathSegment segment;
        const char* caption_name;
        const char* caption_text;
    };

    // One per AFC fault position, message text as afc_fault_position() sees it.
    const Case cases[] = {
        {"lane1 FAILED TO LOAD, CHECK FILAMENT AT TRIGGER", PathSegment::SPOOL,
         "afc_stop_caption_spool", "Stopped between Spool and Lane"},
        {"lane1 filament did not trigger hub sensor, CHECK FILAMENT PATH", PathSegment::HUB,
         "afc_stop_caption_hub", "Stopped between Lane and Hub"},
        {"lane1 filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH",
         PathSegment::OUTPUT, "afc_stop_caption_output", "Stopped between Hub and Toolhead"},
        {"lane1 filament failed to trigger post extruder gear toolhead sensor, CHECK FILAMENT PATH",
         PathSegment::TOOLHEAD, "afc_stop_caption_toolhead", "Jammed at the Toolhead"},
    };

    for (const auto& c : cases) {
        INFO(c.message);
        helix::ui::afc_fault_path_apply(c.message);
        process_lvgl(10);
        REQUIRE(fault_segment() == static_cast<int>(c.segment));
        REQUIRE_FALSE(lv_obj_has_flag(graphic, LV_OBJ_FLAG_HIDDEN));

        int visible = 0;
        for (const char* name : ALL_CAPTIONS) {
            lv_obj_t* caption = lv_obj_find_by_name(graphic, name);
            REQUIRE(caption != nullptr);
            if (lv_obj_has_flag(caption, LV_OBJ_FLAG_HIDDEN)) {
                continue;
            }
            ++visible;
            CHECK(std::string(name) == c.caption_name);
            CHECK(std::string(lv_label_get_text(caption)) == c.caption_text);
        }
        // Two visible captions would contradict each other; none leaves the fault
        // legible in colour alone, which is the whole complaint.
        CHECK(visible == 1);
    }

    // Unrecognised message: the component hides, so no caption speaks for a fault
    // whose position we could not place.
    helix::ui::afc_fault_path_apply(UNKNOWN_FAULT);
    process_lvgl(10);
    CHECK(lv_obj_has_flag(graphic, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(graphic);
    process_lvgl(10);
}

// ============================================================================
// RecoveryModalPresenter -> ActionPromptModal (the `!!` path)
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture,
                 "the recovery modal shows the graphic for a recognised AFC fault",
                 "[afc][fault][modal]") {
    helix::ui::RecoveryModalPresenter presenter(api());

    presenter.present(make_event(PRE_GEAR_FAULT));
    process_lvgl(20);
    REQUIRE(presenter.is_visible());

    lv_obj_t* screen = lv_screen_active();
    CHECK(graphic_visible(screen));
    CHECK(fault_segment() == static_cast<int>(PathSegment::OUTPUT));
    // The art rows are gone; the sentence survives.
    CHECK(prompt_body_text(screen) == PRE_GEAR_SENTENCE);

    presenter.dismiss();
    process_lvgl(20);
}

TEST_CASE_METHOD(LVGLUITestFixture, "an unrecognised fault leaves no graphic behind",
                 "[afc][fault][modal]") {
    helix::ui::RecoveryModalPresenter presenter(api());

    // Show a recognised fault first, so "hidden" cannot pass vacuously.
    presenter.present(make_event(PRE_GEAR_FAULT));
    process_lvgl(20);
    REQUIRE(graphic_visible(lv_screen_active()));
    presenter.dismiss();
    process_lvgl(20);

    presenter.present(make_event(UNKNOWN_FAULT));
    process_lvgl(20);
    REQUIRE(presenter.is_visible());

    lv_obj_t* screen = lv_screen_active();
    CHECK_FALSE(graphic_visible(screen));
    // ...and the message is rendered exactly as it always was.
    CHECK(prompt_body_text(screen) == UNKNOWN_FAULT);

    presenter.dismiss();
    process_lvgl(20);
}

// ============================================================================
// AmsLoadingErrorModal (the AmsAction::ERROR path)
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "the loading-error modal carries the graphic",
                 "[afc][fault][modal]") {
    lv_xml_register_component_from_file("A:ui_xml/ams_loading_error_modal.xml");

    helix::ui::AmsLoadingErrorModal modal;

    // AmsPanel::show_loading_error_modal() runs the backend's operation_detail
    // through afc_fault_path_apply() before handing it to show().
    const std::string shown = helix::ui::afc_fault_path_apply(PRE_GEAR_FAULT);
    REQUIRE(modal.show(lv_screen_active(), shown, []() {}));
    process_lvgl(20);
    REQUIRE(modal.is_visible());

    CHECK(graphic_visible(modal.dialog()));
    CHECK(shown == PRE_GEAR_SENTENCE);

    modal.hide();
    process_lvgl(20);

    // Same modal, unrecognised message: the graphic stays down.
    const std::string plain = helix::ui::afc_fault_path_apply(UNKNOWN_FAULT);
    REQUIRE(modal.show(lv_screen_active(), plain, []() {}));
    process_lvgl(20);
    CHECK_FALSE(graphic_visible(modal.dialog()));

    modal.hide();
    process_lvgl(20);
}

// Moving the graphic out of the scrollable text area (so it never scrolls out of
// view) also moves it into the card's fixed height budget. On the smallest panel we
// ship that budget is tight: a card capped at 85% of 272px has to fit the header, the
// capped text area, the graphic, and the button row. #dialog_content_max is documented
// in globals.xml as bound to a header + divider + button row budget, so this modal is
// already over that allowance. The Klipper recovery dialog had the same shape and
// overflowed downward, clipping its last button, so pin the geometry rather than
// trusting the arithmetic.
//
// Both reachable shapes are covered: graphic + short AFC sentence, and long message
// with no graphic. They cannot combine — the graphic appears only for the five
// recognised AFC faults, all of which are one-line sentences.
//
// Mutation checks (measured, not assumed):
//   - raise the text area's style_max_height above #dialog_content_max (e.g. to 400)
//     -> the card clamps at its 85% cap and the button row lands 2px past the bottom
//        edge; this fails. This is the load-bearing guard at MICRO.
//   - dropping style_max_height from the card itself does NOT fail this test: with the
//     text area already capped at 140, chrome + 140 = 237 never reaches 272, so the
//     card cap is never hit here. It stays as the modal_dialog contract's backstop for
//     breakpoints where #dialog_content_max is large relative to the screen, which this
//     test does not cover.
TEST_CASE_METHOD(LVGLUITestFixture, "the loading-error modal fits a 480x272 panel",
                 "[afc][fault][modal]") {
    lv_xml_register_component_from_file("A:ui_xml/ams_loading_error_modal.xml");

    lv_display_t* disp = lv_display_get_default();
    REQUIRE(disp != nullptr);

    {
        ScopedResolution micro(disp, 480, 272);
        theme_manager_refresh_layout_constants(disp);

        // A recognised AFC fault, verbatim — the graphic only ever appears for those
        // five, and they are all one-line sentences. Padding this out to fill the
        // capped text area would be testing a state the product cannot reach; the
        // long-message risk is covered by the no-graphic case below, where the text
        // area is the only thing that can grow.
        const std::string body = helix::ui::afc_fault_path_apply(PRE_GEAR_FAULT);

        helix::ui::AmsLoadingErrorModal modal;
        REQUIRE(modal.show(lv_screen_active(), body, []() {}));
        process_lvgl(20);
        REQUIRE(modal.is_visible());
        CHECK(graphic_visible(modal.dialog()));

        lv_obj_t* card = modal.dialog();
        REQUIRE(card != nullptr);
        lv_obj_update_layout(card);

        // Absolute screen coords, not lv_obj_get_y() — that is parent-relative, so a
        // nested button's offset would compare meaninglessly against the card's
        // screen-space bottom and the assertion would pass no matter what.
        lv_area_t card_area;
        lv_obj_get_coords(card, &card_area);
        CAPTURE(card_area.y1, card_area.y2);
        CHECK(card_area.y1 >= 0);
        CHECK(card_area.y2 <= 272);

        // The button row is the last child, so it is what gets clipped first when the
        // card's contents outgrow the cap.
        lv_obj_t* primary = lv_obj_find_by_name(card, "btn_primary");
        REQUIRE(primary != nullptr);
        lv_obj_update_layout(primary);
        lv_area_t btn_area;
        lv_obj_get_coords(primary, &btn_area);
        CAPTURE(btn_area.y1, btn_area.y2);

        lv_obj_t* text_scroll = lv_obj_find_by_name(card, "ams_error_text_scroll");
        lv_obj_t* graphic = lv_obj_find_by_name(card, "fault_path");
        const int32_t text_h = text_scroll ? lv_obj_get_height(text_scroll) : -1;
        const int32_t graphic_h = graphic ? lv_obj_get_height(graphic) : -1;
        CAPTURE(text_h, graphic_h);

        CHECK(btn_area.y2 <= card_area.y2);

        modal.hide();
        process_lvgl(20);
    }

    // The other half of the budget: no graphic, but a message long enough to peg the
    // text area at #dialog_content_max. This is the case a non-AFC backend can
    // actually produce, and it is the one the card's style_max_height exists for.
    {
        ScopedResolution micro(disp, 480, 272);
        theme_manager_refresh_layout_constants(disp);

        helix::ui::afc_fault_path_apply(UNKNOWN_FAULT); // clears the graphic
        std::string body = "Filament load failed and the backend returned a long "
                           "explanation that has to wrap several times.";
        while (body.size() < 400) {
            body += " Retry the operation once the path is clear.";
        }

        helix::ui::AmsLoadingErrorModal modal;
        REQUIRE(modal.show(lv_screen_active(), body, []() {}));
        process_lvgl(20);
        REQUIRE(modal.is_visible());
        CHECK_FALSE(graphic_visible(modal.dialog()));

        lv_obj_t* card = modal.dialog();
        REQUIRE(card != nullptr);
        lv_obj_update_layout(card);

        lv_area_t card_area;
        lv_obj_get_coords(card, &card_area);
        CAPTURE(card_area.y1, card_area.y2);
        CHECK(card_area.y1 >= 0);
        CHECK(card_area.y2 <= 272);

        lv_obj_t* primary = lv_obj_find_by_name(card, "btn_primary");
        REQUIRE(primary != nullptr);
        lv_obj_update_layout(primary);
        lv_area_t btn_area;
        lv_obj_get_coords(primary, &btn_area);
        CAPTURE(btn_area.y1, btn_area.y2);
        CHECK(btn_area.y2 <= card_area.y2);

        modal.hide();
        process_lvgl(20);
    }

    // Put the token table back where the rest of the suite expects it.
    theme_manager_refresh_layout_constants(disp);
}
