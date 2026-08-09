// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

struct BundleOptions {
    bool include_klipper_logs = false;
    bool include_moonraker_logs = false;
    std::string user_note;
};

struct BundleResult {
    bool success = false;
    std::string share_code;
    std::string error_message;
};

/**
 * @brief Inputs to the bundle's `update` section.
 *
 * Plain data so the section can be assembled — and unit-tested — for both the
 * suppressed and not-suppressed cases without mutating the process-wide caches
 * behind updates_externally_managed() / self_update_supported().
 */
struct UpdateDiagnostics {
    std::string install_root;            ///< app_get_install_root() ("" if unresolvable)
    bool install_parent_writable = true; ///< dirname(install_root) writable WITHOUT escalation
    bool self_update_supported = true;   ///< self_update_supported(): writable OR root reachable
    bool externally_managed = false;     ///< updates_externally_managed()
    std::string channel;                 ///< "stable"|"beta"|"dev"; "" → "unknown"
    std::string r2_base_url;             ///< effective manifest base URL; "" → "unknown"
    std::string last_check_status;       ///< UpdateChecker::Status as a readable string
    std::string available_version;       ///< cached update version, "" if none
    std::string last_check_error;        ///< last check's error text, "" if none
    std::string platform_asset_name;     ///< exact release artifact this device requests
};

class DebugBundleCollector {
  public:
    /// Collect all debug data into JSON
    static nlohmann::json collect(const BundleOptions& options = {});

    /// Collect, compress, and upload asynchronously.
    /// Callback is invoked on the UI thread via helix::ui::queue_update().
    using ResultCallback = std::function<void(const BundleResult&)>;
    static void upload_async(const BundleOptions& options, ResultCallback callback);

    /// Individual collectors (public for testing)
    static nlohmann::json collect_system_info();
    static nlohmann::json collect_printer_info();
    static std::string collect_log_tail(int num_lines = 2000);

    /// Metadata about the log pipeline so a bundle reader knows whether debug
    /// was being captured: { target, level, ring_lines, log_tail_source }.
    static nlohmann::json collect_log_meta();

    /**
     * @brief In-app update diagnostics: why the update UI is (or is not) usable.
     *
     * The About screen gates both "Check for Updates" and "Install Update" on
     * !in_app_updates_suppressed(); when suppressed the rows are absent and the
     * user cannot update at all. Without this section a "cannot update" report
     * carries no evidence of whether that happened or which of the two
     * predicates caused it.
     *
     * No LVGL access — every value comes from a plain C++ getter, so this is
     * safe from the HttpExecutor thread that upload_async() collects on.
     */
    static nlohmann::json collect_update_info();

    /// Assemble the `update` section from explicit inputs. Pure and static so
    /// both suppression branches are unit-testable.
    static nlohmann::json build_update_info(const UpdateDiagnostics& diag);

    static std::string collect_crash_txt();
    static nlohmann::json collect_sanitized_settings();
    static std::string collect_klipper_log_tail(int num_lines = 2000);
    static std::string collect_moonraker_log_tail(int num_lines = 2000);

    /// Read crash_report.txt from config_dir (persists after crash.txt consumed)
    static std::string collect_crash_report_txt(const std::string& config_dir);

    /// Read raw crash.txt from config_dir (active crash, before next-boot rotation)
    static std::string collect_crash_txt(const std::string& config_dir);

    /// Read crash_history.json from config_dir (past crash submissions)
    static nlohmann::json collect_crash_history(const std::string& config_dir);

    /// Get double-hashed device ID from telemetry_device.json (for R2 cross-ref)
    static std::string collect_device_id(const std::string& config_dir);

    /// Read log tail from an explicit ordered list of paths (testable)
    static std::string collect_log_tail_from_paths(const std::vector<std::string>& paths,
                                                   int num_lines);

    /// Collect Moonraker state via REST (server info, printer state, config)
    static nlohmann::json collect_moonraker_info();

    /// Collect filament system data (AFC, Happy Hare, ACE, Spoolman, tool changers)
    static nlohmann::json collect_filament_system_info();

    /// Filter a Klipper object list to filament-related objects (public for testing)
    static nlohmann::json filter_filament_objects(const nlohmann::json& object_list);

    /// Drop the Stats padding from a raw klippy.log tail (public for testing).
    ///
    /// Keeps every non-Stats line, the `stats_context` Stats lines immediately
    /// preceding each one, and the final `stats_tail` Stats lines. Klipper emits
    /// one ~850-byte "Stats <time>: ..." line per second, so a raw tail is almost
    /// entirely load averages — condensing lets the same byte budget reach hours
    /// back instead of minutes.
    ///
    /// Defaults are sized against real AD5X data (bundle UJCCQP6S: 21 events per
    /// 616s): an 82-minute fetch window condenses to ~340 KiB worst case, versus
    /// 512 KiB for the 10 minutes the old raw tail could reach.
    static std::string condense_klipper_log(const std::string& raw, int stats_context = 3,
                                            int stats_tail = 60);

    /// Collect platform-specific diagnostic files (e.g., AD5X Adventurer5M.json)
    /// served via Moonraker's /server/files/<root>/<path> endpoint. Files that
    /// don't exist (404) are skipped silently; other errors are recorded in-line.
    static nlohmann::json collect_platform_files();

    /// Sanitize a string value for PII patterns (emails, credentials, webhooks, tokens, MACs)
    static std::string sanitize_value(const std::string& value);

    /// Recursively strip sensitive keys from JSON (public for integration testing)
    static nlohmann::json sanitize_json(const nlohmann::json& input, int depth = 0);

    /**
     * @brief Run sanitize_value() over each line of a multi-line body.
     *
     * Applied to every text section that leaves the machine. Line-at-a-time so
     * sanitize_value()'s 4 KB ReDoS guard does not redact a whole log as one
     * oversized value.
     *
     * Catches MACs, tokens, credentials and emails. It cannot catch an SSID —
     * that is an arbitrary user-chosen string with no pattern — so SSIDs are
     * kept out of the logs at the call site instead (include/log_redact.h).
     */
    static std::string sanitize_text_block(const std::string& body);

    /// Gzip compression using zlib
    static std::vector<uint8_t> gzip_compress(const std::string& data);

  private:
    static constexpr const char* WORKER_URL = "https://crash.helixscreen.org/v1/debug-bundle";
    static constexpr const char* INGEST_API_KEY = "hx-tel-v1-a7f3c9e2d1b84056";

    /// Blocking HTTP GET to a Moonraker endpoint, returns parsed JSON or error object
    static nlohmann::json moonraker_get(const std::string& base_url, const std::string& endpoint,
                                        int timeout_sec = 10);

    /// Get the Moonraker HTTP base URL (from IMoonrakerAPI if connected)
    static std::string get_moonraker_url();

    /// Fetch the tail of a log file from Moonraker using HTTP Range requests
    static std::string fetch_log_tail(const std::string& base_url, const std::string& endpoint,
                                      int num_lines, int tail_bytes = 524288,
                                      bool condense_klipper = false);

    /// Check if a key name matches a sensitive pattern
    static bool is_sensitive_key(const std::string& key);
};

} // namespace helix
