// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "panel_widget.h"
#include "theme_manager.h"

#include <string>
#include <utility>

#include "../catch_amalgamated.hpp"

namespace helix {

/// Creates a widget's real XML component, attaches the widget to it, and
/// drives on_size_changed().
///
/// Variadic over the constructor so one harness covers every shape in the set:
/// default-constructed, MoonrakerAPI*, PrinterState&, (string, PrinterState&),
/// and instance-id string.
template <typename W> class PanelWidgetHarness {
  public:
    template <typename... Args>
    explicit PanelWidgetHarness(lv_obj_t* screen, Args&&... args)
        : widget_(std::forward<Args>(args)...) {
        // From the widget, not "panel_widget_" + id(): fan_stack selects between
        // stack and carousel components, and favorite_macro's id carries an
        // instance suffix that would produce an unregistered component name.
        const std::string component = widget_.get_component_name();
        obj_ = static_cast<lv_obj_t*>(lv_xml_create(screen, component.c_str(), nullptr));
        REQUIRE(obj_ != nullptr);
        widget_.attach(obj_, screen);
    }

    ~PanelWidgetHarness() {
        widget_.detach();
    }

    PanelWidgetHarness(const PanelWidgetHarness&) = delete;
    PanelWidgetHarness& operator=(const PanelWidgetHarness&) = delete;

    /// Drive a size change and settle the layout. Does not pump timers; a test
    /// that needs those calls process_lvgl() itself.
    void resize(int colspan, int rowspan, int width_px, int height_px) {
        widget_.on_size_changed(colspan, rowspan, width_px, height_px);
        lv_obj_update_layout(obj_);
    }

    lv_obj_t* child(const char* name) {
        return lv_obj_find_by_name(obj_, name);
    }
    W& widget() {
        return widget_;
    }
    lv_obj_t* root() {
        return obj_;
    }

  private:
    W widget_;
    lv_obj_t* obj_ = nullptr;
};

/// Fails when font tokens are not resolving.
///
/// theme_manager_get_font() falls back to lv_font_get_default() on any miss, so
/// with an uninitialized theme every token returns the same pointer and both
/// branches of a font assertion compare equal while proving nothing. Call this
/// before any font assertion.
inline void require_font_tokens_distinct() {
    REQUIRE(theme_manager_get_font("font_xs") != theme_manager_get_font("font_body"));
}

} // namespace helix
