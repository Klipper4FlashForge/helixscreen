// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_probe_overlay.h"

#include <cstdio>
#include <string>

/// Friend access to the probe overlay's config-edit flow (L065). The edit
/// modal is the only production path into handle_config_save(); seeding the
/// staged field/section/value here reaches the save without building it.
struct ProbeOverlayTestAccess {
    static void stage_edit(ProbeOverlay& o, const std::string& section, const std::string& field,
                           const std::string& value) {
        o.probe_section_ = section;
        o.editing_field_key_ = field;
        snprintf(o.probe_config_edit_value_buf_, sizeof(o.probe_config_edit_value_buf_), "%s",
                 value.c_str());
    }
    static void save(ProbeOverlay& o) {
        o.handle_config_save();
    }
};
