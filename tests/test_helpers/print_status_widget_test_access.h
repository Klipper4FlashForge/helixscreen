// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/print_status_widget.h"

namespace helix {

// Friend access to PrintStatusWidget internals that production code reaches
// only through a live dashboard. The header forward-declares this class inside
// namespace helix, so the definition must live in the same namespace — and in
// ONE place: two test translation units each defining their own version of the
// class would be an ODR violation.
//
//  - idle_thumb_path_subject(): a private static subject.
//  - dispatch_load(): the idle runout dialog's "Load filament" action, which
//    otherwise needs a live modal, an attached widget, and a real runout sensor
//    reading to press.
//
// Follows the tests/test_helpers/ TestAccess pattern ([L088]) rather than
// adding _for_testing() accessors to the production API.
class PrintStatusWidgetTestAccess {
  public:
    static lv_subject_t* idle_thumb_path_subject() {
        return &PrintStatusWidget::idle_thumb_path_subject_;
    }

    static void dispatch_load(PrintStatusWidget& widget) {
        widget.dispatch_load();
    }
};

} // namespace helix
