// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_tool_text.h"

#include "ams_state.h"
#include "observer_factory.h"
#include "static_subject_registry.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <climits>
#include <cstdio>

static ObserverGuard s_tool_text_observer;
static ObserverGuard s_toolchange_total_observer;
static ObserverGuard s_toolchange_current_observer;
static ObserverGuard s_tool_badge_observer;
static bool s_initialized = false;

static void update_toolchange_text(AmsState* a) {
    int total = lv_subject_get_int(a->get_ams_number_of_toolchanges_subject());
    if (total > 0) {
        int current = lv_subject_get_int(a->get_ams_current_toolchange_subject());
        // Backends store a 0-based index (-1 = none yet); display is 1-based.
        // Clamped to the total so a backend that over-reports cannot render a
        // nonsensical "162 / 161".
        int raw_display = current + 1;
        int display_current = std::clamp(raw_display, 0, total);
        // The clamp is a display guard, not a correction: it turns an obviously
        // wrong "162 / 161" into a plausible "161 / 161" that then sits there for
        // the whole tail of the print, so the symptom that led us to the 1-based
        // vs 0-based mismatch in the first place would not be visible a second
        // time. Say so in the log instead. Rate-limited to the transition because
        // this runs on every toolchange status frame.
        //
        // Over-reporting is not necessarily a backend bug: number_of_toolchanges
        // comes from Moonraker file metadata, so a stale or re-sliced total can
        // legitimately sit below a correct current.
        static int s_last_warned = INT_MIN;
        if (raw_display > total) {
            if (raw_display != s_last_warned) {
                s_last_warned = raw_display;
                spdlog::warn("[AmsToolText] Toolchange {} exceeds total {} — clamping display. "
                             "Backend over-reported, or the sliced total is stale.",
                             raw_display, total);
            }
        } else {
            s_last_warned = INT_MIN;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%d / %d", display_current, total);
        lv_subject_copy_string(a->get_toolchange_text_subject(), buf);
    } else {
        lv_subject_copy_string(a->get_toolchange_text_subject(), "");
    }
}

namespace helix::ui {

void init_ams_tool_text_observers() {
    if (s_initialized) {
        return;
    }

    auto& ams = AmsState::instance();

    // Observer on raw ams_current_tool_ (int) → format "T%d" or "---"
    s_tool_text_observer =
        observe_int_sync<AmsState>(ams.get_current_tool_subject(), &ams, [](AmsState* a, int tool) {
            if (tool >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "T%d", tool);
                lv_subject_copy_string(a->get_current_tool_text_subject(), buf);
            } else {
                lv_subject_copy_string(a->get_current_tool_text_subject(), "---");
            }
        });

    // Two observers for toolchange text: one on total, one on current index
    s_toolchange_total_observer =
        observe_int_sync<AmsState>(ams.get_ams_number_of_toolchanges_subject(), &ams,
                                   [](AmsState* a, int /*total*/) { update_toolchange_text(a); });

    s_toolchange_current_observer =
        observe_int_sync<AmsState>(ams.get_ams_current_toolchange_subject(), &ams,
                                   [](AmsState* a, int /*current*/) { update_toolchange_text(a); });

    // Observer on tools_version_ → update tool badge text and visibility
    auto& tools = ToolState::instance();
    s_tool_badge_observer = observe_int_sync<ToolState>(
        tools.get_tools_version_subject(), &tools, [](ToolState* ts, int /*version*/) {
            // Gated on physical extruders, not tool count: an AMS expands the
            // tool list to one entry per filament slot, and annotating the one
            // hotend those slots share with "0" says nothing. Only a printer
            // with more than one nozzle needs to name which is which.
            const auto* tool = ts->has_multiple_extruders() ? ts->active_tool() : nullptr;
            if (tool) {
                // Index only ("0"), not the full tool name ("T0"). The badge is a
                // disc overlaid on the nozzle glyph it annotates, so its diameter is
                // bounded by the icon; two glyphs force it wide enough to cover the
                // icon. Call sites that want the full name bind a text label beside
                // the icon instead (print_status_detailed_active.xml).
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", tool->index);
                lv_subject_copy_string(ts->get_tool_badge_text_subject(), buf);
                lv_subject_set_int(ts->get_show_tool_badge_subject(), 1);
            } else {
                lv_subject_copy_string(ts->get_tool_badge_text_subject(), "");
                lv_subject_set_int(ts->get_show_tool_badge_subject(), 0);
            }
        });

    s_initialized = true;

    StaticSubjectRegistry::instance().register_deinit("AmsToolTextObservers", []() {
        if (s_initialized) {
            s_tool_text_observer.release();
            s_toolchange_total_observer.release();
            s_toolchange_current_observer.release();
            s_tool_badge_observer.release();
            s_initialized = false;
            spdlog::trace("[AmsToolText] Observers released");
        }
    });

    spdlog::debug("[AmsToolText] Tool text observers initialized");
}

} // namespace helix::ui
