// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_tool_chip.h"

#include "ui_observer_guard.h"

#include "ams_state.h"
#include "app_globals.h"
#include "ams_types.h"
#include "filament_op_slot_resolver.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_parser.h"
#include "helix-xml/src/xml/lv_xml_widget.h"
#include "helix-xml/src/xml/parsers/lv_xml_obj_parser.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "theme_manager.h"
#include "ui_temperature_utils.h"
#include "tool_state.h"
#include "ui_panel_ams_overview.h" // navigate_to_ams_panel

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

/// Spool bar height. Thin enough to read as a gauge rather than a second row of
/// content, thick enough to still show a colour at a glance.
constexpr int32_t BAR_H_PX = 5;
constexpr int32_t BAR_H_COMPACT_PX = 4;

/// Material dot diameter.
/// Chip corner radius. Matches the #border_radius_sm XML const; XML may
/// override it per breakpoint like any other style attribute.
constexpr int32_t CHIP_RADIUS_PX = 4;

constexpr int32_t DOT_PX = 8;
constexpr int32_t DOT_COMPACT_PX = 7;

/// Below this remaining fraction the spool bar goes amber, so a run-out is
/// visible before it bites rather than after.
constexpr float LOW_SPOOL_FRACTION = 0.15f;

/// Border width for the selected chip. Selection owns the WIDTH and the fill;
/// heat owns the border COLOUR. Splitting the two means a heating tool still
/// reads as heating when it is not the selected one, and the selected tool is
/// still obvious when its border has gone amber.
constexpr int32_t BORDER_W_SELECTED_PX = 2;
constexpr int32_t BORDER_W_PX = 1;

/// Chip width under which the gram figure is dropped even on a four-tool
/// machine. Measured, not derived from the tool count: a narrow display can
/// pinch four chips as hard as eight chips pinch a wide one.
constexpr int32_t MIN_WIDTH_FOR_GRAMS_PX = 118;

/**
 * @brief Everything one chip needs to draw itself, resolved from live state.
 *
 * Deliberately a plain value: it is recomputed wholesale on every refresh, so
 * there is no partially-updated chip to reason about.
 */
struct ChipModel {
    bool valid = false;      ///< The tool index still exists
    std::string id;          ///< "T0"
    std::string material;    ///< "PETG", or "—" when nothing is mounted
    bool has_material = false;
    bool loaded = false;     ///< Filament from this tool is at the toolhead
    bool selected = false;   ///< The panel's verbs act on this tool
    uint32_t color = 0;      ///< Material colour, 0xRRGGBB
    bool has_color = false;
    float fraction = -1.0f;  ///< Spool remaining, 0..1; <0 = unknown
    std::string remaining;   ///< "480g", or "" when unknown
    /// An AMS operation — a tool change included — is already running. Starting
    /// a second one is a guaranteed refusal, so the row goes inert instead.
    bool busy = false;

    /// This tool's hotend is being held hot - it has a target set. A toolchanger
    /// can hold several heads at temperature at once, and the single nozzle
    /// reading on the left only ever speaks for the active tool, so which heads
    /// are live has to be answered per chip.
    bool heating = false;
    std::string heater; ///< Heater this tool reads, for (re)binding observers
};

/// Per-instance chip state. Lives in the widget's user_data, freed on delete.
struct ChipData {
    int index = -1;

    lv_obj_t* top_row = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* id_label = nullptr;
    lv_obj_t* material_label = nullptr;
    lv_obj_t* bottom_row = nullptr;
    lv_obj_t* bar_track = nullptr;
    lv_obj_t* bar_fill = nullptr;
    lv_obj_t* remaining_label = nullptr;

    /// Selection as last painted. refresh_heat_only() repaints the edge from a
    /// temperature tick alone, so it needs the selection without rebuilding the
    /// whole model just to read it back.
    bool selected = false;

    /// Heater the heat observers are currently bound to. The tool list can be
    /// rebuilt under us (AMS topology), so a chip re-binds when its tool starts
    /// reading a different hotend rather than watching a stale one forever.
    std::string bound_heater;
    ObserverGuard heat_target_obs;
    // The heater's target subject belongs to its extruder entry, not to
    // PrinterState as a whole, so the guard holds that subject's own lifetime.
    SubjectLifetime heat_target_lifetime;

    bool compact = false;

    ObserverGuard tools_version_obs;
    ObserverGuard active_tool_obs;
    ObserverGuard ams_revision_obs;
    ObserverGuard ams_action_obs;
    ObserverGuard selected_obs;

    /// Expires the observers above when the chip is deleted.
    SubjectLifetime alive = std::make_shared<bool>(true);
};

ChipData* chip_data(lv_obj_t* obj) {
    return obj ? static_cast<ChipData*>(lv_obj_get_user_data(obj)) : nullptr;
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

/// The tool row's selection subject, or nullptr before the panel owns one.
lv_subject_t* selected_subject() {
    return lv_xml_get_subject(nullptr, UI_TOOL_CHIP_SELECTED_SUBJECT);
}

/**
 * @brief Resolve one tool's display state from ToolState + the AMS backend.
 *
 * The tool->slot step goes through helix::ui::resolve_op_button_slot() — the
 * same resolver the panel's Load/Unload gating uses — so a chip can never
 * disagree with the buttons about which lane a tool feeds.
 */
bool heater_is_on(const std::string& heater);
void paint_heat_border(lv_obj_t* chip, bool heating, bool selected);

ChipModel build_model(int index) {
    ChipModel m;

    auto& ts = helix::ToolState::instance();
    const auto& tools = ts.tools();
    if (index < 0 || index >= static_cast<int>(tools.size())) {
        return m;
    }
    const helix::ToolInfo& tool = tools[static_cast<size_t>(index)];

    m.valid = true;
    m.id = tool.name;

    lv_subject_t* sel = selected_subject();
    const int selected_index = sel ? lv_subject_get_int(sel) : ts.active_tool_index();
    m.selected = (selected_index == index);

    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        const AmsSystemInfo sys = backend->get_system_info();
        m.busy = sys.is_busy();
        const int slot = helix::ui::resolve_op_button_slot(sys, index, ts.tool_count());
        if (const SlotInfo* info = sys.get_slot_global(slot)) {
            m.material = info->material;
            m.has_material = !info->material.empty();
            m.color = info->color_rgb;
            m.has_color = true;
            m.loaded = (info->status == SlotStatus::LOADED);
            if (info->total_weight_g > 0 && info->remaining_weight_g >= 0) {
                m.fraction = info->remaining_weight_g / info->total_weight_g;
            }
            if (info->remaining_weight_g >= 0) {
                m.remaining = std::to_string(static_cast<int>(info->remaining_weight_g)) + "g";
            }
        }
    }

    // No AMS lane spoke for this tool: a direct-drive tool carries its own
    // Spoolman assignment, which is the only spool figure such a printer has.
    if (m.remaining.empty() && tool.remaining_weight_g >= 0) {
        m.remaining = std::to_string(static_cast<int>(tool.remaining_weight_g)) + "g";
        if (tool.total_weight_g > 0) {
            m.fraction = tool.remaining_weight_g / tool.total_weight_g;
        }
    }
    if (!m.has_material && !tool.spool_name.empty()) {
        m.material = tool.spool_name;
        m.has_material = true;
    }
    if (!m.has_material) {
        m.material = "—";
    }
    if (m.fraction >= 0.0f) {
        m.fraction = std::clamp(m.fraction, 0.0f, 1.0f);
    }

    m.heater = tool.effective_heater();
    m.heating = heater_is_on(m.heater);
    return m;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void apply_model(lv_obj_t* chip, ChipData* d, const ChipModel& m) {
    if (!chip || !d) {
        return;
    }

    // Fill says what the tool holds; the edge (below) says how hot it is.
    // Selected lifts to the elevated fill, a tool that merely has filament
    // loaded sits on the card fill, and an idle one is an outline only — so the
    // row reads as one selected thing among several without the two signals
    // competing for the same channel.
    d->selected = m.selected;
    paint_heat_border(chip, m.heating, m.selected);
    if (m.selected) {
        lv_obj_set_style_bg_color(chip, theme_manager_get_color("elevated_bg"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    } else if (m.loaded) {
        lv_obj_set_style_bg_color(chip, theme_manager_get_color("card_bg"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    // Dot: the material's own colour when this tool's filament is actually at
    // the toolhead, a dim placeholder otherwise. That is the whole "which of
    // these is loaded" signal, so it must not also fire for a merely-mounted spool.
    if (d->dot) {
        const lv_color_t dot_color = (m.loaded && m.has_color)
                                         ? lv_color_hex(m.color)
                                         : theme_manager_get_color("border");
        lv_obj_set_style_bg_color(d->dot, dot_color, LV_PART_MAIN);
    }

    // A running operation owns the toolhead, so no chip is choosable until it
    // finishes. Fading the whole row says that once, rather than letting every
    // tap produce a refusal toast.
    lv_obj_set_style_opa(chip, m.busy ? LV_OPA_50 : LV_OPA_COVER, LV_PART_MAIN);


    if (d->id_label) {
        lv_label_set_text(d->id_label, m.id.c_str());
        lv_obj_set_style_text_color(d->id_label,
                                    theme_manager_get_color(m.selected ? "text" : "text_subtle"),
                                    LV_PART_MAIN);
    }

    if (d->material_label) {
        lv_label_set_text(d->material_label, m.material.c_str());
        const char* token = !m.has_material ? "text_subtle" : (m.selected ? "text" : "text_muted");
        lv_obj_set_style_text_color(d->material_label, theme_manager_get_color(token),
                                    LV_PART_MAIN);
    }

    // Bar: amber below LOW_SPOOL_FRACTION, green when this tool is feeding,
    // otherwise a neutral fill. An unknown remaining draws an empty track rather
    // than a full one — a bar that lies full is worse than a bar that says nothing.
    if (d->bar_fill) {
        int32_t pct = 0;
        const char* token = "border";
        if (m.fraction >= 0.0f) {
            pct = static_cast<int32_t>(m.fraction * 100.0f + 0.5f);
            if (m.fraction < LOW_SPOOL_FRACTION) {
                token = "warning";
            } else if (m.loaded) {
                token = "success";
            } else {
                token = "text_subtle";
            }
        }
        lv_obj_set_width(d->bar_fill, lv_pct(std::clamp<int32_t>(pct, 0, 100)));
        lv_obj_set_style_bg_color(d->bar_fill, theme_manager_get_color(token), LV_PART_MAIN);
    }

    if (d->remaining_label) {
        lv_label_set_text(d->remaining_label, m.remaining.c_str());
        const char* token = "text";
        if (m.remaining.empty() || m.fraction < 0.0f) {
            token = "text_subtle";
        } else if (m.fraction < LOW_SPOOL_FRACTION) {
            token = "warning";
        }
        lv_obj_set_style_text_color(d->remaining_label, theme_manager_get_color(token),
                                    LV_PART_MAIN);
    }
}

/**
 * @brief Does this hotend have a target set — is it being held hot?
 *
 * The target, not the current temperature. A tool the printer is keeping at
 * temperature is the one that matters here, whether it is still ramping up or
 * already sitting there; a head with its heater off is on its way to cold and
 * is not what this mark is for.
 *
 * The single rule both callers use — the full rebuild and the per-tick edge
 * repaint — so a chip's border and its model can never disagree.
 */
bool heater_is_on(const std::string& heater) {
    if (heater.empty()) {
        return false;
    }
    lv_subject_t* tgt = get_printer_state().get_extruder_target_subject(heater);
    return tgt && helix::ui::temperature::deci_to_degrees(lv_subject_get_int(tgt)) > 0;
}

/**
 * @brief Colour the chip's edge from its hotend.
 *
 * The border is the heat channel and it says exactly one thing: amber when this
 * tool's heater is driven. Anything else takes the neutral edge, brightened
 * when it is the selected one.
 */
void paint_heat_border(lv_obj_t* chip, bool heating, bool selected) {
    const char* token = heating ? "warning" : (selected ? "text_subtle" : "border");
    lv_obj_set_style_border_color(chip, theme_manager_get_color(token), LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, selected ? BORDER_W_SELECTED_PX : BORDER_W_PX,
                                  LV_PART_MAIN);
}

/// Repaint just the edge. A heater-target change touches nothing else on the
/// chip, so it must not drag the AMS system-info read (and its mutex) along
/// with it.
void refresh_heat_only(lv_obj_t* chip) {
    ChipData* d = chip_data(chip);
    if (!d) {
        return;
    }
    paint_heat_border(chip, heater_is_on(d->bound_heater), d->selected);
}

/// Point the heater-target observer at @p heater, when it is not what it
/// already watches. Called from refresh(), which is also where the heater name
/// is resolved, so the two cannot drift.
void bind_heat_observers(lv_obj_t* chip, ChipData* d, const std::string& heater) {
    if (heater.empty() || heater == d->bound_heater) {
        return;
    }
    d->bound_heater = heater;
    // Only the TARGET is watched. The current temperature ticks about once a
    // second per hotend and changes nothing the chip draws, so observing it
    // would wake every chip on the row for nothing.
    d->heat_target_obs = helix::ui::observe_int_sync<lv_obj_t>(
        get_printer_state().get_extruder_target_subject(heater, d->heat_target_lifetime), chip,
        [](lv_obj_t* c, int) { refresh_heat_only(c); }, d->heat_target_lifetime);
}

void refresh(lv_obj_t* chip) {
    ChipData* d = chip_data(chip);
    if (!d) {
        return;
    }
    const ChipModel m = build_model(d->index);
    // A chip whose tool no longer exists hides itself rather than drawing a
    // blank card: the row is rebuilt from tool_count, and a stale chip would
    // otherwise linger for one frame as an empty slot.
    if (!m.valid) {
        lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_HIDDEN);
    bind_heat_observers(chip, d, m.heater);
    apply_model(chip, d, m);
}

/**
 * @brief Choose wide vs compact from the chip's own measured width.
 *
 * Compact drops the gram figure and nothing else — the dot, the id, the material
 * and the bar all survive, which is what lets an eight-tool machine still show
 * every spool at a glance where a scrolling strip would show four.
 */
void apply_compaction(lv_obj_t* chip) {
    ChipData* d = chip_data(chip);
    if (!d || !d->remaining_label) {
        return;
    }
    const int32_t w = lv_obj_get_width(chip);
    const int tool_count = helix::ToolState::instance().tool_count();
    const bool compact =
        (tool_count > UI_TOOL_CHIP_COMPACT_ABOVE) || (w > 0 && w < MIN_WIDTH_FOR_GRAMS_PX);
    if (compact == d->compact) {
        return;
    }
    d->compact = compact;

    if (compact) {
        lv_obj_add_flag(d->remaining_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(d->remaining_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (d->dot) {
        const int32_t dot = compact ? DOT_COMPACT_PX : DOT_PX;
        lv_obj_set_size(d->dot, dot, dot);
    }
    if (d->bar_track) {
        lv_obj_set_height(d->bar_track, compact ? BAR_H_COMPACT_PX : BAR_H_PX);
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// DECLARATIVE_OK: this file *is* the widget — there is no XML beneath it to
// bind, and SIZE_CHANGED / DELETE have no declarative equivalent.
void chip_event_cb(lv_event_t* e) {
    lv_obj_t* chip = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    ChipData* d = chip_data(chip);
    if (!d) {
        return;
    }

    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED: {
        // Tapping through a running tool change is what produced a string of
        // "AMS busy" refusals: each tap queued another change behind one the
        // backend had not finished. The precondition belongs here, before the
        // request, not in the error handler after it.
        if (build_model(d->index).busy) {
            spdlog::info("[ToolChip] T{} tap ignored — an AMS operation is running", d->index);
            return;
        }
        lv_subject_t* sel = selected_subject();
        if (!sel) {
            spdlog::warn("[ToolChip] No '{}' subject — selection ignored",
                         UI_TOOL_CHIP_SELECTED_SUBJECT);
            return;
        }
        // Tapping the already-selected tool clears the selection. That is how the
        // all-docked state is reached, so the row needs no separate Dock control.
        const int next = (lv_subject_get_int(sel) == d->index) ? -1 : d->index;
        lv_subject_set_int(sel, next);
        break;
    }
    case LV_EVENT_LONG_PRESSED:
        // The tool row replaced the spool card that used to carry a Manage
        // button, so the chips are now the only filament surface on this panel.
        // Long-press keeps the AMS panel one gesture away rather than only
        // reachable from Home.
        navigate_to_ams_panel();
        break;
    case LV_EVENT_SIZE_CHANGED:
        apply_compaction(chip);
        break;
    case LV_EVENT_DELETE:
        d->tools_version_obs.reset();
        d->active_tool_obs.reset();
        d->ams_revision_obs.reset();
        d->ams_action_obs.reset();
        d->selected_obs.reset();
        d->heat_target_obs.reset();
        lv_obj_set_user_data(chip, nullptr);
        delete d;
        break;
    default:
        break;
    }
}

void wire_observers(lv_obj_t* chip, ChipData* d) {
    auto& ts = helix::ToolState::instance();
    auto on_change = [](lv_obj_t* c, int) { refresh(c); };

    d->tools_version_obs = helix::ui::observe_int_sync<lv_obj_t>(ts.get_tools_version_subject(), chip,
                                                      on_change, d->alive);
    d->active_tool_obs =
        helix::ui::observe_int_sync<lv_obj_t>(ts.get_active_tool_subject(), chip, on_change, d->alive);
    d->ams_revision_obs = helix::ui::observe_int_sync<lv_obj_t>(
        AmsState::instance().get_ams_data_revision_subject(), chip, on_change, d->alive);
    // The action subject is what moves on a tool change, so the inert look
    // arrives with the operation rather than a data revision later.
    d->ams_action_obs = helix::ui::observe_int_sync<lv_obj_t>(
        AmsState::instance().get_ams_action_subject(), chip, on_change, d->alive);
    if (lv_subject_t* sel = selected_subject()) {
        d->selected_obs = helix::ui::observe_int_sync<lv_obj_t>(sel, chip, on_change, d->alive);
    }
}

// ---------------------------------------------------------------------------
// XML widget
// ---------------------------------------------------------------------------

lv_obj_t* make_row(lv_obj_t* parent, int32_t height) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);
    return row;
}

lv_obj_t* make_label(lv_obj_t* parent, const char* font_token) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    if (const lv_font_t* font = theme_manager_get_font(font_token)) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    }
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return label;
}

void* ui_tool_chip_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    (void)attrs;
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));

    lv_obj_t* chip = lv_obj_create(parent);
    auto* d = new ChipData();
    lv_obj_set_user_data(chip, d);

    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(chip, CHIP_RADIUS_PX, LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chip, theme_manager_get_color("border"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chip, theme_manager_get_spacing("space_sm"), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chip, theme_manager_get_spacing("space_xxs"), LV_PART_MAIN);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(chip, theme_manager_get_spacing("space_xxs"), LV_PART_MAIN);

    // Top: dot, id, material (material right-aligned via the spacer's grow)
    d->top_row = make_row(chip, LV_SIZE_CONTENT);
    d->dot = lv_obj_create(d->top_row);
    lv_obj_set_size(d->dot, DOT_PX, DOT_PX);
    lv_obj_set_style_radius(d->dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d->dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(d->dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d->dot, LV_OBJ_FLAG_EVENT_BUBBLE);

    d->id_label = make_label(d->top_row, "font_small");
    d->material_label = make_label(d->top_row, "font_small");
    lv_obj_set_flex_grow(d->material_label, 1);
    lv_label_set_long_mode(d->material_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(d->material_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    // Bottom: spool bar, then the gram figure compaction drops first
    d->bottom_row = make_row(chip, LV_SIZE_CONTENT);
    d->bar_track = lv_obj_create(d->bottom_row);
    lv_obj_set_height(d->bar_track, BAR_H_PX);
    lv_obj_set_flex_grow(d->bar_track, 1);
    lv_obj_set_style_pad_all(d->bar_track, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d->bar_track, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->bar_track, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d->bar_track, theme_manager_get_color("elevated_bg"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d->bar_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(d->bar_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d->bar_track, LV_OBJ_FLAG_EVENT_BUBBLE);

    d->bar_fill = lv_obj_create(d->bar_track);
    lv_obj_set_height(d->bar_fill, lv_pct(100));
    lv_obj_set_width(d->bar_fill, 0);
    lv_obj_align(d->bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_pad_all(d->bar_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d->bar_fill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->bar_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d->bar_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(d->bar_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d->bar_fill, LV_OBJ_FLAG_EVENT_BUBBLE);

    d->remaining_label = make_label(d->bottom_row, "font_xs");

    lv_obj_add_event_cb(chip, chip_event_cb, LV_EVENT_ALL, nullptr);
    return chip;
}

void ui_tool_chip_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* chip = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));
    lv_xml_obj_apply(state, attrs);

    ChipData* d = chip_data(chip);
    if (!d) {
        return;
    }

    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], "index") == 0) {
            d->index = atoi(attrs[i + 1]);
        }
    }

    wire_observers(chip, d);
    apply_compaction(chip);
    refresh(chip);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ui_tool_chip_register_widget() {
    lv_xml_register_widget("tool_chip", ui_tool_chip_xml_create, ui_tool_chip_xml_apply);
    spdlog::trace("[ToolChip] Widget registered with XML system");
}

bool ui_tool_chip_is_valid(lv_obj_t* obj) {
    return chip_data(obj) != nullptr;
}

int ui_tool_chip_get_index(lv_obj_t* obj) {
    ChipData* d = chip_data(obj);
    return d ? d->index : -1;
}
