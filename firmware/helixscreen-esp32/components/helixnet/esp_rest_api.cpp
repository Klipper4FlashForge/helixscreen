// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// esp_rest_api.cpp — the ESP32 concrete implementation of MoonrakerRestAPI and
// MoonrakerFileTransferAPI (Task 10), replacing the two excluded desktop TUs
// (src/api/moonraker_rest_api.cpp, src/api/moonraker_file_transfer_api.cpp —
// both libhv/hv::requests-based, see app_srcs.txt "EXCLUDED, and why") the
// same way esp_moonraker_client.cpp replaces moonraker_client.cpp for the
// WebSocket transport. Their ctor/dtor/virtuals used to live as link stubs in
// helixapp/task10_pending_stubs.cpp; this file is their real home now.
//
// R1 enumeration (esp32p4-task-10-report.md has the full call-site ledger):
// of the ten ITransfersAPI/IRestAPI methods, exactly ONE is reachable from the
// v1 print-select thumbnail/metadata surface AND fits the R3 no-materialization
// constraint:
//
//   ITransfersAPI::download_file_partial — REAL (below). Used today by
//   ui_panel_print_select.cpp's gcode-header thumbnail-extraction fallback
//   (100KB Range fetch) and is exactly the "download_file_partial-style
//   in-memory thumbnail fetch" the Task 10 plan entry describes — Task 11
//   ("new design" print-select) reuses this same primitive directly for real
//   thumbnail bytes (root="gcodes", path=the resolved .thumbs/ path, capped
//   at kHardCapBytes), it just isn't wired to a UI yet.
//
// ITransfersAPI::get_file_metadata / metascan_file are IFilesAPI (NOT this
// file) and are already real: they're pure JSON-RPC over the WebSocket
// transport (src/api/moonraker_file_api.cpp, kept unmodified in
// app_srcs.txt), no HTTP involved.
//
// Everything else here is an asserting stub (log + ErrorCallback, matching
// task10_pending_stubs.cpp's existing non-fatal pattern — never abort/reset
// the board on a stray user action):
//
//   ITransfersAPI::download_thumbnail — its CONTRACT implies a file path
//     (takes a cache_path, returns a local path via StringCallback so the
//     caller can lv_image_set_src() it — see moonraker_file_transfer_api.h's
//     doc comment). That's exactly R3's "if an interface method's contract
//     implies a file path, it gets an asserting stub instead" carve-out.
//     Task 11's new PSRAM-thumbnail design calls download_file_partial
//     directly instead of going through this file-path-shaped method.
//   ITransfersAPI::download_file, download_file_to_path, upload_file,
//     upload_file_with_name, upload_file_from_path — not reachable from the
//     v1 print-select thumbnail/metadata surface (used elsewhere: macro
//     editor, AMS AD5X/QIDI polling, timelapse, klipper.conf editor, print-
//     start prep — all out of Task 10's scope), and download_file_to_path
//     specifically is HARD-BANNED by R3 regardless (file materialization).
//   IRestAPI::call_rest_get/call_rest_post/wled_*/get_server_config — no
//     print-select call site; WLED is excluded from v1, and the AMS backends
//     that use call_rest_get/call_rest_post (ACE, Snapmaker) are explicitly
//     out of Task 10's scope per the brief ("What's missing is the HTTP
//     side: thumbnails and file metadata").

#include "moonraker_file_transfer_api.h"
#include "moonraker_rest_api.h"

#include "esp_http_lane.h"

#include "esp_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

// NOTE: deliberately does NOT include src/api/moonraker_api_internal.h. That
// header unconditionally pulls in moonraker_api.h -> moonraker_client.h, which
// needs the parse-only hv/Event.h + hv/WebSocketClient.h shim helixapp
// provides (HELIXAPP_SHIM) — helixnet doesn't have that shim and shouldn't
// gain a dependency on it just for two validation helpers. This file stays in
// the same "no calls into helixapp-only symbols" lane esp_moonraker_client.cpp
// already keeps (see its header comment) and reimplements the small
// ESP32-relevant subset of moonraker_api_internal.h's validation/error-report
// helpers directly, against nothing heavier than moonraker_error.h (already
// proven to compile standalone in this component — see this CMakeLists.txt's
// REPO_SRCS comment on MoonrakerError::timeout()).

namespace {
constexpr char TAG[] = "esp_rest_api";

// --- Minimal standalone equivalents of src/api/moonraker_api_internal.h's
// path/root validation + error-report helpers (desktop's file rejects
// directory traversal / control chars the same way; kept in sync by hand
// since this file can't include that header — see note above). ---

bool esp_is_safe_path(const std::string& path) {
    if (path.empty() || path.find("..") != std::string::npos || path[0] == '/' ||
        path.find('\0') != std::string::npos) {
        return false;
    }
    return path.find_first_of("<>|*?") == std::string::npos;
}

bool esp_is_safe_file_root(const std::string& root) {
    auto is_plain = [](const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
                   return std::isalnum(c) || c == '_' || c == ' ';
               });
    };
    if (is_plain(root)) {
        return true;
    }
    return root.size() > 1 && root[0] == '.' && is_plain(root.substr(1));
}

void esp_report_error(const std::function<void(const MoonrakerError&)>& on_error,
                      MoonrakerErrorType type, const char* method, const std::string& message) {
    if (!on_error) {
        return;
    }
    MoonrakerError err;
    err.type = type;
    err.method = method;
    err.message = message;
    on_error(err);
}

// Returns true (and reports VALIDATION_ERROR) if path is INVALID — caller
// should return immediately, mirroring reject_invalid_path()'s contract.
bool esp_reject_invalid_path(const std::string& path, const char* method,
                             const std::function<void(const MoonrakerError&)>& on_error) {
    if (esp_is_safe_path(path)) {
        return false;
    }
    ESP_LOGE(TAG, "%s: invalid path '%s'", method, path.c_str());
    esp_report_error(on_error, MoonrakerErrorType::VALIDATION_ERROR, method,
                     "Invalid path contains directory traversal or illegal characters");
    return true;
}

bool esp_reject_invalid_file_root(const std::string& root, const char* method,
                                  const std::function<void(const MoonrakerError&)>& on_error) {
    if (esp_is_safe_file_root(root)) {
        return false;
    }
    ESP_LOGE(TAG, "%s: invalid file root '%s'", method, root.c_str());
    esp_report_error(on_error, MoonrakerErrorType::VALIDATION_ERROR, method,
                     "Invalid file root contains illegal characters");
    return true;
}

// Percent-encodes a Moonraker file path the same way desktop's HUrl::escape(
// path, "/.-_") does: alnum and "/.-_" pass through unescaped, everything else
// becomes %XX. libhv's HUrl isn't part of the ESP32 build, so this is a small
// standalone equivalent (no ESP-IDF/libhv dependency, just <cctype>/<cstdio>).
std::string esp_url_escape_path(const std::string& path) {
    static constexpr char kUnreserved[] = "/.-_";
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr(kUnreserved, static_cast<char>(c)) != nullptr) {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Non-fatal: log + surface failure through the API's ErrorCallback. Mirrors
// task10_pending_stubs.cpp's task10_unimplemented_err — a stray user action
// (e.g. an AMS backend polling call_rest_get) must fail gracefully, not
// abort/reset the board.
void esp_rest_unimplemented_err(const char* sym,
                                const std::function<void(const MoonrakerError&)>& err) {
    ESP_LOGE(TAG, "esp_rest_api stub: %s", sym);
    if (err) {
        MoonrakerError e;
        e.type = MoonrakerErrorType::VALIDATION_ERROR;
        e.method = sym;
        err(e);
    }
}
} // namespace

// ============================================================================
// MoonrakerFileTransferAPI
// ============================================================================

MoonrakerFileTransferAPI::MoonrakerFileTransferAPI(helix::IMoonrakerClient& client,
                                                   const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

MoonrakerFileTransferAPI::~MoonrakerFileTransferAPI() = default;

void MoonrakerFileTransferAPI::download_file_partial(const std::string& root,
                                                     const std::string& path, size_t max_bytes,
                                                     StringCallback on_success,
                                                     ErrorCallback on_error) {
    if (esp_reject_invalid_path(path, "download_file_partial", on_error))
        return;
    if (esp_reject_invalid_file_root(root, "download_file_partial", on_error))
        return;

    if (http_base_url_.empty()) {
        esp_report_error(on_error, MoonrakerErrorType::CONNECTION_LOST, "download_file_partial",
                        "HTTP base URL not configured");
        return;
    }

    std::string url = http_base_url_ + "/server/files/" + root + "/" + esp_url_escape_path(path);
    ESP_LOGD(TAG, "download_file_partial: %s (cap request %u)", url.c_str(), (unsigned)max_bytes);

    bool queued = helix::http::EspHttpLane::instance().submit_get(
        url, max_bytes,
        [on_success](const uint8_t* data, size_t size) {
            if (on_success) {
                on_success(std::string(reinterpret_cast<const char*>(data), size));
            }
        },
        [on_error](const std::string& message) {
            esp_report_error(on_error, MoonrakerErrorType::UNKNOWN, "download_file_partial",
                            message);
        });

    if (!queued) {
        esp_report_error(on_error, MoonrakerErrorType::UNKNOWN, "download_file_partial",
                        "HTTP lane queue full — try again");
    }
}

// --- Asserting stubs: not reachable from the v1 print-select surface (see
// file header). download_file_to_path is additionally hard-banned by R3
// regardless of reachability: its contract IS file materialization. ---

void MoonrakerFileTransferAPI::download_file(const std::string&, const std::string&,
                                             StringCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::download_file", on_error);
}

void MoonrakerFileTransferAPI::download_file_to_path(const std::string&, const std::string&,
                                                     const std::string&, StringCallback,
                                                     ErrorCallback on_error, ProgressCallback) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::download_file_to_path", on_error);
}

void MoonrakerFileTransferAPI::download_thumbnail(const std::string&, const std::string&,
                                                  StringCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::download_thumbnail", on_error);
}

void MoonrakerFileTransferAPI::upload_file(const std::string&, const std::string&,
                                           const std::string&, SuccessCallback,
                                           ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::upload_file", on_error);
}

void MoonrakerFileTransferAPI::upload_file_with_name(const std::string&, const std::string&,
                                                     const std::string&, const std::string&,
                                                     SuccessCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::upload_file_with_name", on_error);
}

void MoonrakerFileTransferAPI::upload_file_from_path(const std::string&, const std::string&,
                                                     const std::string&, SuccessCallback,
                                                     ErrorCallback on_error, ProgressCallback) {
    esp_rest_unimplemented_err("MoonrakerFileTransferAPI::upload_file_from_path", on_error);
}

// ============================================================================
// MoonrakerRestAPI — no v1 print-select call site (see file header); every
// method stays an asserting stub.
// ============================================================================

MoonrakerRestAPI::MoonrakerRestAPI(helix::IMoonrakerClient& client,
                                   const std::string& http_base_url)
    : client_(client), http_base_url_(http_base_url) {}

MoonrakerRestAPI::~MoonrakerRestAPI() = default;

void MoonrakerRestAPI::call_rest_get(const std::string&, RestCallback on_complete) {
    ESP_LOGE(TAG, "esp_rest_api stub: MoonrakerRestAPI::call_rest_get");
    if (on_complete) {
        RestResponse resp;
        resp.success = false;
        resp.error = "not implemented on this platform";
        on_complete(resp);
    }
}

void MoonrakerRestAPI::call_rest_post(const std::string&, const json&, RestCallback on_complete) {
    ESP_LOGE(TAG, "esp_rest_api stub: MoonrakerRestAPI::call_rest_post");
    if (on_complete) {
        RestResponse resp;
        resp.success = false;
        resp.error = "not implemented on this platform";
        on_complete(resp);
    }
}

void MoonrakerRestAPI::wled_get_strips(RestCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerRestAPI::wled_get_strips", on_error);
}

void MoonrakerRestAPI::wled_set_strip(const std::string&, const std::string&, int, int,
                                      SuccessCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerRestAPI::wled_set_strip", on_error);
}

void MoonrakerRestAPI::wled_get_status(RestCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerRestAPI::wled_get_status", on_error);
}

void MoonrakerRestAPI::get_server_config(RestCallback, ErrorCallback on_error) {
    esp_rest_unimplemented_err("MoonrakerRestAPI::get_server_config", on_error);
}
