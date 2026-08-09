// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// An ActionPromptModal button with an empty gcode means DO NOTHING.
//
// It used to mean "send the LABEL as gcode" (action_prompt_modal.cpp's
// `btn.gcode.empty() ? btn.label : btn.gcode`), so a button marked "OK" or
// "Dismiss" transmitted `OK` to Klipper (#1172). Both in-tree dismiss
// affordances worked around it by smuggling a Klipper comment through as the
// gcode — `"; error-dismiss"` and `"; qidi-blocked-dismiss"` — which is
// exactly the tribal knowledge a silent fallback creates.
//
// Klipper's own `action:prompt_button` protocol genuinely does default gcode
// to the label, but ActionPromptManager::parse_button_spec() applies that
// convention explicitly before the data ever reaches the modal, so wire
// prompts are unaffected by dropping the fallback here.

#include "ui_modal.h"

#include "../lvgl_ui_test_fixture.h"
#include "action_prompt_manager.h"
#include "action_prompt_modal.h"
#include "display_settings_manager.h"

#include <functional>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

class DismissFixture : public LVGLUITestFixture {
  public:
    DismissFixture() {
        prev_animations_ = helix::DisplaySettingsManager::instance().get_animations_enabled();
        helix::DisplaySettingsManager::instance().set_animations_enabled(false);
        modal_.set_gcode_callback([this](const std::string& g) { sent_.push_back(g); });
    }
    ~DismissFixture() override {
        helix::DisplaySettingsManager::instance().set_animations_enabled(prev_animations_);
    }

    /// Depth-first search for the nth lv_button in the dialog tree. The modal's
    /// buttons are nameless, so type + order is the only handle.
    lv_obj_t* nth_button(size_t n) {
        std::vector<lv_obj_t*> found;
        std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* node) {
            if (!node)
                return;
            if (lv_obj_check_type(node, &lv_button_class))
                found.push_back(node);
            uint32_t count = lv_obj_get_child_count(node);
            for (uint32_t i = 0; i < count; ++i)
                walk(lv_obj_get_child(node, i));
        };
        walk(modal_.dialog());
        return n < found.size() ? found[n] : nullptr;
    }

    helix::ui::ActionPromptModal modal_;
    std::vector<std::string> sent_;
    bool prev_animations_ = true;
};

helix::PromptData two_button_prompt() {
    helix::PromptData data;
    data.title = "Printer Error";
    data.text_lines.push_back("Unattributed fault");

    helix::PromptButton resume;
    resume.label = "Resume";
    resume.gcode = "RESUME";
    data.buttons.push_back(std::move(resume));

    // The dismiss affordance: a label, deliberately no gcode.
    helix::PromptButton dismiss;
    dismiss.label = "OK";
    data.buttons.push_back(std::move(dismiss));

    return data;
}

} // namespace

TEST_CASE_METHOD(DismissFixture, "a button with no gcode sends nothing and closes",
                 "[action_prompt][1172][ui_integration]") {
    REQUIRE(modal_.show_prompt(test_screen(), two_button_prompt()));

    lv_obj_t* dismiss = nth_button(1);
    REQUIRE(dismiss != nullptr);

    lv_obj_send_event(dismiss, LV_EVENT_CLICKED, nullptr);
    process_lvgl(40);

    // The label must NOT have been sent as a command — `OK` is not gcode.
    CHECK(sent_.empty());
    CHECK_FALSE(modal_.is_visible());
}

TEST_CASE_METHOD(DismissFixture, "a button with a gcode still sends it",
                 "[action_prompt][1172][ui_integration]") {
    // The other side of the branch: dropping the fallback must not make real
    // actions inert.
    REQUIRE(modal_.show_prompt(test_screen(), two_button_prompt()));

    lv_obj_t* resume = nth_button(0);
    REQUIRE(resume != nullptr);

    lv_obj_send_event(resume, LV_EVENT_CLICKED, nullptr);
    process_lvgl(40);

    REQUIRE(sent_.size() == 1);
    CHECK(sent_[0] == "RESUME");
    CHECK_FALSE(modal_.is_visible());
}
