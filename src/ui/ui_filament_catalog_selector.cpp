// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_catalog_selector.h"

#include "ui_icon_codepoints.h"
#include "ui_utils.h"

#include "filament_variants.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>

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
    preselect_anchor_id_.clear();
}

void FilamentCatalogSelector::populate() {
    catalog_ = helix::printer::FilamentCatalog::load_full(); // fresh load per open
    highlighted_id_.clear();
    preselect_anchor_id_.clear();
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

void FilamentCatalogSelector::preselect_first() {
    if (!highlighted_id_.empty())
        return; // keep an existing selection
    auto products = ordered_products_for(current_vendor(), current_type());
    if (products.empty())
        return;
    handle_row_selected(products.front()->id);
    // Remember the entry pick so a dropdown round-trip back to this vendor+type
    // re-checks the same product rather than the first row.
    preselect_anchor_id_ = highlighted_id_;
}

void FilamentCatalogSelector::set_preselect_on_change(bool enable) {
    preselect_on_change_ = enable;
}

void FilamentCatalogSelector::preselect_after_change() {
    auto products = ordered_products_for(current_vendor(), current_type());
    if (products.empty()) {
        // Genuinely no product for this vendor+type (e.g. a firmware-whitelisted
        // material we haven't seeded). Leave unchecked; the host decides what a
        // Save with no highlight means.
        if (on_selection_changed_)
            on_selection_changed_(nullptr);
        return;
    }
    // Prefer the anchor (the identity the host entered with) if it survived into
    // the rebuilt list — user navigated back to the original type.
    if (!preselect_anchor_id_.empty()) {
        for (const auto* p : products) {
            if (p->id == preselect_anchor_id_) {
                handle_row_selected(p->id);
                return;
            }
        }
    }
    handle_row_selected(products.front()->id);
}

void FilamentCatalogSelector::select_first_product_for_test() {
    auto products = ordered_products_for(current_vendor(), current_type());
    if (products.empty())
        return;
    handle_row_selected(products.front()->id);
}

std::vector<std::string> FilamentCatalogSelector::product_names_for_test() const {
    std::vector<std::string> names;
    for (const auto* p : ordered_products_for(current_vendor(), current_type()))
        names.push_back(p->name);
    return names;
}

std::vector<const helix::printer::EffectiveFilament*>
FilamentCatalogSelector::products_for_test() const {
    return ordered_products_for(current_vendor(), current_type());
}

void FilamentCatalogSelector::select_product_for_test(const std::string& product_id) {
    handle_row_selected(product_id);
}

void FilamentCatalogSelector::change_vendor_for_test(uint32_t index) {
    lv_obj_t* dd = find_child("vendor_dropdown");
    if (!dd)
        return;
    lv_dropdown_set_selected(dd, index);
    handle_vendor_changed();
}

void FilamentCatalogSelector::change_type_for_test(uint32_t index) {
    lv_obj_t* dd = find_child("type_dropdown");
    if (!dd)
        return;
    lv_dropdown_set_selected(dd, index);
    handle_type_changed();
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

namespace {

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string FilamentCatalogSelector::family_of(const std::string& type) {
    return filament::display_family(type);
}

bool FilamentCatalogSelector::type_allowed(const std::string& type) const {
    if (!allowed_types_)
        return true;
    // Case-insensitive match: a backend whitelist may spell a type differently
    // than the catalog ("pla" vs "PLA").
    const std::string type_lc = to_lower_copy(type);
    return std::any_of(allowed_types_->begin(), allowed_types_->end(),
                       [&](const std::string& a) { return to_lower_copy(a) == type_lc; });
}

void FilamentCatalogSelector::populate_type_dropdown() {
    lv_obj_t* dd = find_child("type_dropdown");
    if (!dd)
        return;

    // Headings are material FAMILIES, not raw types: PLA / PLA-CF / PLA-GF /
    // PLA-AERO / SILK collapse into one "PLA" entry. Variants stay separately
    // selectable as rows underneath — this is display grouping only, and the
    // `type` a selection emits is untouched.
    //
    // The whitelist is applied to each TYPE before its family is derived, so a
    // family heading only appears if at least one variant behind it is allowed,
    // and it never smuggles in a rejected sibling (AD5X allows PLA and PLA-CF
    // but not PLA-GF — the PLA heading must not surface PLA-GF).
    std::vector<std::string> families;
    std::set<std::string> seen_family;
    std::set<std::string> covered_types_lc; // whitelist types with a real product
    for (const auto& type : catalog_.types_for_brand(current_vendor())) {
        if (!type_allowed(type))
            continue;
        covered_types_lc.insert(to_lower_copy(type));
        std::string family = family_of(type);
        if (seen_family.insert(family).second)
            families.push_back(family);
    }
    std::sort(families.begin(), families.end());

    if (allowed_types_) {
        // Whitelist entries the catalog has no product for (e.g. AD5X "SILK"
        // under a vendor that stocks none) are still appended so users aren't
        // locked out of a firmware-supported material. These become their own
        // heading and keep the WHITELIST spelling, because current_type() is
        // read back as the material string on exactly this no-product path.
        for (const auto& allowed : *allowed_types_) {
            if (covered_types_lc.count(to_lower_copy(allowed)))
                continue;
            bool dup = std::any_of(families.begin(), families.end(), [&](const std::string& f) {
                return to_lower_copy(f) == to_lower_copy(allowed);
            });
            if (!dup)
                families.push_back(allowed);
        }
    }

    // Seed by the FAMILY of the requested type: a host seeding "PLA-CF" wants
    // the PLA heading open, then preselect lands on the PLA-CF product.
    const std::string seed_family = seed_type_ ? family_of(*seed_type_) : std::string{};
    std::string options;
    int seed_idx = 0;
    for (size_t i = 0; i < families.size(); ++i) {
        if (i)
            options += "\n";
        options += families[i];
        if (seed_type_ && (families[i] == seed_family || families[i] == *seed_type_))
            seed_idx = static_cast<int>(i);
    }
    spdlog::debug("[FilamentCatalogSelector] vendor='{}' -> {} family headings", current_vendor(),
                  families.size());
    lv_dropdown_set_options(dd, options.empty() ? "" : options.c_str());
    lv_dropdown_set_selected(dd, seed_idx);
}

std::vector<const helix::printer::EffectiveFilament*>
FilamentCatalogSelector::ordered_products_for(const std::string& vendor,
                                              const std::string& family) const {
    // Collect every product of this vendor whose type belongs to `family`.
    // Whitelist gating is per TYPE (see type_allowed): a heading is only as
    // permissive as the individual variants behind it.
    std::vector<const helix::printer::EffectiveFilament*> products;
    for (const auto* p : catalog_.products_for_brand(vendor)) {
        if (!type_allowed(p->type))
            continue;
        if (family_of(p->type) == family)
            products.push_back(p);
    }

    const std::string family_lc = to_lower_copy(family);
    // Variant grouping key: base-type products ("" sorts first) cluster ahead of
    // variants, and each variant type ("ASA-CF", "ASA-GF") forms its own
    // contiguous run so a heading reads as base-then-variants rather than one
    // interleaved alphabetical soup.
    auto variant_key = [&](const helix::printer::EffectiveFilament* p) -> std::string {
        std::string type_lc = to_lower_copy(p->type);
        return type_lc == family_lc ? std::string{} : type_lc;
    };
    // Within a variant run: 0 = the plain material whose name is just the type,
    // 1 = everything else (alphabetical), 2 = "Support..." (sunk to the bottom,
    // file order preserved).
    auto rank_of = [&](const helix::printer::EffectiveFilament* p) -> int {
        std::string name_lc = to_lower_copy(p->name);
        if (name_lc == to_lower_copy(p->type))
            return 0;
        if (name_lc.rfind("support", 0) == 0)
            return 2;
        return 1;
    };
    std::stable_sort(products.begin(), products.end(),
                     [&](const helix::printer::EffectiveFilament* a,
                         const helix::printer::EffectiveFilament* b) {
                         std::string ka = variant_key(a);
                         std::string kb = variant_key(b);
                         if (ka != kb)
                             return ka < kb;
                         int ra = rank_of(a);
                         int rb = rank_of(b);
                         if (ra != rb)
                             return ra < rb;
                         if (ra != 1)
                             return false; // stable within ranks 0 and 2
                         return to_lower_copy(a->name) < to_lower_copy(b->name);
                     });
    return products;
}

std::string
FilamentCatalogSelector::row_label_for_test(const helix::printer::EffectiveFilament* p) const {
    if (!p)
        return {};
    // Mirrors rebuild_product_list(): name, plus the variant chip when the row's
    // type differs from the family heading it sits under.
    const std::string family = current_type();
    if (!p->type.empty() && p->type != family)
        return p->name + " " + p->type;
    return p->name;
}

void FilamentCatalogSelector::rebuild_product_list() {
    lv_obj_t* list = find_child("product_list");
    if (!list)
        return;
    helix::ui::safe_clean_children(list);

    lv_color_t accent = theme_manager_get_color("primary");
    lv_color_t text_color = theme_manager_get_color("text");
    lv_color_t muted_color = theme_manager_get_color("text_muted");
    const char* body_font_name = lv_xml_get_const(nullptr, "font_body");
    const lv_font_t* body_font =
        body_font_name ? lv_xml_get_font(nullptr, body_font_name) : lv_font_get_default();
    const char* icon_font_name = lv_xml_get_const(nullptr, "icon_font_xs");
    const lv_font_t* icon_font =
        icon_font_name ? lv_xml_get_font(nullptr, icon_font_name) : body_font;
    const char* check_codepoint = ui_icon::lookup_codepoint("check");

    const std::string family = current_type();
    auto products = ordered_products_for(current_vendor(), family);
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

        // Variant chip: under a collapsed family heading the row's own type is
        // the only thing separating ASA-CF from ASA-GF, and it is exactly the
        // string this selection emits. Base-type rows carry no chip — the
        // heading already says it. Untranslated: material names are identifiers
        // (L070).
        if (!p->type.empty() && p->type != family) {
            lv_obj_t* variant_lbl = lv_label_create(row);
            lv_label_set_text(variant_lbl, p->type.c_str());
            lv_obj_set_style_text_font(variant_lbl, body_font, 0);
            lv_obj_set_style_text_color(variant_lbl, is_current ? accent : muted_color, 0);
            lv_obj_remove_flag(variant_lbl, LV_OBJ_FLAG_CLICKABLE);
        }

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
    highlighted_id_.clear();  // stale row no longer visible under the new vendor
    populate_type_dropdown(); // vendor changed -> types change
    rebuild_product_list();
    if (preselect_on_change_) {
        preselect_after_change(); // keep a checked row (invariant)
    } else if (on_selection_changed_) {
        on_selection_changed_(nullptr);
    }
}

void FilamentCatalogSelector::handle_type_changed() {
    highlighted_id_.clear(); // stale row no longer visible under the new type
    rebuild_product_list();
    if (preselect_on_change_) {
        preselect_after_change(); // keep a checked row (invariant)
    } else if (on_selection_changed_) {
        on_selection_changed_(nullptr);
    }
}

void FilamentCatalogSelector::handle_row_selected(const std::string& product_id) {
    highlighted_id_ = product_id;
    rebuild_product_list(); // redraw to move the checkmark
    if (on_selection_changed_)
        on_selection_changed_(highlighted());
}

} // namespace helix::ui
