// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_widget_m117.cpp
 * @brief Widget-level regression cover for Klipper M117 (display_status.message)
 *        visibility in panel_widget_print_status.xml.
 *
 * Task 4 added the `idle_display_message` (print_card_idle) and `display_message`
 * (print_card_printing, view-3-scoped) rows verified only by an XML parse check
 * and a lint pass. Nothing asserted they actually render in the right views —
 * which is exactly the gap that let a clipped, dead M117 copy exist undetected
 * inside print_status_detailed_active (view 4) for an unknown span of time.
 *
 * Both tests drive the message through PrinterState::update_from_status(), the
 * real production entry point (never by poking the display_message subject
 * directly), and drain the UpdateQueue before every assertion since the
 * bind_flag_if / bind_flag_if_eq observers that translate subject changes into
 * LV_OBJ_FLAG_HIDDEN are deferred.
 *
 * Teardown follows the pattern documented in test_print_status_widget_recycle.cpp:
 * the widget lives in a nested scope so its destructor (and the per-instance
 * PrinterState subjects it observes) runs before PrintStatusWidget::destroy_
 * formatter_for_test(), and captured bools are asserted only after that
 * cleanup — a future regression fails cleanly instead of segfaulting the
 * whole suite in teardown.
 */

#include "src/ui/panel_widgets/print_status_widget.h"

#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"

#include "printer_state.h"
#include "ui_update_queue.h"

#include "../catch_amalgamated.hpp"
#include "../lvgl_ui_test_fixture.h"

using namespace helix;
using helix::ui::UpdateQueue;

namespace {

lv_obj_t* make_print_status(lv_obj_t* parent) {
    return static_cast<lv_obj_t*>(lv_xml_create(parent, "panel_widget_print_status", nullptr));
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "print_status idle M117 row tracks display_message subject",
                 "[print_status][panel_widget][m117]") {
    bool hidden_when_empty = false;
    bool hidden_after_message = true;
    bool hidden_after_clear = false;
    {
        // Construct the widget first so print_status_view/print_status_colspan
        // are registered before the component XML is parsed — helix-xml skips
        // bindings whose subject is absent at parse time (see recycle test).
        PrintStatusWidget widget;
        widget.set_config({{"layout_style", "library"}});

        lv_obj_t* comp = make_print_status(test_screen());
        REQUIRE(comp != nullptr);
        widget.attach(comp, test_screen());
        process_lvgl(50); // drain attach-time observer callbacks

        lv_obj_t* idle_msg = lv_obj_find_by_name(comp, "idle_display_message");
        REQUIRE(idle_msg != nullptr);

        // Empty at attach — the widget never sent a message yet.
        hidden_when_empty = lv_obj_has_flag(idle_msg, LV_OBJ_FLAG_HIDDEN);

        state().update_from_status({{"display_status", {{"message", "Heating bed..."}}}});
        UpdateQueue::instance().drain();
        hidden_after_message = lv_obj_has_flag(idle_msg, LV_OBJ_FLAG_HIDDEN);

        // Klipper clears M117 by sending a null message.
        state().update_from_status({{"display_status", {{"message", nullptr}}}});
        UpdateQueue::instance().drain();
        hidden_after_clear = lv_obj_has_flag(idle_msg, LV_OBJ_FLAG_HIDDEN);
    }
    PrintStatusWidget::destroy_formatter_for_test();

    REQUIRE(hidden_when_empty);
    REQUIRE_FALSE(hidden_after_message);
    REQUIRE(hidden_after_clear);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "print_status active M117 row is scoped to view==3 (active_library)",
                 "[print_status][panel_widget][m117]") {
    bool hidden_before_active = false;
    bool hidden_while_active_library = true;
    bool hidden_after_clear_while_active = false;
    int view_before_active = -1;
    int view_while_active = -1;
    int view_after_clear = -1;
    {
        PrintStatusWidget widget;
        widget.set_config({{"layout_style", "library"}});
        widget.on_size_changed(2, 2, 400, 400);

        lv_obj_t* comp = make_print_status(test_screen());
        REQUIRE(comp != nullptr);
        widget.attach(comp, test_screen());
        process_lvgl(50);

        lv_obj_t* printing_card = lv_obj_find_by_name(comp, "print_card_printing");
        REQUIRE(printing_card != nullptr);
        lv_obj_t* active_msg = lv_obj_find_by_name(printing_card, "display_message");
        REQUIRE(active_msg != nullptr);

        // Non-empty message the whole time — only the view changes. This is
        // the assertion that would have caught the dead view-4 copy: a
        // binding that ignored print_status_view would leave this row
        // visible even while idle (view != 3).
        state().update_from_status({{"display_status", {{"message", "Heating bed..."}}}});
        UpdateQueue::instance().drain();

        view_before_active = lv_subject_get_int(PrintStatusWidget::view_subject_for_test());
        hidden_before_active = lv_obj_has_flag(active_msg, LV_OBJ_FLAG_HIDDEN);

        widget.on_print_state_changed_for_test(PrintJobState::PRINTING);
        UpdateQueue::instance().drain();

        view_while_active = lv_subject_get_int(PrintStatusWidget::view_subject_for_test());
        hidden_while_active_library = lv_obj_has_flag(active_msg, LV_OBJ_FLAG_HIDDEN);

        // Second half of the compound condition
        // ("print_status_view ne 3 or display_message_visible eq 0"): stay in
        // view 3 and clear M117 the way Klipper does (null message). Without
        // this case the `display_message_visible eq 0` clause is dead — the
        // message is non-empty for the whole test above, so deleting the clause
        // leaves every other assertion green.
        state().update_from_status({{"display_status", {{"message", nullptr}}}});
        UpdateQueue::instance().drain();

        view_after_clear = lv_subject_get_int(PrintStatusWidget::view_subject_for_test());
        hidden_after_clear_while_active = lv_obj_has_flag(active_msg, LV_OBJ_FLAG_HIDDEN);
    }
    PrintStatusWidget::destroy_formatter_for_test();

    REQUIRE(view_before_active != 3);
    REQUIRE(hidden_before_active);

    REQUIRE(view_while_active == 3);
    REQUIRE_FALSE(hidden_while_active_library);

    // Still the active library view, but the message is gone -> row hides.
    REQUIRE(view_after_clear == 3);
    REQUIRE(hidden_after_clear_while_active);
}
