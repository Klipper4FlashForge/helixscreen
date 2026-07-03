// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_catalog_picker.h"

#include <spdlog/spdlog.h>

#include "lvgl.h"

namespace helix::ui {

bool FilamentCatalogPickerModal::callbacks_registered_ = false;
FilamentCatalogPickerModal* FilamentCatalogPickerModal::active_instance_ = nullptr;

void FilamentCatalogPickerModal::register_callbacks() {
    if (callbacks_registered_) return;
    lv_xml_register_event_cb(nullptr, "catalog_picker_close_cb",
        [](lv_event_t*) { if (active_instance_) active_instance_->hide(); });
    lv_xml_register_event_cb(nullptr, "catalog_picker_vendor_changed_cb",
        [](lv_event_t*) { if (active_instance_) active_instance_->handle_vendor_changed(); });
    lv_xml_register_event_cb(nullptr, "catalog_picker_type_changed_cb",
        [](lv_event_t*) { if (active_instance_) active_instance_->handle_type_changed(); });
    lv_xml_register_event_cb(nullptr, "catalog_picker_select_cb",
        [](lv_event_t*) { if (active_instance_) active_instance_->handle_select_button(); });
    callbacks_registered_ = true;
}

void FilamentCatalogPickerModal::show(lv_obj_t* parent, std::optional<std::string> seed_type,
                                      SelectCallback on_select) {
    show(parent, std::move(seed_type), std::nullopt, std::move(on_select));
}

void FilamentCatalogPickerModal::show(lv_obj_t* parent, std::optional<std::string> seed_type,
                                      std::optional<std::vector<std::string>> allowed_types,
                                      SelectCallback on_select) {
    register_callbacks();
    catalog_ = helix::printer::FilamentCatalog::load_full();  // fresh load per open
    seed_type_ = std::move(seed_type);
    allowed_types_ = std::move(allowed_types);
    on_select_ = std::move(on_select);
    highlighted_id_.clear();
    Modal::show(parent);   // triggers on_show()
}

void FilamentCatalogPickerModal::on_show() {
    active_instance_ = this;
    populate_vendor_dropdown();
    populate_type_dropdown();
    rebuild_product_list();
}

void FilamentCatalogPickerModal::on_hide() {
    if (active_instance_ == this) active_instance_ = nullptr;
    // catalog_ stays until next show() reloads it; memory is bounded to one catalog.
}

std::string FilamentCatalogPickerModal::current_vendor() const {
    lv_obj_t* dd = lv_obj_find_by_name(dialog(), "vendor_dropdown");
    if (!dd) return {};
    uint32_t sel = lv_dropdown_get_selected(dd);
    return sel < vendor_order_.size() ? vendor_order_[sel] : std::string{};
}

std::string FilamentCatalogPickerModal::current_type() const {
    lv_obj_t* dd = lv_obj_find_by_name(dialog(), "type_dropdown");
    if (!dd) return {};
    char buf[64] = {};
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    return buf;
}

void FilamentCatalogPickerModal::populate_vendor_dropdown() {
    lv_obj_t* dd = lv_obj_find_by_name(dialog(), "vendor_dropdown");
    if (!dd) return;
    // "Generic" pinned first, then the rest (all_brands() is already sorted+deduped).
    vendor_order_.clear();
    vendor_order_.push_back("Generic");
    for (const auto& b : catalog_.all_brands()) {
        if (b != "Generic") vendor_order_.push_back(b);
    }
    std::string options;
    for (size_t i = 0; i < vendor_order_.size(); ++i) {
        if (i) options += "\n";
        options += vendor_order_[i];
    }
    lv_dropdown_set_options(dd, options.c_str());
    lv_dropdown_set_selected(dd, 0);  // Generic
}

// populate_type_dropdown / rebuild_product_list / handle_* stubbed here, filled in Task 3-4.
void FilamentCatalogPickerModal::populate_type_dropdown() {}
void FilamentCatalogPickerModal::rebuild_product_list() {}
void FilamentCatalogPickerModal::handle_vendor_changed() {}
void FilamentCatalogPickerModal::handle_type_changed() {}
void FilamentCatalogPickerModal::handle_row_selected(const std::string&) {}
void FilamentCatalogPickerModal::handle_select_button() {}

}  // namespace helix::ui
