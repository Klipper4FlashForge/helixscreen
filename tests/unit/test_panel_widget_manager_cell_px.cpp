// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_widget_manager_cell_px.cpp
 * @brief The pixel size a widget is told must match the size it is given.
 *
 * populate_widgets() promises each widget a pixel size via on_size_changed(),
 * then LVGL lays the same widget out from the grid descriptor. The descriptor
 * has inter-track gutters; the promise is computed from the ungapped content
 * width. LVGL's own geometry is therefore the oracle: whatever it renders is
 * what the widget should have been told.
 */

#include "../test_fixtures.h"
#include "config.h"
#include "grid_layout.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "layout_manager.h"
#include "panel_widget.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Records the pixel size populate_widgets() promised, alongside the LVGL
/// object it was promised for, so the promise can be checked against the
/// geometry the grid actually produced.
struct SizeOracleWidget : helix::PanelWidget {
    static lv_obj_t* s_obj;
    static int s_width_px;
    static int s_height_px;

    static void reset() {
        s_obj = nullptr;
        s_width_px = -1;
        s_height_px = -1;
    }

    void attach(lv_obj_t* widget_obj, lv_obj_t*) override {
        s_obj = widget_obj;
    }
    void detach() override {}
    void on_size_changed(int, int, int width_px, int height_px) override {
        s_width_px = width_px;
        s_height_px = height_px;
    }
    const char* id() const override {
        return "shutdown";
    }
    std::string get_component_name() const override {
        return "test_size_oracle_widget";
    }
};

lv_obj_t* SizeOracleWidget::s_obj = nullptr;
int SizeOracleWidget::s_width_px = -1;
int SizeOracleWidget::s_height_px = -1;

/// Swap a registry factory for the duration of a test and restore it after.
class ScopedOracleFactory {
  public:
    explicit ScopedOracleFactory(const char* id) : id_(id) {
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        original_ = def->factory;
        helix::register_widget_factory(id, [](const std::string&) {
            return std::unique_ptr<PanelWidget>(new SizeOracleWidget());
        });
    }
    ~ScopedOracleFactory() {
        helix::register_widget_factory(id_, original_);
    }

  private:
    const char* id_;
    WidgetFactory original_;
};

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Widget is told the pixel width the grid actually gave it",
                 "[manager][grid_metrics]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_size_oracle_widget",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    SizeOracleWidget::reset();

    // theme_manager_resolve_px_tokens() reads "ui_xml" as a relative path. Run
    // from anywhere but the repo root and every spacing token silently becomes
    // 0, the gutters vanish, and this test would pass without proving anything.
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    ScopedOracleFactory oracle("shutdown");

    const std::string panel_id = "test_cell_px_oracle";
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"},
                          {"widgets",
                           {{{"id", "shutdown"},
                             {"enabled", true},
                             {"col", 0},
                             {"row", 0},
                             {"colspan", 1},
                             {"rowspan", 1}}}}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);

    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 800, 480);
    process_lvgl(10);

    auto widgets = mgr.populate_widgets(panel_id, container, /*page_index=*/1);

    REQUIRE(SizeOracleWidget::s_obj != nullptr);
    REQUIRE(SizeOracleWidget::s_width_px > 0);

    lv_obj_update_layout(container);
    const int rendered_w = lv_obj_get_width(SizeOracleWidget::s_obj);

    // Tolerance of 1px absorbs LVGL's FR remainder distribution. The
    // gutter-blind promise is off by roughly a whole gutter, well outside it.
    INFO("promised=" << SizeOracleWidget::s_width_px << " rendered=" << rendered_w
                     << " gutter=" << theme_manager_get_spacing("space_xs"));
    REQUIRE(std::abs(SizeOracleWidget::s_width_px - rendered_w) <= 1);

    mgr.clear_panel_config(panel_id);
}
