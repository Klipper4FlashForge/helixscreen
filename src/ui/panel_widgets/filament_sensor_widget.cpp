// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_sensor_widget.h"

#include "ui_event_safety.h"
#include "ui_settings_sensors.h"

#include "app_globals.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "panel_widget_registry.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix {

void register_filament_sensor_widget() {
    FilamentSensorWidget::init_static_subjects();

    register_widget_factory(
        "filament", [](const std::string&) { return std::make_unique<FilamentSensorWidget>(); });

    lv_xml_register_event_cb(nullptr, "filament_widget_clicked_cb",
                             FilamentSensorWidget::clicked_cb);
}

void FilamentSensorWidget::init_static_subjects() {
    if (subjects_initialized_) {
        return;
    }
    subjects_initialized_ = true;

    // -1 = no sensor, matching FilamentSensorManager's encoding, so the tile
    // reads "hidden" until the first real value arrives rather than "empty".
    lv_subject_init_int(&tile_state_subject_, -1);
    lv_subject_init_int(&advisory_subject_, 0);

    // Global scope (not the "panel_widget_filament" component scope): the
    // mirror is also read as a `subject`-type prop by the nested
    // <filament_sensor_indicator> component, which resolves that prop via
    // lv_xml_get_subject() against its OWN scope, falling back only to
    // "globals" - never to the parent component's private scope. A
    // component-scoped registration here left that lookup unresolved (8x "No
    // subject was found" warnings) despite the same-component label bindings
    // resolving it fine, since those short-circuit against the local scope
    // before ever needing the fallback. lv_xml_register_subject(nullptr, ...)
    // routes to "globals" (lib/helix-xml/src/xml/lv_xml.c:689).
    lv_xml_register_subject(nullptr, "filament_tile_state", &tile_state_subject_);

    auto* modal_scope = lv_xml_component_get_scope("runout_guidance_modal");
    if (modal_scope) {
        lv_xml_register_subject(modal_scope, "runout_is_advisory", &advisory_subject_);
    } else {
        spdlog::warn("[FilamentSensorWidget] runout_guidance_modal scope not found");
    }

    StaticSubjectRegistry::instance().register_deinit("FilamentSensorWidget", []() {
        if (!subjects_initialized_) {
            return;
        }
        lv_subject_deinit(&tile_state_subject_);
        lv_subject_deinit(&advisory_subject_);
        subjects_initialized_ = false;
    });
}

FilamentSensorWidget::FilamentSensorWidget() = default;

FilamentSensorWidget::~FilamentSensorWidget() {
    detach();
}

void FilamentSensorWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    lv_obj_set_user_data(widget_obj_, this);

    // Instances are recycled across grid rebuilds, so the source binding must be
    // (re)established here, not only in set_config().
    rebind_source();
}

void FilamentSensorWidget::detach() {
    source_observer_.reset();
    lifetime_.invalidate();
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
    }
    widget_obj_ = nullptr;
    parent_screen_ = nullptr;
}

void FilamentSensorWidget::set_config(const nlohmann::json& config) {
    config_ = config;
    if (config.contains("source") && config["source"].is_string()) {
        source_ = ui::parse_tile_source(config["source"].get<std::string>());
    }
    spdlog::debug("[FilamentSensorWidget] Config: source={}", ui::tile_source_to_string(source_));
    rebind_source();
}

bool FilamentSensorWidget::on_edit_configure() {
    // The source picker (Auto/Runout/Toolhead/Entry) lands in a follow-up task;
    // the gear is present per has_edit_configure() but inert until then.
    return false;
}

void FilamentSensorWidget::clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[FilamentSensorWidget] clicked_cb");
    // Sole registration is the XML <event_cb> on the tile's root, which LVGL's
    // XML engine dispatches with user_data=NULL - recover the instance via
    // widget_obj_'s own user_data (set in attach()) instead. No parallel
    // lv_obj_add_event_cb() here: the root has no clickable child to target,
    // unlike MotionWidget's motion_button, so a second registration on the
    // same object would only double-dispatch every tap.
    auto* self = panel_widget_from_event<FilamentSensorWidget>(e);
    if (self) {
        self->record_interaction();
        self->handle_click();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void FilamentSensorWidget::handle_click() {
    const int sensor_state = lv_subject_get_int(&tile_state_subject_);
    const int print_state = static_cast<int>(get_printer_state().get_print_job_state());

    switch (ui::decide_tap_destination(sensor_state, print_state)) {
    case ui::FilamentTapDestination::None:
        spdlog::debug("[FilamentSensorWidget] Tap ignored - no sensor configured");
        return;
    case ui::FilamentTapDestination::SensorSettings:
        open_sensor_settings();
        return;
    case ui::FilamentTapDestination::ModalStatusOnly:
        show_tap_modal(/*status_only=*/true);
        return;
    case ui::FilamentTapDestination::ModalFull:
        show_tap_modal(/*status_only=*/false);
        return;
    }
}

void FilamentSensorWidget::open_sensor_settings() {
    if (!parent_screen_) {
        return;
    }
    helix::settings::get_sensor_settings_overlay().show(parent_screen_);
}

void FilamentSensorWidget::show_tap_modal(bool status_only) {
    if (!parent_screen_) {
        return;
    }
    lv_subject_set_int(&advisory_subject_, 1);

    // Override the runout copy. `title` and `message` are <prop>s on
    // runout_guidance_modal.xml, and Modal::show() forwards XML attrs, so the
    // runout path's defaults are untouched. NULL-terminated, as show() expects.
    // The manual row is hidden mid-print by the XML gate, so the default
    // "What would you like to do?" copy - and a plain "Load or unload" - both
    // read as offers the dialog is not making. Say what is actually true.
    const char* message = status_only ? lv_tr("Filament controls are unavailable while printing.")
                                      : lv_tr("Load or unload filament.");
    const char* attrs[] = {"title", lv_tr("Filament"), "message", message, nullptr};
    tap_modal_.show(parent_screen_, attrs);
}

void FilamentSensorWidget::rebind_source() {
    // Reset first: attach() runs again on every grid rebuild, and set_config()
    // can fire after attach(), so without this the observers stack up.
    source_observer_.reset();

    lv_subject_t* src = lv_xml_get_subject(nullptr, ui::tile_source_subject(source_));
    if (!src) {
        spdlog::warn("[FilamentSensorWidget] Subject '{}' not found - tile will not update",
                     ui::tile_source_subject(source_));
        return;
    }

    // Mirror the selected role subject into the one the XML binds. A runtime
    // source mux, not a compound condition, so <subject_expr> cannot express it.
    // DECLARATIVE_OK: the selection is user config, not a static relationship.
    source_observer_ = helix::ui::observe_int_sync<FilamentSensorWidget>(
        src, this,
        [](FilamentSensorWidget* self, int value) {
            (void)self;
            lv_subject_set_int(&tile_state_subject_, value);
        },
        source_lifetime_);
}

} // namespace helix
