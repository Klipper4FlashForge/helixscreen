// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

namespace helix {

/// Preset-driven wizard skip policy, derived purely from whether a complete
/// preset is applied and how many printers are configured. Single source of
/// truth so the LVGL wizard and tests agree, and so the two concerns can't drift:
///   - skip_hardware: the preset already configures heater/fan/AMS/LED/filament/
///     input-shaper, so those pickers are redundant. True for ANY printer with a
///     preset — the first one or a later one added via the printer manager.
///   - first_run: additionally skip the summary and show the one-time telemetry
///     opt-in. First configured printer only — telemetry is a global, one-time
///     prompt, so it must not re-fire when adding subsequent printers.
struct WizardPresetPlan {
    bool skip_hardware = false;
    bool first_run = false;
};
WizardPresetPlan wizard_preset_plan(bool has_preset, int printer_count);

/// Config key (appended to Config::df()) recording that the top-level "preset"
/// marker was written by a wizard run that had not finished yet.
inline constexpr const char* kWizardPresetProvisional = "preset_provisional";

/// Whether a persisted preset marker is allowed to collapse the wizard's
/// printer-identify + hardware steps.
///
/// A preset seeded before the wizard ever ran (install-time detection, factory
/// image) is authoritative immediately — that fast path is the whole point of
/// shipping presets. A preset written *by* the printer-identify step is only
/// provisional until the run finishes: the marker alone made an interrupted
/// wizard (crash, power cut, user quits) come back with identify, every
/// hardware picker and the summary gone, locking the user to a pick they never
/// confirmed and offering no in-app way back.
///
/// Within the same process the preset stays authoritative, so the existing
/// in-run "preset applied during identify cleanup → collapse the rest"
/// redirect keeps working unchanged.
///
/// @param preset_marker    Config::has_preset() — a preset name is persisted
/// @param provisional      kWizardPresetProvisional is set for this printer
/// @param wizard_completed The active printer finished its wizard
/// @param applied_this_session The wizard applied the preset in this process
bool wizard_preset_is_authoritative(bool preset_marker, bool provisional, bool wizard_completed,
                                    bool applied_this_session);

/// Record that the printer-identify step applied a preset in this process.
void wizard_mark_preset_applied_this_session();

/// @see wizard_mark_preset_applied_this_session
bool wizard_preset_applied_this_session();

/// Clear the process-local flag. Tests only — no production caller.
void wizard_reset_preset_session_state();

// ============================================================================
// Id-based pure navigation over the step registry. Operates on a vector of
// {StepId, skipped} entries — the registry-driven representation. No LVGL;
// fully testable. This is the sole navigation API; the wizard and tests both
// drive it.
// ============================================================================

namespace wizard {
enum class StepId;
}

/// One registry entry's navigation state: its id and whether it is skipped.
struct StepSkip {
    wizard::StepId id;
    bool skipped;
};

/// Count of non-skipped entries.
int wizard_visible_count(const std::vector<StepSkip>&);

/// 1-based display number for `current`: 1 + number of visible entries strictly
/// before it.
int wizard_display_number(wizard::StepId current, const std::vector<StepSkip>&);

/// First non-skipped entry after `current`, or nullopt if none.
std::optional<wizard::StepId> wizard_next(wizard::StepId, const std::vector<StepSkip>&);

/// First non-skipped entry before `current`, or nullopt if none.
std::optional<wizard::StepId> wizard_prev(wizard::StepId, const std::vector<StepSkip>&);

/// True if there is no visible entry after `current`.
bool wizard_is_last(wizard::StepId, const std::vector<StepSkip>&);

} // namespace helix
