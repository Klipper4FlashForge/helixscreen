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
#include "i_moonraker_api.h"

#include <cstdlib>
#include <string>

namespace {
[[maybe_unused]] [[noreturn]] void task10_unimplemented(const char* sym) {
    ESP_LOGE("helixapp",
             "task10_pending_stubs: %s called — Moonraker HTTP/transport lands in "
             "Task 10; this path must not run on the idle bring-up build",
             sym);
    abort();
}
} // namespace

// IMoonrakerAPI::is_safe_gcode_param — a pure gcode-safety validator whose sole
// definition lives in src/api/moonraker_api_controls.cpp (excluded here: it
// includes hv/requests.h, Task 10 scope). Called by the AFC/HappyHare AMS
// backends to vet gcode params. Conservative default: reject. Task 10 restores
// the real validator with the controls TU. NOT abort() — it is on a normal AMS
// code path, and rejecting is the safe answer if it ever runs pre-Task-10.
bool IMoonrakerAPI::is_safe_gcode_param(const std::string& /*str*/) {
    return false;
}

// (further Task 10 HTTP sub-API symbols appended here as the link demands them)
