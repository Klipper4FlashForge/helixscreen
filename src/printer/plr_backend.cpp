// SPDX-License-Identifier: GPL-3.0-or-later
#include "plr_backend.h"

#include <spdlog/spdlog.h>

#include <array>
#include <fstream>
#include <sstream>

#include "hv/json.hpp"

namespace helix {

namespace {

/// Data roots to try for the Creality recovery sidecar. K1-class images mount
/// user storage at /usr/data; the OpenWrt-class K2 uses /mnt/UDISK.
constexpr std::array<const char*, 2> kCrealityDataRoots = {"/usr/data", "/mnt/UDISK"};

/// Characters that would end the gcode command or start a new extended
/// parameter, letting a filename escape `FILENAME=<f>`. Spaces are NOT here:
/// Klipper's extended-command parser runs a value up to the next `KEY=` or end
/// of line, so spaces are legal and gcode filenames routinely contain them.
constexpr const char* kGcodeParamBreakers = "\n\r;#*=";

} // namespace

PlrBackendType plr_select_backend(const PlrCapabilitySignals& caps) {
    // Snapmaker first: its signal is passive, so choosing it never costs a
    // side-effectful probe. The two markers come from different firmware forks
    // and should never coexist, but the ordering makes the tie deterministic.
    if (caps.snapmaker_pl_env_valid) {
        return PlrBackendType::SNAPMAKER;
    }
    if (caps.creality_power_loss_field) {
        return PlrBackendType::CREALITY;
    }
    return PlrBackendType::NONE;
}

bool plr_creality_recovery_available(const PlrDetectResult& r) {
    // `completed` is load-bearing, not a redundancy check: it certifies that the
    // probe ran this connection, which is what set print_stats.power_loss=1 in
    // firmware. See PlrDetectResult and docs/devel/POWER_LOSS_RECOVERY.md.
    return r.completed && r.file_state && r.eeprom_state;
}

bool plr_parse_check_continue_response(const nlohmann::json& response, PlrDetectResult& out) {
    auto result_it = response.find("result");
    if (result_it == response.end() || !result_it->is_object()) {
        return false;
    }
    auto file_it = result_it->find("file_state");
    auto eeprom_it = result_it->find("eeprom_state");
    // Both must be present AND boolean. A firmware that answers with strings or
    // numbers is not one we understand, and "not understood" must never
    // authorize a resume.
    if (file_it == result_it->end() || !file_it->is_boolean() || eeprom_it == result_it->end() ||
        !eeprom_it->is_boolean()) {
        return false;
    }
    out.file_state = file_it->get<bool>();
    out.eeprom_state = eeprom_it->get<bool>();
    out.completed = true;
    return true;
}

bool plr_is_safe_recovery_filename(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    return name.find_first_of(kGcodeParamBreakers) == std::string::npos;
}

PlrRecoveryPlan plr_build_plan(PlrBackendType backend, const std::string& recovery_file,
                               const PlrDetectResult& detect) {
    PlrRecoveryPlan plan;
    plan.backend = backend;
    plan.recovery_file = recovery_file;

    switch (backend) {
    case PlrBackendType::SNAPMAKER:
        // Passive backend: pl_env_valid already IS the firmware's own
        // validation of the snapshot, so there is nothing further to confirm.
        // The gcode carries no parameters, so the filename is display-only.
        plan.resume_gcode = kSnapmakerResumeGcode;
        plan.discard_gcode = kSnapmakerDiscardGcode;
        break;

    case PlrBackendType::CREALITY:
        // Discard is always safe to expose — it touches no motion.
        plan.discard_rpc_method = kCrealityDiscardRpc;

        // SAFETY GATE. Resume is authorized ONLY by a completed probe reporting
        // both states. The probe is what sets print_stats.power_loss=1, which
        // the stock sensorless-homing macro reads to choose a full Z clearance
        // lift; without it the machine lifts 0.1mm and homes X/Y through the
        // part. Leaving resume_gcode empty is the refusal.
        if (!plr_creality_recovery_available(detect)) {
            spdlog::warn("[PLR] Creality resume withheld: detect not confirmed "
                         "(completed={} file_state={} eeprom_state={})",
                         detect.completed, detect.file_state, detect.eeprom_state);
            break;
        }
        if (!plr_is_safe_recovery_filename(recovery_file)) {
            // The gcode embeds FILENAME=, so with nothing safe to substitute
            // there is no command we can send at all.
            spdlog::warn("[PLR] Creality resume withheld: unusable recovery filename '{}'",
                         recovery_file);
            break;
        }
        plan.resume_gcode =
            std::string("SDCARD_PRINT_FILE FILENAME=") + recovery_file + " ISCONTINUEPRINT=1";
        break;

    case PlrBackendType::NONE:
        break;
    }
    return plan;
}

std::string plr_parse_creality_sidecar(const std::string& json_text) {
    if (json_text.empty()) {
        return {};
    }
    // Non-throwing parse: the sidecar is firmware-owned and may be truncated if
    // power was lost mid-write, which is exactly the situation we are in.
    nlohmann::json doc = nlohmann::json::parse(json_text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        spdlog::debug("[PLR] Creality sidecar is not parseable JSON");
        return {};
    }
    auto it = doc.find("file_path");
    if (it == doc.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

std::string plr_read_creality_recovery_filename() {
    for (const char* root : kCrealityDataRoots) {
        std::string path = std::string(root) + "/" + kCrealitySidecarRelPath;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            continue;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string name = plr_parse_creality_sidecar(buf.str());
        if (!name.empty()) {
            spdlog::info("[PLR] Creality recovery filename from {}: '{}'", path, name);
            return name;
        }
        spdlog::debug("[PLR] Creality sidecar {} present but yielded no file_path", path);
    }
    spdlog::debug("[PLR] No readable Creality recovery sidecar (looked for {})",
                  kCrealitySidecarRelPath);
    return {};
}

} // namespace helix
