// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_mount.h"

#include "esp_littlefs.h"
#include "esp_log.h"

#include <stdbool.h>

static const char *TAG = "storage_mount";

static esp_err_t mount_one(const char *base_path, const char *partition_label,
                            bool format_if_mount_failed) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .format_if_mount_failed = format_if_mount_failed,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount '%s' -> %s failed: %s", partition_label, base_path,
                  esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(partition_label, &total, &used);
    ESP_LOGI(TAG, "mounted '%s' -> %s: %u/%u KB used", partition_label, base_path,
             (unsigned)(used / 1024), (unsigned)(total / 1024));
    return ESP_OK;
}

esp_err_t storage_mount(void) {
    // storage: never format-on-fail. A corrupt asset image must fail loudly
    // (nothing on it is disposable app state) so the failure surfaces instead
    // of silently wiping the ui_xml/config assets it was built to protect.
    esp_err_t storage_err = mount_one("/littlefs", "storage", false);
    // cfg: format-on-fail. First boot (blank flash) and a corrupt filesystem
    // both self-heal instead of leaving settings permanently unreachable.
    esp_err_t cfg_err = mount_one("/config", "cfg", true);
    return storage_err != ESP_OK ? storage_err : cfg_err;
}
