// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_tool_chip.h"

#include "ui_temperature_utils.h"

#include "app_globals.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_parser.h"
#include "helix-xml/src/xml/lv_xml_widget.h"
#include "helix-xml/src/xml/parsers/lv_xml_obj_parser.h"
#include "helix_sparkline.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "subject_managed_panel.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

constexpr uint32_t TOOL_CARD_DATA_MAGIC = 0x544F4F4CU; // "TOOL"

// This widget adapts per-tool data to the same XML card used by Bed and Chamber.
// Its uniquely named subjects belong to this instance and are withdrawn on delete.
struct ToolCardData {
    uint32_t magic = TOOL_CARD_DATA_MAGIC;
    int index = -1;
    std::string heater;
    std::string current_name;
    std::string target_name;
    std::string temperature_name;
    char current_buf[24]{};
    char target_buf[24]{};
    lv_subject_t current_subject{};
    lv_subject_t target_subject{};
    lv_subject_t temperature_subject{};
    SubjectManager subjects;
    ObserverGuard current_observer;
    ObserverGuard target_observer;
    ObserverGuard tools_observer;
    lv_obj_t* chart = nullptr;
};

void bind_heater(ToolCardData* data) {
    const auto& tools = helix::ToolState::instance().tools();
    const std::string heater = data->index >= 0 && data->index < static_cast<int>(tools.size())
                                   ? tools[data->index].effective_heater()
                                   : std::string{};
    // Always rebind on a topology update: the subjects may have been replaced
    // even when the heater name is unchanged.
    data->current_observer.reset();
    data->target_observer.reset();
    data->heater = heater;
    lv_subject_set_int(&data->temperature_subject, 0);
    lv_subject_copy_string(&data->current_subject, "—");
    lv_subject_copy_string(&data->target_subject, "—");
    if (data->chart)
        lv_obj_invalidate(data->chart);
    if (heater.empty())
        return;

    auto& state = get_printer_state();
    SubjectLifetime current_lifetime;
    SubjectLifetime target_lifetime;
    data->current_observer = helix::ui::observe_int_sync<ToolCardData>(
        state.get_extruder_temp_subject(heater, current_lifetime), data,
        [](ToolCardData* d, int value) {
            lv_subject_set_int(&d->temperature_subject, value);
            std::snprintf(d->current_buf, sizeof(d->current_buf), "%d°",
                          helix::ui::temperature::deci_to_degrees(value));
            lv_subject_copy_string(&d->current_subject, d->current_buf);
            if (d->chart)
                lv_obj_invalidate(d->chart);
        },
        current_lifetime);
    data->target_observer = helix::ui::observe_int_sync<ToolCardData>(
        state.get_extruder_target_subject(heater, target_lifetime), data,
        [](ToolCardData* d, int value) {
            helix::ui::temperature::format_target_or_off(
                helix::ui::temperature::deci_to_degrees(value), d->target_buf,
                sizeof(d->target_buf), true);
            lv_subject_copy_string(&d->target_subject, d->target_buf);
        },
        target_lifetime);
}

void* tool_card_create(lv_xml_parser_state_t* state, const char** attrs) {
    auto data = std::make_unique<ToolCardData>();
    const char* index = attrs ? lv_xml_get_value_of(attrs, "index") : nullptr;
    data->index = index ? std::atoi(index) : -1;
    static uint64_t next_id = 0;
    const std::string prefix = "tool_card_" + std::to_string(++next_id);
    data->current_name = prefix + "_current";
    data->target_name = prefix + "_target";
    data->temperature_name = prefix + "_temperature";
    UI_MANAGED_SUBJECT_INT(data->temperature_subject, 0, data->temperature_name.c_str(),
                           data->subjects);
    UI_MANAGED_SUBJECT_STRING(data->current_subject, data->current_buf, "—",
                              data->current_name.c_str(), data->subjects);
    UI_MANAGED_SUBJECT_STRING(data->target_subject, data->target_buf, "—",
                              data->target_name.c_str(), data->subjects);

    const std::string title = "T" + std::to_string(data->index);
    const char* card_attrs[] = {
        "tool_index",
        index ? index : "-1",
        "card_flow",
        "row",
        "stripe_width",
        "2",
        "stripe_height",
        "100%",
        "body_width",
        "0",
        "body_height",
        "100%",
        "chart_width",
        "42%",
        "chart_height",
        "50%",
        "body_gap",
        "#space_xs",
        "body_pad_left",
        "#space_md",
        "action_size",
        "#button_height",
        "hide_tool_readout",
        "false",
        "hide_heater_readout",
        "true",
        "temperature_subject",
        data->temperature_name.c_str(),
        "heater",
        "tool",
        "title",
        title.c_str(),
        "accent",
        "#temp_gradient_hot",
        "current_subject",
        data->current_name.c_str(),
        "target_subject",
        data->target_name.c_str(),
        "callback",
        "on_filament_tool_actions",
        "hide_action",
        "false",
        "action_callback",
        "on_filament_tool_actions",
        nullptr,
    };
    auto* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));
    auto* card = static_cast<lv_obj_t*>(lv_xml_create(parent, "heater_summary_card", card_attrs));
    if (!card)
        return nullptr;
    data->chart = lv_obj_find_by_name(card, "tool_temperature_chart");
    if (data->chart) {
        helix::ui::HelixSparkline::set_history_reader(data->chart, [d = data.get()]() {
            return helix::ui::HelixSparkline::temperature_history(d->heater);
        });
    }
    auto* d = data.release();
    lv_obj_set_user_data(card, d);
    // DELETE has no declarative equivalent; releasing observers before the
    // subjects keeps topology replacement and hot reload safe.
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
            auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            auto* data = static_cast<ToolCardData*>(lv_obj_get_user_data(obj));
            lv_obj_set_user_data(obj, nullptr);
            delete data;
        },
        LV_EVENT_DELETE, nullptr);
    auto& tools = helix::ToolState::instance();
    d->tools_observer = helix::ui::observe_int_sync<ToolCardData>(
        tools.get_tools_version_subject(), d, [](ToolCardData* data, int) { bind_heater(data); },
        tools.get_subjects_lifetime());
    return card;
}

} // namespace

void ui_tool_chip_register_widget() {
    lv_xml_register_widget("tool_chip", tool_card_create, lv_xml_obj_apply);
    spdlog::trace("[ToolCard] Widget registered with XML system");
}

bool ui_tool_chip_is_valid(lv_obj_t* obj) {
    auto* data = obj ? static_cast<ToolCardData*>(lv_obj_get_user_data(obj)) : nullptr;
    return data && data->magic == TOOL_CARD_DATA_MAGIC;
}

int ui_tool_chip_get_index(lv_obj_t* obj) {
    auto* data = obj ? static_cast<ToolCardData*>(lv_obj_get_user_data(obj)) : nullptr;
    return data && data->magic == TOOL_CARD_DATA_MAGIC ? data->index : -1;
}
