// SPDX-License-Identifier: GPL-3.0-or-later
//
// TEMPORARY link stubs for the Moonraker sub-APIs deferred to Task 10 (the
// hv/requests.h HTTP TUs and the concrete libhv transport). Each symbol here is
// referenced by a kept app TU but defined in a source file excluded from this
// component (see app_srcs.txt "EXCLUDED, and why"). Bodies ESP_LOGE + abort:
// nothing on the idle hello-card path calls them, and Task 10 replaces this
// whole file with the real esp_http_client / esp_websocket_client ports.
//
// REMOVE IN TASK 10.
//
// Symbols are added here exactly as the linker demands them; each carries the
// excluded source file it comes from. The full inventory is mirrored in
// esp32p4-task-5-report.md.

#include "esp_log.h"

#include "moonraker_api.h"
#include "moonraker_file_transfer_api.h"
#include "moonraker_rest_api.h"
#include "moonraker_timelapse_api.h"

#include <cstdlib>

namespace {
[[maybe_unused]] [[noreturn]] void task10_unimplemented(const char* sym) {
    ESP_LOGE("helixapp",
             "task10_pending_stubs: %s called — Moonraker HTTP/transport lands in "
             "Task 10; this path must not run on the idle bring-up build",
             sym);
    abort();
}
} // namespace

// ===========================================================================
// GROUP A — Moonraker HTTP/transport surface deferred to Task 10.
// ===========================================================================

// ---------------------------------------------------------------------------
// MoonrakerAPI facade control methods.
// Real definitions live in the EXCLUDED src/api/moonraker_api_controls.cpp
// (one of the 5 hv/requests.h HTTP TUs). moonraker_api.cpp (KEPT) declares the
// facade; these command entry points are only reachable through user actions
// that cannot occur on the idle Stage-A bring-up build, so abort-if-called.
// The parameter typedefs (SuccessCallback, ErrorCallback, PowerDevicesCallback,
// SensorsCallback) resolve in MoonrakerAPI's inherited scope, guaranteeing the
// definitions mangle identically to the header declarations.
// ---------------------------------------------------------------------------

void MoonrakerAPI::set_temperature(const std::string&, double, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::set_temperature");
}

void MoonrakerAPI::set_fan_speed(const std::string&, double, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::set_fan_speed");
}

void MoonrakerAPI::get_power_devices(PowerDevicesCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::get_power_devices");
}

void MoonrakerAPI::set_device_power(const std::string&, const std::string&, SuccessCallback,
                                    ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::set_device_power");
}

void MoonrakerAPI::get_sensors(SensorsCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::get_sensors");
}

void MoonrakerAPI::execute_gcode(const std::string&, SuccessCallback, ErrorCallback, uint32_t,
                                 bool) {
    task10_unimplemented("MoonrakerAPI::execute_gcode");
}

void MoonrakerAPI::exclude_object(const std::string&, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::exclude_object");
}

void MoonrakerAPI::emergency_stop(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::emergency_stop");
}

void MoonrakerAPI::restart_firmware(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::restart_firmware");
}

void MoonrakerAPI::restart_klipper(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::restart_klipper");
}

void MoonrakerAPI::restart_service(const std::string&, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::restart_service");
}

void MoonrakerAPI::restart_moonraker(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::restart_moonraker");
}

void MoonrakerAPI::machine_shutdown(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::machine_shutdown");
}

void MoonrakerAPI::machine_reboot(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::machine_reboot");
}

void MoonrakerAPI::update_safety_limits_from_printer(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerAPI::update_safety_limits_from_printer");
}

// ---------------------------------------------------------------------------
// Moonraker sub-API classes (rest_api_ / file_transfer_api_ / timelapse_api_).
// moonraker_api.cpp (KEPT) constructs each as a std::unique_ptr member via
// make_unique and destroys them in ~MoonrakerAPI. Because each class OVERRIDES
// its interface's pure virtuals, a bare ctor stub is NOT enough: the ctor emits
// a reference to the class vtable, whose key function (the out-of-line dtor) and
// every virtual slot live in the EXCLUDED src/api/moonraker_{rest,file_transfer,
// timelapse}_api.cpp. So we must define ctor + dtor + ALL virtuals here to let
// the compiler emit each vtable in this TU with every slot resolved.
//
//   ctor : real no-op — runs during MoonrakerAPI construction on boot. Only
//          initializes the two reference members (client_, http_base_url_).
//   dtor : real no-op — runs during MoonrakerAPI destruction on shutdown.
//   virtuals : abort-if-called — no HTTP transport runs on the idle path.
//
// Callback-parameter typedefs resolve in each class's inherited scope.
// ---------------------------------------------------------------------------

// --- MoonrakerRestAPI (src/api/moonraker_rest_api.cpp) ---
MoonrakerRestAPI::MoonrakerRestAPI(helix::IMoonrakerClient& client,
                                   const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

MoonrakerRestAPI::~MoonrakerRestAPI() {}

void MoonrakerRestAPI::call_rest_get(const std::string&, RestCallback) {
    task10_unimplemented("MoonrakerRestAPI::call_rest_get");
}

void MoonrakerRestAPI::call_rest_post(const std::string&, const json&, RestCallback) {
    task10_unimplemented("MoonrakerRestAPI::call_rest_post");
}

void MoonrakerRestAPI::wled_get_strips(RestCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerRestAPI::wled_get_strips");
}

void MoonrakerRestAPI::wled_set_strip(const std::string&, const std::string&, int, int,
                                      SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerRestAPI::wled_set_strip");
}

void MoonrakerRestAPI::wled_get_status(RestCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerRestAPI::wled_get_status");
}

void MoonrakerRestAPI::get_server_config(RestCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerRestAPI::get_server_config");
}

// --- MoonrakerFileTransferAPI (src/api/moonraker_file_transfer_api.cpp) ---
MoonrakerFileTransferAPI::MoonrakerFileTransferAPI(helix::IMoonrakerClient& client,
                                                   const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

MoonrakerFileTransferAPI::~MoonrakerFileTransferAPI() {}

void MoonrakerFileTransferAPI::download_file(const std::string&, const std::string&, StringCallback,
                                             ErrorCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::download_file");
}

void MoonrakerFileTransferAPI::download_file_partial(const std::string&, const std::string&, size_t,
                                                     StringCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::download_file_partial");
}

void MoonrakerFileTransferAPI::download_file_to_path(const std::string&, const std::string&,
                                                     const std::string&, StringCallback,
                                                     ErrorCallback, ProgressCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::download_file_to_path");
}

void MoonrakerFileTransferAPI::download_thumbnail(const std::string&, const std::string&,
                                                  StringCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::download_thumbnail");
}

void MoonrakerFileTransferAPI::upload_file(const std::string&, const std::string&,
                                           const std::string&, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::upload_file");
}

void MoonrakerFileTransferAPI::upload_file_with_name(const std::string&, const std::string&,
                                                     const std::string&, const std::string&,
                                                     SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::upload_file_with_name");
}

void MoonrakerFileTransferAPI::upload_file_from_path(const std::string&, const std::string&,
                                                     const std::string&, SuccessCallback,
                                                     ErrorCallback, ProgressCallback) {
    task10_unimplemented("MoonrakerFileTransferAPI::upload_file_from_path");
}

// --- MoonrakerTimelapseAPI (src/api/moonraker_timelapse_api.cpp) ---
MoonrakerTimelapseAPI::MoonrakerTimelapseAPI(helix::IMoonrakerClient& client,
                                             const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

MoonrakerTimelapseAPI::~MoonrakerTimelapseAPI() {}

void MoonrakerTimelapseAPI::get_timelapse_settings(TimelapseSettingsCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::get_timelapse_settings");
}

void MoonrakerTimelapseAPI::set_timelapse_settings(const TimelapseSettings&, SuccessCallback,
                                                   ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::set_timelapse_settings");
}

void MoonrakerTimelapseAPI::set_timelapse_enabled(bool, SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::set_timelapse_enabled");
}

void MoonrakerTimelapseAPI::render_timelapse(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::render_timelapse");
}

void MoonrakerTimelapseAPI::save_timelapse_frames(SuccessCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::save_timelapse_frames");
}

void MoonrakerTimelapseAPI::get_last_frame_info(std::function<void(const LastFrameInfo&)>,
                                                ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::get_last_frame_info");
}

void MoonrakerTimelapseAPI::get_webcam_list(WebcamListCallback, ErrorCallback) {
    task10_unimplemented("MoonrakerTimelapseAPI::get_webcam_list");
}

// (further Task 10 HTTP sub-API symbols appended here as the link demands them)
