// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"
#include "filament_slot_override.h"
#include "hv/json.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

class IMoonrakerAPI;
class FilamentSlotOverrideStoreTestAccess;

namespace helix::ams {

// Outer Moonraker DB key convention for per-slot lane_data records. The two
// styles carry the SAME 0-based inner "lane" field but different outer keys:
//   - Lane: "laneN" (1-based, AFC/Happy Hare convention) — filament systems.
//   - Tool: "T<n>"  (0-based, Orca/Mainsail tool convention) — tool changers.
// A tool changer converging on the T<n> key style makes HelixScreen writes
// overwrite Mainsail #2510's records instead of duplicating them (both readers
// key off the inner "lane" field, so a shared outer key avoids Orca's no-dedup
// collision). See docs/specs/filament_slots.md § "Interoperating readers and
// writers".
enum class LaneKeyStyle { Lane, Tool };

// Maps an AmsType to its lane_data key style. Tool changers (Snapmaker, generic
// TOOL_CHANGER) use T<n> keys; every filament-switching system uses laneN.
// Deriving from is_tool_changer() keeps the policy in one place — never branch
// on backend_id, and never use !is_filament_system() (it includes SNAPMAKER in
// both lists, ams_types.h).
inline LaneKeyStyle lane_key_style_for(AmsType t) {
    return is_tool_changer(t) ? LaneKeyStyle::Tool : LaneKeyStyle::Lane;
}

// Counts of lane_data records that are inconsistent or invisible to other
// readers. Detected read-only at load and logged once — we do NOT auto-rewrite
// third-party/corrupt records (that would vandalize a shared namespace); the
// one-shot laneN->T<n> migration only ever touches keys HelixScreen authored.
// Note: out-of-range slots are intentionally NOT counted — the store does not
// know NUM_PORTS (the caller range-checks), so it cannot honestly detect them.
struct LaneDataAnomalies {
    int int_typed_lane = 0;     ///< inner "lane" is an int, not a string — OrcaSlicer drops these
    int key_inner_mismatch = 0; ///< key looks like laneN/T<n> but disagrees with the inner index
    int unparseable = 0;        ///< non-"seated" object carrying no valid "lane" field
    int duplicate_slot = 0;     ///< more than one record resolving to the same slot index
    [[nodiscard]] int total() const {
        return int_typed_lane + key_inner_mismatch + unparseable + duplicate_slot;
    }
};

// Read-only scan of a raw lane_data namespace document. Pure: no DB access, no
// mutation. Skips the "seated" sibling scalar. Used for the one-shot load-time
// diagnostic; also unit-tested directly.
[[nodiscard]] LaneDataAnomalies scan_lane_data_anomalies(const nlohmann::json& namespace_doc);

class FilamentSlotOverrideStore {
  public:
    // key_style defaults to Lane so the many lane-based construction sites and
    // tests need no change. Production sites pass lane_key_style_for(get_type())
    // so the correct style is derived from the backend's AmsType.
    FilamentSlotOverrideStore(IMoonrakerAPI* api, std::string backend_id,
                              LaneKeyStyle key_style = LaneKeyStyle::Lane);

    // Blocking load from Moonraker database (called only at backend init time).
    // Later tasks will add local-cache fallback.
    std::unordered_map<int, FilamentSlotOverride> load_blocking();

    using SaveCallback = std::function<void(bool success, std::string error)>;
    void save_async(int slot_index, const FilamentSlotOverride& override, SaveCallback cb);
    void clear_async(int slot_index, SaveCallback cb);

    // Seated-lane persistence. Unlike the per-lane overrides above, this is a
    // single scalar (the 0-based index of the lane currently loaded to the
    // toolhead) stored under a sibling key "seated" in the same lane_data
    // namespace. The value on disk is a plain JSON integer.

    // Persist the 0-based seated lane index to lane_data/"seated".
    // Fire-and-forget, mirrors save_async (dispatches via
    // api_->database_post_item, does not block).
    void save_seated_slot_async(int slot_index, SaveCallback cb);

    // Remove lane_data/"seated" (nothing currently seated). Mirrors clear_async.
    void clear_seated_slot_async(SaveCallback cb);

    // Blocking read of lane_data/"seated" at init time (mirrors load_blocking's
    // cv.wait_for pattern + load_timeout_). Returns nullopt if absent/unreachable
    // or the stored value is not a valid 0-based lane index. The caller is
    // responsible for range-checking against its own NUM_PORTS.
    std::optional<int> load_seated_slot_blocking();

    const std::string& backend_id() const {
        return backend_id_;
    }

  private:
    // Test-only access to mutate load_timeout_ without exposing a public
    // setter. Per L065, prefer friend-class over test-only public methods.
    friend class ::FilamentSlotOverrideStoreTestAccess;

    IMoonrakerAPI* api_;
    std::string backend_id_;
    // Outer-key style for this backend's lane_data records (laneN vs T<n>). Set
    // once at construction from the backend's AmsType via lane_key_style_for().
    LaneKeyStyle key_style_;
    // Adopts the AFC/OrcaSlicer lane_data Moonraker convention. Each slot is
    // stored under key "laneN" where N is the 1-based slot index (lane1, lane2,
    // ...), or "T<n>" (0-based) on tool changers. Slot index 0 maps to "lane1"
    // (Lane style) or "T0" (Tool style) on disk. See format_lane_key in the
    // .cpp for the exact rule.
    std::string namespace_ = "lane_data";
    // Local timeout for load_blocking()'s cv.wait_for. Defaults to 5 seconds;
    // overridable by FilamentSlotOverrideStoreTestAccess for timeout tests.
    // Stored as milliseconds (not seconds) because tests need sub-second
    // resolution — a chrono::seconds member would truncate 50ms to 0s.
    std::chrono::milliseconds load_timeout_{5000};
    // On-disk read-cache directory. Empty = use helix::get_user_config_dir().
    // Overridable by FilamentSlotOverrideStoreTestAccess so tests write to a
    // per-PID tmp dir instead of polluting the user's config. The cache is
    // NEVER authoritative — the Moonraker DB on the printer is the source of
    // truth. The cache exists only so the UI can show last-known metadata
    // when Moonraker is unreachable at backend init.
    std::filesystem::path cache_dir_;
    // Absolute path to the cache JSON file. Computed from cache_dir_ (or
    // get_user_config_dir() if empty). One file serves all backends; each
    // backend's slots live under doc[backend_id]["slots"].
    std::filesystem::path cache_path() const;
    // Absolute path to the directory used for on-disk caches. Same resolution
    // as cache_path(): cache_dir_ if set, otherwise get_user_config_dir().
    // Migration uses this to locate legacy "{backend_id}_slot_overrides.json"
    // files that pre-date the unified filament_slot_overrides.json format.
    std::filesystem::path cache_dir_effective() const;
};

// =============================================================================
// Shared firmware -> lane_data mirror helper
// =============================================================================
//
// AFC and Happy Hare publish lane_data themselves (their Klipper plugins write
// directly to the Moonraker DB), so OrcaSlicer's MoonrakerPrinterAgent can
// read filament state without HelixScreen's involvement.
//
// CFS, AD5X IFS, and Snapmaker firmware do NOT publish lane_data. HelixScreen
// has to mirror firmware-detected color/material into the lane_data namespace
// so OrcaSlicer's "Sync filaments from Printer" works. This helper centralizes
// that mirror so the three backends share one implementation.
//
// Why a policy enum: backends differ in whether user UI edits propagate back
// to firmware:
//
//   - IFS: set_slot_info writes to Adventurer5M.json — firmware re-reads it
//     and reports the user's chosen color on the next status poll. The mirror
//     can safely overwrite the override unconditionally because firmware-truth
//     and user-truth converge.
//
//   - CFS / Snapmaker: set_slot_info does NOT touch the firmware-side
//     material_type / RFID values. If the mirror unconditionally overwrote
//     ovr.color_rgb with firmware-truth, every status poll would erase the
//     user's color override. So these backends use FillUnsetOnly: only fill
//     fields the user hasn't explicitly set. clear_slot_override resets the
//     entry, after which auto-mirror takes over again.
enum class MirrorPolicy {
    /// Overwrite ovr.color_rgb / ovr.material with firmware values
    /// unconditionally. Use when user edits propagate back to firmware so the
    /// two views stay in sync (AD5X IFS).
    OverwriteAlways,
    /// Only fill ovr.color_rgb / ovr.material when they're currently UNSET
    /// (color_rgb == 0, empty material). Use when user edits don't reach
    /// firmware (CFS, Snapmaker).
    FillUnsetOnly,
};

/// Mirror firmware-detected color/material into `overrides[slot_index]` and
/// fire `store->save_async` to push the resulting record to the lane_data
/// namespace. Caller MUST hold the backend's mutex protecting `overrides`.
///
/// No-op (returns false without writing) when:
///   - !slot_has_filament  (empty / unread slot — no signal)
///   - the chosen policy leaves nothing to change (e.g. FillUnsetOnly when
///     ovr already has both fields set, or OverwriteAlways when ovr already
///     matches firmware)
///
/// Note: `firmware_color == 0` is NOT treated as "no signal" — pure black is
/// a legitimate color the user can load. Backends whose parse path may run
/// before colors are populated (e.g. AD5X IFS) must apply their own
/// color-zero guard upstream of this helper.
///
/// `store` may be null (init-time race / test fixture without MR API) — the
/// in-memory override is still updated, but no save_async is fired.
///
/// `log_tag` is included in the warn log on save failure so multi-backend
/// logs stay attributable.
///
/// Returns true iff `overrides[slot_index]` was actually mutated. Callers
/// (e.g. IFS) use this to drive secondary side-effects like _IFS_VARS sync.
bool mirror_firmware_to_lane_data(FilamentSlotOverrideStore* store,
                                  std::unordered_map<int, FilamentSlotOverride>& overrides,
                                  int slot_index, uint32_t firmware_color,
                                  const std::string& firmware_material, bool slot_has_filament,
                                  MirrorPolicy policy, const std::string& log_tag);

} // namespace helix::ams
