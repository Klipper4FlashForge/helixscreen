// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_err.h"

// Mounts both LittleFS partitions the app needs before UI/asset access:
//   /littlefs  <- "storage" partition (read-write, minified ui_xml + config
//                 JSON + printer images; reflashed whole on asset updates —
//                 never format-on-fail, a corrupt image must be visible)
//   /config    <- "cfg" partition (persistent settings; survives asset
//                 reflashes; format-on-fail so a first boot or corrupt
//                 filesystem self-heals instead of bricking settings access)
// Logs mounted/total bytes for both. Returns the first mount failure (storage
// checked first); callers decide whether that's fatal.
esp_err_t storage_mount(void);
