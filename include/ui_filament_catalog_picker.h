// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "filament_catalog.h"
#include "ui_modal.h"

namespace helix::ui {

/// Offline branded-filament picker. Loads FilamentCatalog::load_full() for its open
/// duration (transient — freed on hide), presents linked Vendor->Type dropdowns driving
/// a product list, and emits one EffectiveFilament by value. Knows nothing about slots
/// or presets — callers decide what to do with the result.
class FilamentCatalogPickerModal : public Modal {
  public:
    using SelectCallback = std::function<void(const helix::printer::EffectiveFilament&)>;

    FilamentCatalogPickerModal() = default;
    ~FilamentCatalogPickerModal() override = default;
    FilamentCatalogPickerModal(const FilamentCatalogPickerModal&) = delete;
    FilamentCatalogPickerModal& operator=(const FilamentCatalogPickerModal&) = delete;

    /// seed_type: pre-select the Type dropdown (preset path); nullopt = free (AMS path).
    void show(lv_obj_t* parent, std::optional<std::string> seed_type, SelectCallback on_select);
    /// allowed_types: restrict the Type dropdown to this set (AMS backend whitelist).
    void show(lv_obj_t* parent, std::optional<std::string> seed_type,
              std::optional<std::vector<std::string>> allowed_types, SelectCallback on_select);

    [[nodiscard]] const char* get_name() const override { return "Filament Catalog Picker"; }
    [[nodiscard]] const char* component_name() const override { return "filament_catalog_picker"; }

  protected:
    void on_show() override;
    void on_hide() override;

  private:
    void populate_vendor_dropdown();
    void populate_type_dropdown();      // from current vendor (+ allowed_types filter)
    void rebuild_product_list();        // from current vendor+type
    void handle_vendor_changed();
    void handle_type_changed();
    void handle_row_selected(const std::string& product_id);
    void handle_select_button();        // confirm current highlighted row

    std::string current_vendor() const; // reads vendor_dropdown selected string
    std::string current_type() const;   // reads type_dropdown selected string

    static void register_callbacks();
    static bool callbacks_registered_;
    static FilamentCatalogPickerModal* active_instance_;

    // Default-constructed (empty/unindexed) — NOT loaded here. Loading a full catalog
    // in the member initializer would parse it every time an *owner* is constructed
    // (e.g. an AMS edit modal holding this as a member), even if the picker never
    // opens. show() below reloads it fresh on every open; that is the only load point.
    helix::printer::FilamentCatalog catalog_;
    std::optional<std::string> seed_type_;
    std::optional<std::vector<std::string>> allowed_types_;
    SelectCallback on_select_;
    std::string highlighted_id_;        // currently-selected product row id
    std::vector<std::string> vendor_order_;  // dropdown index -> brand (Generic pinned first)
};

}  // namespace helix::ui
