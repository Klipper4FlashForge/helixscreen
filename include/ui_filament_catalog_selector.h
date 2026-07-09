// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_catalog.h"
#include "lvgl.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace helix::ui {

/// Embeddable branded-filament selector controller. Drives a
/// "filament_catalog_selector" XML fragment (vendor/type dropdowns + product
/// list) created by the host. Owns no chrome and no lifetime beyond the
/// attached fragment — hosts call attach()/populate() when the fragment
/// becomes relevant and detach() before its widget tree dies.
///
/// Multiple live instances are supported (standalone picker modal + the AMS
/// editor's filament-details view): static XML callbacks resolve the owning
/// instance by walking up from the event target to a registered fragment root.
class FilamentCatalogSelector {
  public:
    /// Fired whenever the highlighted product changes; nullptr = cleared.
    using SelectionChangedCallback =
        std::function<void(const helix::printer::EffectiveFilament* ef)>;

    FilamentCatalogSelector() = default;
    ~FilamentCatalogSelector();
    FilamentCatalogSelector(const FilamentCatalogSelector&) = delete;
    FilamentCatalogSelector& operator=(const FilamentCatalogSelector&) = delete;

    /// Register catalog_select_* XML callbacks (idempotent, process-wide).
    static void register_callbacks();

    /// Bind to a created "filament_catalog_selector" fragment root.
    void attach(lv_obj_t* fragment_root);
    /// Unbind (call before the fragment's widget tree is destroyed).
    void detach();

    /// seed_type: pre-select the Type dropdown; allowed_types: whitelist
    /// filter (case-insensitive), nullopt = all types.
    void configure(std::optional<std::string> seed_type,
                   std::optional<std::vector<std::string>> allowed_types);

    /// Load the catalog fresh and (re)fill dropdowns + product list.
    void populate();
    /// Free the ~356-product catalog between opens.
    void clear_catalog();

    /// Currently highlighted product, or nullptr. Pointer is valid until the
    /// next populate()/clear_catalog().
    [[nodiscard]] const helix::printer::EffectiveFilament* highlighted() const;

    /// Highlight the first product of the current vendor+type if nothing is
    /// highlighted yet (hosts that show an already-defined filament want its
    /// matching variant pre-checked rather than an unchecked list). Records the
    /// picked product as the preselect anchor so a later dropdown round-trip can
    /// restore it (see set_preselect_on_change()).
    void preselect_first();

    /// Opt-in: when enabled, a vendor/type dropdown change auto-highlights a
    /// product in the rebuilt list instead of leaving it unchecked — the
    /// anchor product if it survived into the new list, else the first row.
    /// Guarantees the list always shows a checked row so a host's Save can't
    /// silently drop an identity change. Empty rebuilt lists stay unchecked
    /// (host handles that case). Default off preserves the standalone picker's
    /// "nothing checked until the user taps a row" behavior.
    void set_preselect_on_change(bool enable);

    void set_selection_changed(SelectionChangedCallback cb);

    // === Introspection (tests + hosts) ===
    [[nodiscard]] std::string current_vendor() const;
    [[nodiscard]] std::string current_type() const;
    [[nodiscard]] std::string type_options() const;

    // === Test drivers (mirror the XML event paths) ===
    void select_first_product_for_test();
    void change_vendor_for_test(uint32_t index);
    void change_type_for_test(uint32_t index);

  private:
    void populate_vendor_dropdown();
    void populate_type_dropdown();
    void rebuild_product_list();
    void handle_vendor_changed();
    void handle_type_changed();
    void handle_row_selected(const std::string& product_id);
    /// After a dropdown rebuild with preselect_on_change_ set: highlight the
    /// anchor product if present in the new list, else the first row; notify
    /// nullptr only when the list is genuinely empty.
    void preselect_after_change();

    lv_obj_t* find_child(const char* name) const;

    static FilamentCatalogSelector* from_event(lv_event_t* e);
    static std::map<lv_obj_t*, FilamentCatalogSelector*>& registry();
    static bool callbacks_registered_;

    lv_obj_t* root_ = nullptr;
    helix::printer::FilamentCatalog catalog_;
    std::optional<std::string> seed_type_;
    std::optional<std::vector<std::string>> allowed_types_;
    std::string highlighted_id_;
    std::string preselect_anchor_id_; // product to restore on a dropdown round-trip
    bool preselect_on_change_ = false;
    std::vector<std::string> vendor_order_; // dropdown index -> brand
    SelectionChangedCallback on_selection_changed_;
};

} // namespace helix::ui
