// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plan 4 Task 6 — the real HelixScreen shell booting on the K-Touch.
//
// app_boot_ui() mirrors the desktop Application startup phases (see
// src/application/application.cpp Application::run() / init_ui()) with the real
// registration/init calls, minus SDL/CLI/desktop-only seams. The canonical
// desktop order is reproduced: asset root → Config → fonts → globals.xml →
// theme → widgets → translations → XML components → core subjects →
// MoonrakerManager (ESP factory arm) → panel subjects → app shell. It runs
// once on the UI pthread (created in app_main before any network task) and
// leaves the navbar + six resident panels live on the active screen.
//
// Divergences from desktop (documented in esp32p4-task-6-report.md):
//   * Config storage is injected explicitly (set_storage) to the writable
//     /config LittleFS partition — the /assets container is read-only frogfs.
//   * fonts are registered via helix_fonts_register() (the medium-tier ESP
//     face set, link-anchored in main/CMakeLists.txt) in addition to
//     AssetManager::register_all(), which registers the full token set backed
//     by the aliases in font_aliases.cpp.
//   * No connect() for real (non-mock) builds: WiFi lands in Task 13, so the
//     shell comes up in the not-ready/connecting UI, which is product-correct.
//   * CONFIG_HELIX_MOCK_PRINTER drives a firmware-local synthetic PrinterState
//     driver (below), NOT the app-layer MoonrakerClientMock — that mock
//     inherits the libhv-based concrete MoonrakerClient, whose transport base
//     is intentionally undefined on ESP32 (see helixnet/helixapp shims), so it
//     cannot be constructed on-device. The synthetic driver feeds the same
//     production PrinterState::update_from_status() path the real notify stream
//     would, with zero network.

#include "app_boot.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_globals.h"
#include "asset_manager.h"
#include "config.h"
#include "config_storage.h"
#include "connection_state.h"
#include "data_root_resolver.h"
#include "helix_sparkline.h"
#include "moonraker_api.h" // complete MoonrakerAPI : IMoonrakerAPI for the init_panels upcast
#include "moonraker_manager.h"
#include "panel_factory.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "setting_group.h"
#include "subject_initializer.h"
#include "theme_manager.h"
#include "translation_loader.h"
#include "ui_ams_mini_status.h"
#include "ui_bed_mesh.h"
#include "ui_card.h"
#include "ui_component_header_bar.h"
#include "ui_dialog.h"
#include "ui_gcode_viewer.h"
#include "ui_gradient_canvas.h"
#include "ui_icon.h"
#include "ui_nav_manager.h"
#include "ui_panel_home.h"
#include "ui_severity_card.h"
#include "ui_status_pill.h"
#include "ui_switch.h"
#include "ui_temp_display.h"
#include "ui_update_queue.h"
#include "xml_registration.h"

#include "src/xml/lv_xml.h"

#include <spdlog/spdlog.h>

#include <string>

// Defined in the main component (main/font_registration.c). Declared locally
// rather than via its header: that header lives under main/, which cannot be
// on helixapp's include path (main REQUIRES helixapp — the reverse include
// would be circular). The symbol resolves at the final whole-image link, and
// is kept alive by -Wl,--undefined=helix_fonts_register in main/CMakeLists.txt.
extern "C" void helix_fonts_register(void);

static const char* TAG = "app_boot";

namespace {

// The MoonrakerManager owns the client + API for the process lifetime. Held at
// file scope so app_boot_tick() can pump its notification/timeout queues from
// the render loop. Set once app_boot_ui() completes; null before then.
MoonrakerManager* g_manager = nullptr;

// One-shot boot heap milestone. heap_caps_get_largest_free_block() walks the
// heap in a critical section, so this is called only at discrete boot
// milestones — never from the steady-state render loop (see the audit's
// log_heap vs log_heap_fast note).
void log_heap_milestone(const char* stage) {
    ESP_LOGI(TAG, "[heap:%s] internal free=%u largest=%u | psram free=%u largest=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

// Register the custom C++ widgets the XML components extend. Same set as the
// desktop register_widgets() phase (application.cpp:1517) minus the subsystems
// gated off for v1 (camera/gcode-3D/etc.). ui_gcode_viewer_register() resolves
// to the no-op #else branch in ui_gcode_viewer.cpp on ESP (HELIX_HAS_GCODE_
// VIEWER=0) — it still registers a stub <gcode_viewer> widget so XML that names
// it parses.
void register_widgets() {
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
}

// Build the app shell: app_layout.xml instantiates the navbar and all six
// panels resident-and-hidden (the desktop memory model), then PanelFactory
// finds + wires them. Mirrors Application::init_ui() (application.cpp:1721).
// Returns false on any structural failure (logged).
bool build_shell() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* app_layout = static_cast<lv_obj_t*>(lv_xml_create(screen, "app_layout", nullptr));
    if (!app_layout) {
        spdlog::error("app_boot: app_layout XML create FAILED");
        return false;
    }
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(screen);
    NavigationManager::instance().set_app_layout(app_layout);

    lv_obj_t* navbar = lv_obj_find_by_name(app_layout, "navbar");
    lv_obj_t* content_area = lv_obj_find_by_name(app_layout, "content_area");
    if (!navbar || !content_area) {
        spdlog::error("app_boot: navbar/content_area not found in app_layout");
        return false;
    }
    NavigationManager::instance().wire_events(navbar);

    lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "panel_container");
    if (!panel_container) {
        spdlog::error("app_boot: panel_container not found");
        return false;
    }
    static helix::PanelFactory panels;
    if (!panels.find_panels(panel_container)) {
        spdlog::error("app_boot: find_panels FAILED");
        return false;
    }
    panels.setup_panels(screen);
    get_global_home_panel().finalize_setup();
    return true;
}

#if CONFIG_HELIX_MOCK_PRINTER
// Firmware-local synthetic printer, gated behind CONFIG_HELIX_MOCK_PRINTER.
// Feeds oscillating temperatures through the same production status-apply path
// (PrinterState::update_from_status) the real Moonraker notify stream uses, so
// the home panel shows live-looking data with no network and no printer. This
// is the ESP substitute for the desktop MoonrakerClientMock (unconstructible
// here — see file header).
void mock_seed_ready() {
    helix::PrinterState& ps = get_printer_state();
    ps.set_klippy_state_sync(helix::KlippyState::READY);
    ps.set_printer_connection_state(static_cast<int>(helix::ConnectionState::CONNECTED),
                                    "Mock printer");
    spdlog::info("app_boot: mock printer seeded READY/CONNECTED (CONFIG_HELIX_MOCK_PRINTER)");
}

void mock_push_temps() {
    static int t = 0;
    ++t;
    // Gentle sine-free oscillation (integer, no libm) so the readout visibly
    // moves without pretending to be a real heater curve.
    double nozzle = 205.0 + (t % 20);
    double bed = 58.0 + (t % 5);
    nlohmann::json status = {
        {"extruder", {{"temperature", nozzle}, {"target", 215.0}}},
        {"heater_bed", {{"temperature", bed}, {"target", 60.0}}},
    };
    get_printer_state().update_from_status(status);
}
#endif // CONFIG_HELIX_MOCK_PRINTER

} // namespace

extern "C" void app_boot_ui(void) {
    log_heap_milestone("boot-ui-start");

    // Phase 1: asset root + writable config storage. The packed /assets frogfs
    // container is read-only; settings live on the /config LittleFS partition.
    helix::set_asset_root("/assets");
    helix::Config* config = helix::Config::get_instance();
    config->set_storage(helix::make_file_config_storage("/config/settings.json"));
    config->init("/config/settings.json");

    // Phase 2: RuntimeConfig from build config (no CLI). g_runtime_config is a
    // process-global; MoonrakerManager + SubjectInitializer read it back.
    RuntimeConfig& rc = *get_runtime_config();
#if CONFIG_HELIX_MOCK_PRINTER
    // NOTE: we deliberately do NOT set rc.test_mode here. test_mode would route
    // MoonrakerManager through the app-layer mock arms, which construct the
    // libhv-based MoonrakerClientMock/MoonrakerAPIMock — unlinkable on ESP32.
    // The synthetic driver above provides mock data instead.
#endif

    // Phase 3: UI update queue (registers its high-priority drain timer), then
    // fonts BEFORE theme init. helix_fonts_register() is the link-anchored
    // medium-tier face set; AssetManager::register_all() adds the full token
    // set (aliased) + images. Both must precede globals.xml/theme — the
    // responsive font registrar hard-aborts on an unresolved font token.
    helix::ui::update_queue_init();
    helix_fonts_register();
    AssetManager::register_all();

    // Phase 4: globals scope (theme consts, font tokens) then theme init.
    std::string globals = "A:" + helix::asset_path("ui_xml/globals.xml");
    if (lv_xml_register_component_from_file(globals.c_str()) != LV_RESULT_OK) {
        spdlog::error("app_boot: globals.xml register FAILED ({})", globals);
    }
    theme_manager_init(lv_display_get_default(), true);
    log_heap_milestone("theme-up");

    // Phase 5: custom widgets.
    register_widgets();

    // Phase 6: translations for the active locale (before XML create — layout
    // + bindings resolve lv_tr() strings).
    std::string lang = config->get_language();
    helix::ui::ensure_translation_loaded(lang);
    lv_translation_set_language(lang.c_str());

    // Phase 7: all XML components from the /assets container.
    helix::register_xml_components();

    // Phase 8: core subjects (PrinterState / AmsState).
    static SubjectInitializer subjects;
    subjects.init_core_and_state();

    // Phase 9: MoonrakerManager — ESP factory arm builds EspMoonrakerClient +
    // the real MoonrakerAPI over it. init() creates client + API; it does NOT
    // connect (WiFi is Task 13). In mock builds the client stays idle and the
    // synthetic driver supplies data.
    static MoonrakerManager manager;
    manager.init(rc, config);
    set_moonraker_manager(&manager);
    g_manager = &manager;

    // Phase 10: panel subjects, now that the API pointer exists.
    subjects.init_panels(manager.api(), rc);
    subjects.init_post(rc);
    log_heap_milestone("subjects-up");

    // Phase 11: the app shell (navbar + six resident panels).
    if (!build_shell()) {
        spdlog::error("app_boot: shell build failed — UI incomplete");
        return;
    }

#if CONFIG_HELIX_MOCK_PRINTER
    mock_seed_ready();
#endif

    ESP_LOGI(TAG, "helix: home panel up");
    log_heap_milestone("home-panel-up");
}

extern "C" void app_boot_tick(void) {
    if (g_manager) {
        // Drain queued Moonraker notifications + request timeouts on the UI
        // thread — the work Application does each main-loop iteration.
        g_manager->process_notifications();
        g_manager->process_timeouts();
    }
#if CONFIG_HELIX_MOCK_PRINTER
    // ~1 Hz synthetic temperature push. tick fires every render iteration
    // (5-50ms); gate to roughly once a second.
    static int64_t last = 0;
    int64_t now = esp_timer_get_time();
    if (now - last > 1000000) {
        last = now;
        mock_push_temps();
    }
#endif
}
