// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task 3 slice driver: mirrors the Application::run() bootstrap phases
// (application.cpp:556-591) with the real registration/init calls, then
// creates the home panel from its real XML and pushes fake printer state
// through PrinterState. Fonts/translations soft-fall-back by design
// (lv_xml_get_font → LV_FONT_DEFAULT); the audit verifies structure +
// subject flow, not glyph fidelity.

#include "audit_app.h"

#include "app_globals.h"
#include "asset_manager.h"
#include "helix_sparkline.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "setting_group.h"
#include "static_subject_registry.h"
#include "subject_initializer.h"
#include "theme_manager.h"
#include "ui_ams_mini_status.h"
#include "ui_bed_mesh.h"
#include "ui_card.h"
#include "ui_component_header_bar.h"
#include "ui_dialog.h"
#include "ui_gcode_viewer.h"
#include "ui_gradient_canvas.h"
#include "ui_icon.h"
#include "ui_panel_home.h"
#include "ui_severity_card.h"
#include "ui_status_pill.h"
#include "ui_switch.h"
#include "ui_temp_display.h"
#include "ui_update_queue.h"
#include "xml_registration.h"

#include "src/xml/lv_xml.h"
#include "src/xml/lv_xml_component.h"

#include <spdlog/spdlog.h>

extern "C" void audit_app_run(void) {
    spdlog::info("audit_app: PrinterState subject pipeline");
    helix::ui::update_queue_init();
    helix::PrinterState& ps = get_printer_state();
    ps.init_subjects(true);

    // Phase 6 mirror: globals scope + theme consts (#screen_bg etc.)
    lv_result_t g = lv_xml_register_component_from_file("A:ui_xml/globals.xml");
    spdlog::info("audit_app: globals.xml {}", g == LV_RESULT_OK ? "OK" : "FAILED");
    // Fonts must be XML-registered before theme_manager_init: the responsive
    // font registrar drops any token whose face isn't linked (application.cpp
    // calls AssetManager::register_all() before theme init for the same
    // reason), and ui_text hard-aborts on a missing font_small.
    AssetManager::register_all();
    theme_manager_init(lv_display_get_default(), true);

    // Phase 7 mirror: custom widget registrations (application.cpp:1517)
    ui_icon_register_widget();
    ui_status_pill_register_widget();
    ui_switch_register();
    ui_card_register();
    setting_group_register();
    ui_temp_display_init();
    ui_ams_mini_status_init();
    ui_severity_card_register();
    ui_dialog_register();
    ui_bed_mesh_register();
    ui_gcode_viewer_register();
    ui_gradient_canvas_register();
    helix::ui::register_helix_sparkline_widget();
    ui_component_header_bar_init();

    // Phase 8c mirror: all XML components from the LittleFS image
    helix::register_xml_components();

    // Phase 9 mirror: full subject initialization. API is nullptr — panels
    // store the pointer and only call through it on user actions; the audit
    // drives state via PrinterState setters (like --test mode, minus libhv).
    static RuntimeConfig rc;
    static SubjectInitializer subjects;
    subjects.init_core_and_state();
    subjects.init_panels(nullptr, rc);
    subjects.init_post(rc);
    spdlog::info("audit_app: subjects initialized");

    // The real panel, from its real XML.
    HomePanel& hp = get_global_home_panel();
    lv_obj_t* panel = (lv_obj_t*)lv_xml_create(lv_screen_active(), "home_panel", nullptr);
    if (!panel) {
        spdlog::error("audit_app: home_panel XML create FAILED");
        return;
    }
    hp.setup(panel, lv_screen_active());
    hp.finalize_setup();
    spdlog::info("audit_app: home panel created and finalized");
}
