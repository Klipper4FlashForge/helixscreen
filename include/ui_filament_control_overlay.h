// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "overlay_base.h"

#include <string>

// Navigation shell for the two filament controls. Data and subjects belong to
// FilamentPanel; OverlayBase owns rebuilds and the standard overlay lifecycle.
class FilamentControlOverlay : public OverlayBase {
  public:
    explicit FilamentControlOverlay(const char* component) : component_(component) {}
    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return component_;
    }
    void show(lv_obj_t* parent, const std::string& title);
    void hide();
    void destroy();
    explicit operator bool() const;

  private:
    const char* component_;
    std::string title_;
};
