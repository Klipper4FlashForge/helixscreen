// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_print_status.h"

#include <string>

// Friend access to PrintStatusPanel internals. `ui_panel_print_status.h`
// declares `friend class PrintStatusPanelTestAccess;` in the GLOBAL namespace,
// so the definition must live there too — and in ONE place: two test
// translation units each defining their own version of the class would be an
// ODR violation.
//
//  - recompute_aux_composites(): the measurement-only entry point for the fan
//    row's composite visibility, which otherwise needs a laid-out widget tree.
//  - set_thumbnail_widget(): stands in for the XML build, which is what
//    normally assigns print_thumbnail_. The shared-subject observer only
//    touches the image when that pointer is non-null, so a test that leaves it
//    null silently skips the code it means to exercise.
//  - displayed_file() / cached_thumbnail_path(): the two markers the panel uses
//    to decide whether the preview is current.
//
// Follows the tests/test_helpers/ TestAccess pattern ([L088]) rather than
// adding _for_testing() accessors to the production API.
class PrintStatusPanelTestAccess {
  public:
    static void recompute_aux_composites(PrintStatusPanel& panel, int density, bool aux_present) {
        panel.recompute_aux_composites_for_measurement(density, aux_present);
    }

    static void set_thumbnail_widget(PrintStatusPanel& panel, lv_obj_t* image) {
        panel.print_thumbnail_ = image;
    }

    static const std::string& displayed_file(const PrintStatusPanel& panel) {
        return panel.displayed_file_;
    }

    static const std::string& cached_thumbnail_path(const PrintStatusPanel& panel) {
        return panel.cached_thumbnail_path_;
    }
};
