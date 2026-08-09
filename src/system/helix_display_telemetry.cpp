// SPDX-License-Identifier: GPL-3.0-or-later
#include "helix_display_telemetry.h"

#include "system/telemetry_manager.h"

// Real implementation for the main binary. Splash / watchdog link a no-op stub
// (tools/helix_lvgl_anomaly_stub.cpp) instead — they force-link the display
// backends but run no telemetry pipeline. See helix_display_telemetry.h.
extern "C" void helix_display_telemetry_error(const char* category, const char* code,
                                              const char* detail) {
    TelemetryManager::instance().record_error(category ? category : "", code ? code : "",
                                              detail ? detail : "");
}
