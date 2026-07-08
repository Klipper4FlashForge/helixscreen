// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_catalog_selector.h"

#include "ui_icon_codepoints.h"
#include "ui_utils.h"

#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace helix::ui {

bool FilamentCatalogSelector::callbacks_registered_ = false;

std::map<lv_obj_t*, FilamentCatalogSelector*>& FilamentCatalogSelector::registry() {
    static std::map<lv_obj_t*, FilamentCatalogSelector*> instances;
    return instances;
}

FilamentCatalogSelector* FilamentCatalogSelector::from_event(lv_event_t* e) {
    // Walk up from the event target until we hit a registered fragment root.
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    while (obj) {
        auto it = registry().find(obj);
        if (it != registry().end()) {
            return it->second;
        }
        obj = lv_obj_get_parent(obj);
    }
    spdlog::warn("[FilamentCatalogSelector] Event with no attached selector");
    return nullptr;
}

void FilamentCatalogSelector::register_callbacks() {
    if (callbacks_registered_)
        return;
    lv_xml_register_event_cb(nullptr, "catalog_select_vendor_changed_cb", [](lv_event_t* e) {
        if (auto* self = from_event(e))
            self->handle_vendor_changed();
    });
    lv_xml_register_event_cb(nullptr, "catalog_select_type_changed_cb", [](lv_event_t* e) {
        if (auto* self = from_event(e))
            self->handle_type_changed();
    });
    callbacks_registered_ = true;
}

FilamentCatalogSelector::~FilamentCatalogSelector() {
    detach();
}

void FilamentCatalogSelector::attach(lv_obj_t* fragment_root) {
    detach();
    root_ = fragment_root;
    if (root_) {
        registry()[root_] = this;
    }
}

void FilamentCatalogSelector::detach() {
    if (root_) {
        registry().erase(root_);
        root_ = nullptr;
    }
}

void FilamentCatalogSelector::configure(std::optional<std::string> seed_type,
                                        std::optional<std::vector<std::string>> allowed_types) {
    seed_type_ = std::move(seed_type);
    allowed_types_ = std::move(allowed_types);
    highlighted_id_.clear();
}

void FilamentCatalogSelector::populate() {
    catalog_ = helix::printer::FilamentCatalog::load_full(); // fresh load per open
    highlighted_id_.clear();
    populate_vendor_dropdown();
    populate_type_dropdown();
    rebuild_product_list();
}

void FilamentCatalogSelector::clear_catalog() {
    catalog_ = helix::printer::FilamentCatalog{};
    highlighted_id_.clear();
}

const helix::printer::EffectiveFilament* FilamentCatalogSelector::highlighted() const {
    if (highlighted_id_.empty())
        return nullptr;
    return catalog_.resolve_id(highlighted_id_);
}

void FilamentCatalogSelector::set_selection_changed(SelectionChangedCallback cb) {
    on_selection_changed_ = std::move(cb);
}

lv_obj_t* FilamentCatalogSelector::find_child(const char* name) const {
    return root_ ? lv_obj_find_by_name(root_, name) : nullptr;
}

std::string FilamentCatalogSelector::current_vendor() const {
    lv_obj_t* dd = find_child("vendor_dropdown");
    if (!dd)
        return {};
    uint32_t sel = lv_dropdown_get_selected(dd);
    return sel < vendor_order_.size() ? vendor_order_[sel] : std::string{};
}

std::string FilamentCatalogSelector::current_type() const {
    lv_obj_t* dd = find_child("type_dropdown");
    if (!dd)
        return {};
    char buf[64] = {};
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    return buf;
}

std::string FilamentCatalogSelector::type_options() const {
    lv_obj_t* dd = find_child("type_dropdown");
    return dd ? std::string(lv_dropdown_get_options(dd)) : std::string();
}

void FilamentCatalogSelector::select_first_product_for_test() {
    auto products = catalog_.products_for(current_vendor(), current_type());
    if (products.empty())
        return;
    handle_row_selected(products.front()->id);
}

void FilamentCatalogSelector::change_vendor_for_test(uint32_t index) {
    lv_obj_t* dd = find_child("vendor_dropdown");
    if (!dd)
        return;
    lv_dropdown_set_selected(dd, index);
    handle_vendor_changed();
}

void FilamentCatalogSelector::populate_vendor_dropdown() {
    lv_obj_t* dd = find_child("vendor_dropdown");
    if (!dd)
        return;
    // "Generic" pinned first, then the rest (all_brands() is already sorted+deduped).
    vendor_order_.clear();
    vendor_order_.push_back("Generic");
    for (const auto& b : catalog_.all_brands()) {
        if (b != "Generic")
            vendor_order_.push_back(b);
    }
    std::string options;
    for (size_t i = 0; i < vendor_order_.size(); ++i) {
        if (i)
            options += "\n";
        options += vendor_order_[i];
    }
    lv_dropdown_set_options(dd, options.c_str());
    lv_dropdown_set_selected(dd, 0); // Generic
}

void FilamentCatalogSelector::populate_type_dropdown() {
    lv_obj_t* dd = find_child("type_dropdown");
    if (!dd)
        return;
    std::vector<std::string> types = catalog_.types_for_brand(current_vendor());
    if (allowed_types_) {
        // Case-insensitive match: a backend whitelist may spell a type
        // differently than the catalog ("pla" vs "PLA").
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        std::vector<std::string> allowed_lc;
        allowed_lc.reserve(allowed_types_->size());
        for (const auto& a : *allowed_types_)
            allowed_lc.push_back(to_lower(a));
        std::vector<std::string> filtered;
        for (const auto& t : types) {
            if (std::find(allowed_lc.begin(), allowed_lc.end(), to_lower(t)) != allowed_lc.end())
                filtered.push_back(t);
        }
        types.swap(filtered);
    }
    std::string options;
    int seed_idx = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i)
            options += "\n";
        options += types[i];
        if (seed_type_ && types[i] == *seed_type_)
            seed_idx = static_cast<int>(i);
    }
    lv_dropdown_set_options(dd, options.empty() ? "" : options.c_str());
    lv_dropdown_set_selected(dd, seed_idx);
}

void FilamentCatalogSelector::rebuild_product_list() {
    lv_obj_t* list = find_child("product_list");
    if (!list)
        return;
    helix::ui::safe_clean_children(list);

    lv_color_t accent = theme_manager_get_color("primary");
    lv_color_t text_color = theme_manager_get_color("text");
    const char* body_font_name = lv_xml_get_const(nullptr, "font_body");
    const lv_font_t* body_font =
        body_font_name ? lv_xml_get_font(nullptr, body_font_name) : lv_font_get_default();
    const char* icon_font_name = lv_xml_get_const(nullptr, "icon_font_xs");
    const lv_font_t* icon_font =
        icon_font_name ? lv_xml_get_font(nullptr, icon_font_name) : body_font;
    const char* check_codepoint = ui_icon::lookup_codepoint("check");

    auto products = catalog_.products_for(current_vendor(), current_type());
    for (const auto* p : products) {
        bool is_current = (highlighted_id_ == p->id);
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 8, 0);
        lv_obj_set_style_pad_gap(row, 8, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_name(row, p->id.c_str()); // identity for click handler (L069)

        lv_obj_t* indicator = lv_label_create(row);
        lv_obj_set_style_text_font(indicator, icon_font, 0);
        lv_obj_set_style_min_width(indicator, 16, 0);
        if (is_current && check_codepoint) {
            lv_label_set_text(indicator, check_codepoint);
            lv_obj_set_style_text_color(indicator, accent, 0);
        } else {
            lv_label_set_text(indicator, "");
        }
        lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, p->name.c_str());
        lv_obj_set_style_text_font(name_lbl, body_font, 0);
        lv_obj_set_style_text_color(name_lbl, is_current ? accent : text_color, 0);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_obj_remove_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);

        char temps[32];
        snprintf(temps, sizeof(temps), "%d\xC2\xB0 / %d\xC2\xB0", p->nozzle_recommended,
                 p->bed_temp);
        lv_obj_t* temp_lbl = lv_label_create(row);
        lv_label_set_text(temp_lbl, temps);
        lv_obj_set_style_text_font(temp_lbl, body_font, 0);
        lv_obj_set_style_text_color(temp_lbl, text_color, 0);
        lv_obj_remove_flag(temp_lbl, LV_OBJ_FLAG_CLICKABLE);

        // Dynamic row — moved code keeps its direct event hookup; instance is
        // resolved by walking up to the registered fragment root.
        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                lv_obj_t* row = lv_event_get_current_target_obj(e);
                if (!row)
                    return;
                FilamentCatalogSelector* self = from_event(e);
                const char* id = lv_obj_get_name(row);
                if (self && id)
                    self->handle_row_selected(id);
            },
            LV_EVENT_CLICKED, nullptr);
    }
}

void FilamentCatalogSelector::handle_vendor_changed() {
    highlighted_id_.clear(); // stale row no longer visible under the new vendor
    if (on_selection_changed_)
        on_selection_changed_(nullptr);
    populate_type_dropdown(); // vendor changed -> types change
    rebuild_product_list();
}

void FilamentCatalogSelector::handle_type_changed() {
    highlighted_id_.clear(); // stale row no longer visible under the new type
    if (on_selection_changed_)
        on_selection_changed_(nullptr);
    rebuild_product_list();
}

void FilamentCatalogSelector::handle_row_selected(const std::string& product_id) {
    highlighted_id_ = product_id;
    rebuild_product_list(); // redraw to move the checkmark
    if (on_selection_changed_)
        on_selection_changed_(highlighted());
}

} // namespace helix::ui
