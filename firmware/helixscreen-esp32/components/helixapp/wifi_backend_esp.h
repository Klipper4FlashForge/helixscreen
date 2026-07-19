// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task 13 — companion header for wifi_backend_esp.cpp. Firmware-local (not
// under include/): the one seam app_boot.cpp needs beyond the shared
// create_platform_wifi_backend() declaration in wifi_backend.h.

#pragma once

namespace helix {

// Gate flag for the esp_wifi hardware bring-up (esp_wifi_init/esp_wifi_start
// and their internal-DRAM allocations). Defaults to CLOSED: a WifiBackendEsp
// constructed and start_async()'d before this is opened (e.g. by
// NetworkWidget's ctor, which runs during build_shell() — well before
// app_boot.cpp's internal-DRAM boot gates clear) gets a harmless
// NOT_INITIALIZED WiFiError instead of running esp_wifi_init on the wrong
// thread/stack, preserving THE PATTERN (Task 8): the only >=32KB internal
// allocation on the net path happens on app_net_start()'s dedicated pthread,
// claimed AFTER the boot's internal-DRAM gates.
//
// app_boot.cpp's net thread calls this once, immediately before nudging the
// (possibly already-constructed) shared WiFiManager to retry — see
// helix::WiFiManager::retry_async() in wifi_manager.cpp, which re-invokes
// backend_->start_async() and this time performs the real bring-up.
void wifi_backend_esp_allow_hardware_bringup();

} // namespace helix
