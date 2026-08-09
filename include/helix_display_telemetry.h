// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HELIX_DISPLAY_TELEMETRY_H
#define HELIX_DISPLAY_TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Report a telemetry anomaly from a display backend.
 *
 * Thin C bridge so the display backends (which land in libhelix-display, and are
 * force-linked with --whole-archive into helix-splash / helix-watchdog) can raise
 * a telemetry anomaly WITHOUT depending on the heavy TelemetryManager singleton.
 * The real implementation (src/system/helix_display_telemetry.cpp) forwards to
 * TelemetryManager::record_error and links into the main binary; the splash and
 * watchdog binaries link a no-op stub (tools/helix_lvgl_anomaly_stub.cpp) because
 * they have no telemetry pipeline.
 *
 * Rate-limited (1 per category per 5 minutes) by the underlying TelemetryManager.
 */
void helix_display_telemetry_error(const char* category, const char* code, const char* detail);

#ifdef __cplusplus
}
#endif

#endif
