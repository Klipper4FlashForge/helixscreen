// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_ams_context_menu.h"

#include "lvgl.h"

#include <functional>
#include <memory>

namespace helix::ui {

/// What the owning panel does for the two menu actions this function cannot
/// perform on its own. Both are optional; an unset hook makes its menu entry a
/// no-op, which is what a panel with no operation sidebar wants.
struct ExternalSpoolMenuHooks {
    /// EDIT and SPOOLMAN. The bool requests the Spoolman spool picker directly
    /// (true for SPOOLMAN / "Select Spool", false for EDIT / "Spool Info").
    std::function<void(bool open_on_picker)> on_edit;

    /// LOAD. Routed to the panel because the dispatch belongs to its operation
    /// sidebar, which owns the stepper the op drives and the bypass-engage
    /// chain a load may need first.
    std::function<void()> on_load;

    /// UNLOAD. Routed for the same reason as on_load.
    std::function<void()> on_unload;
};

/// Show the External Spool context menu (Load / Unload / Edit / Spoolman /
/// Scan QR / Clear) anchored to a canvas widget. Replaces the duplicated
/// handle_bypass_click body that used to live in both AmsPanel and
/// AmsOverviewPanel.
///
/// Load and Unload act on EXTERNAL_SPOOL_SLOT, which plan_load() and
/// plan_unload() both resolve. Their enabled state is compute_op_button_gating()
/// with that slot, decided in AmsContextMenu::on_created() — this function only
/// routes the action.
///
/// @param parent_screen Parent screen for modal/overlay placement
/// @param anchor_widget Canvas (or other widget) the menu attaches to
/// @param context_menu  Owning panel's lazy-init unique_ptr — created on
///                      first use, reused afterwards
/// @param hooks         Panel-side actions; see ExternalSpoolMenuHooks. The
///                      Scan QR and Clear actions go straight to AmsState and
///                      don't need a panel hook.
void show_external_spool_menu(lv_obj_t* parent_screen, lv_obj_t* anchor_widget,
                              std::unique_ptr<AmsContextMenu>& context_menu,
                              ExternalSpoolMenuHooks hooks);

} // namespace helix::ui
