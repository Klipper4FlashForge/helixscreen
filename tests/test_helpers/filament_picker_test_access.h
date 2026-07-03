// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_filament_catalog_picker.h"

#include "lvgl.h"

namespace helix::ui {

// Test-only accessor for FilamentCatalogPickerModal's private selection state.
struct FilamentPickerTestAccess {
    // Highlights the first product in the currently selected vendor+type, as if the
    // user had tapped that row.
    static void select_first_product(FilamentCatalogPickerModal& m) {
        auto products = m.catalog_.products_for(m.current_vendor(), m.current_type());
        if (products.empty()) return;
        m.highlighted_id_ = products.front()->id;
        m.rebuild_product_list();
    }

    static void press_select(FilamentCatalogPickerModal& m) {
        m.handle_select_button();
    }

    // Drives the vendor dropdown to `index` and fires the same change handler the XML
    // "value_changed" event would trigger (types/list rebuild + highlighted_id_ reset).
    static void change_vendor(FilamentCatalogPickerModal& m, uint32_t index) {
        lv_obj_t* dd = lv_obj_find_by_name(m.dialog(), "vendor_dropdown");
        if (!dd) return;
        lv_dropdown_set_selected(dd, index);
        m.handle_vendor_changed();
    }

    static const std::string& highlighted_id(const FilamentCatalogPickerModal& m) {
        return m.highlighted_id_;
    }
};

} // namespace helix::ui
