// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include "calibration_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief "Share results" modal for the screws tilt panel
 *
 * Shows one row per bed screw (name, probed Z, adjustment) plus a QR code
 * encoding the same values as plain text, so a user can pull the numbers onto
 * a phone. There is no OS clipboard on a printer — the QR is the transport.
 *
 * Heap-allocated and self-deleting: construct with the result set, call
 * show_modal(), and the instance deletes itself once hidden.
 */
class ScrewsTiltShareModal : public Modal {
  public:
    /// Rows the XML component can render; results beyond this are QR-only.
    static constexpr size_t MAX_ROWS = 8;

    explicit ScrewsTiltShareModal(std::vector<ScrewTiltResult> results);

    const char* get_name() const override {
        return "Screws Tilt Share";
    }
    const char* component_name() const override {
        return "screws_tilt_share_modal";
    }

    /// Populate subjects, then show. Returns false if the XML failed to create.
    bool show_modal(lv_obj_t* parent);

    /// Plain-text payload encoded into the QR (exposed for logging/tests).
    [[nodiscard]] const std::string& share_text() const {
        return share_text_;
    }

  protected:
    void on_show() override;
    void on_hide() override;

  private:
    std::vector<ScrewTiltResult> results_;
    std::string share_text_;

    void create_qr_code();
};

} // namespace helix::ui
