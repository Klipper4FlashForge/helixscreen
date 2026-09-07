// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera_config_modal.h"

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "app_globals.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "printer_state.h"
#include "webcam_selection.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>

namespace helix {

CameraConfigModal* CameraConfigModal::s_active_ = nullptr;

CameraConfigModal::CameraConfigModal(const std::string& widget_id, const std::string& panel_id,
                                     SaveCallback on_save)
    : widget_id_(widget_id), panel_id_(panel_id), on_save_(std::move(on_save)) {
    init_subjects();
    s_active_ = this;
}

CameraConfigModal::~CameraConfigModal() {
    if (s_active_ == this)
        s_active_ = nullptr;
    deinit_subjects();
}

void CameraConfigModal::init_subjects() {
    if (subjects_initialized_)
        return;

    lv_subject_init_int(&rot_0_active_, 1);
    lv_subject_init_int(&rot_90_active_, 0);
    lv_subject_init_int(&rot_180_active_, 0);
    lv_subject_init_int(&rot_270_active_, 0);
    lv_subject_init_int(&flip_h_active_, 0);
    lv_subject_init_int(&flip_v_active_, 0);

    lv_xml_register_subject(nullptr, "cam_rot_0_active", &rot_0_active_);
    lv_xml_register_subject(nullptr, "cam_rot_90_active", &rot_90_active_);
    lv_xml_register_subject(nullptr, "cam_rot_180_active", &rot_180_active_);
    lv_xml_register_subject(nullptr, "cam_rot_270_active", &rot_270_active_);
    lv_xml_register_subject(nullptr, "cam_flip_h_active", &flip_h_active_);
    lv_xml_register_subject(nullptr, "cam_flip_v_active", &flip_v_active_);

    lv_subject_init_int(&source_count_, 0);
    lv_subject_init_int(&source_auto_active_, 1);
    lv_xml_register_subject(nullptr, "cam_source_count", &source_count_);
    lv_xml_register_subject(nullptr, "cam_source_auto_active", &source_auto_active_);
    for (size_t i = 0; i < MAX_SOURCES; ++i) {
        char key[40];
        lv_subject_init_int(&source_active_[i], 0);
        std::snprintf(key, sizeof(key), "cam_source_%zu_active", i);
        lv_xml_register_subject(nullptr, key, &source_active_[i]);
        lv_subject_init_string(&source_name_[i], source_name_buf_[i].data(), nullptr,
                               SOURCE_TEXT_LEN, "");
        std::snprintf(key, sizeof(key), "cam_source_%zu_name", i);
        lv_xml_register_subject(nullptr, key, &source_name_[i]);
        lv_subject_init_string(&source_note_[i], source_note_buf_[i].data(), nullptr,
                               SOURCE_TEXT_LEN, "");
        std::snprintf(key, sizeof(key), "cam_source_%zu_note", i);
        lv_xml_register_subject(nullptr, key, &source_note_[i]);
    }

    subjects_initialized_ = true;
}

void CameraConfigModal::deinit_subjects() {
    if (!subjects_initialized_)
        return;
    lv_subject_deinit(&rot_0_active_);
    lv_subject_deinit(&rot_90_active_);
    lv_subject_deinit(&rot_180_active_);
    lv_subject_deinit(&rot_270_active_);
    lv_subject_deinit(&flip_h_active_);
    lv_subject_deinit(&flip_v_active_);
    lv_subject_deinit(&source_count_);
    lv_subject_deinit(&source_auto_active_);
    for (size_t i = 0; i < MAX_SOURCES; ++i) {
        lv_subject_deinit(&source_active_[i]);
        lv_subject_deinit(&source_name_[i]);
        lv_subject_deinit(&source_note_[i]);
    }
    subjects_initialized_ = false;
}

void CameraConfigModal::on_show() {
    wire_ok_button("btn_primary");
    wire_cancel_button("btn_secondary");

    if (dialog())
        lv_obj_set_user_data(dialog(), this);

    // Load current config
    auto& wc = PanelWidgetManager::instance().get_widget_config(panel_id_);
    load_config(wc.get_widget_config(widget_id_));
    publish_sources(get_printer_state().get_webcams());

    spdlog::debug("[CameraConfig] Opened: source='{}', rotation={}, flip_h={}, flip_v={}", source_,
                  rotation_, flip_h_, flip_v_);
}

void CameraConfigModal::load_config(const nlohmann::json& config) {
    base_config_ = config.is_object() ? config : nlohmann::json::object();

    if (config.contains("rotation") && config["rotation"].is_number_integer())
        rotation_ = config["rotation"].get<int>();
    if (config.contains("flip_h") && config["flip_h"].is_boolean())
        flip_h_ = config["flip_h"].get<bool>();
    if (config.contains("flip_v") && config["flip_v"].is_boolean())
        flip_v_ = config["flip_v"].get<bool>();
    if (config.contains("source") && config["source"].is_string())
        source_ = config["source"].get<std::string>();

    sync_rotation_subjects();
    sync_flip_subjects();
    sync_source_subjects();
}

nlohmann::json CameraConfigModal::build_config() const {
    nlohmann::json config = base_config_;
    config["rotation"] = rotation_;
    config["flip_h"] = flip_h_;
    config["flip_v"] = flip_v_;
    if (source_.empty()) {
        config.erase("source");
    } else {
        config["source"] = source_;
    }
    return config;
}

void CameraConfigModal::on_ok() {
    nlohmann::json config = build_config();

    auto& wc = PanelWidgetManager::instance().get_widget_config(panel_id_);
    wc.set_widget_config(widget_id_, config);

    if (on_save_)
        on_save_(config);

    spdlog::info("[CameraConfig] Saved: source='{}', rotation={}, flip_h={}, flip_v={}", source_,
                 rotation_, flip_h_, flip_v_);
    hide();
}

void CameraConfigModal::publish_sources(const std::vector<WebcamInfo>& cams) {
    source_names_.clear();
    for (const auto& cam : cams) {
        if (cam.name.empty() || source_names_.size() >= MAX_SOURCES)
            continue; // The local-probe entry has no name; nothing to pick
        size_t i = source_names_.size();
        source_names_.push_back(cam.name);
        lv_subject_copy_string(&source_name_[i], cam.name.c_str());
        // What the user gets if they pick it: a live stream, snapshot polling
        // for a service CameraStream cannot decode, or (for now) nothing.
        const char* note = "";
        if (!cam.unavailable_reason.empty()) {
            note = lv_tr("Unavailable");
        } else if (!webcam::is_mjpeg_service(cam.service)) {
            note = lv_tr("Snapshot only");
        }
        lv_subject_copy_string(&source_note_[i], note);
    }
    for (size_t i = source_names_.size(); i < MAX_SOURCES; ++i) {
        lv_subject_copy_string(&source_name_[i], "");
        lv_subject_copy_string(&source_note_[i], "");
    }
    sync_source_subjects();
    // Set the count LAST: it drives the <repeat> expansion, and the rebuilt
    // rows read the row subjects as they are created.
    lv_subject_set_int(&source_count_, static_cast<int>(source_names_.size()));
}

void CameraConfigModal::select_source(int index) {
    if (index < 0 || static_cast<size_t>(index) >= source_names_.size()) {
        source_.clear();
    } else {
        source_ = source_names_[static_cast<size_t>(index)];
    }
    sync_source_subjects();
}

void CameraConfigModal::sync_source_subjects() {
    bool any = false;
    for (size_t i = 0; i < MAX_SOURCES; ++i) {
        bool active = i < source_names_.size() && source_names_[i] == source_;
        any = any || active;
        lv_subject_set_int(&source_active_[i], active ? 1 : 0);
    }
    // A saved name no row carries (camera gone from Moonraker) shows as
    // Automatic — which is the feed the widget falls back to.
    lv_subject_set_int(&source_auto_active_, any ? 0 : 1);
}

void CameraConfigModal::sync_rotation_subjects() {
    lv_subject_set_int(&rot_0_active_, rotation_ == 0 ? 1 : 0);
    lv_subject_set_int(&rot_90_active_, rotation_ == 90 ? 1 : 0);
    lv_subject_set_int(&rot_180_active_, rotation_ == 180 ? 1 : 0);
    lv_subject_set_int(&rot_270_active_, rotation_ == 270 ? 1 : 0);
}

void CameraConfigModal::sync_flip_subjects() {
    lv_subject_set_int(&flip_h_active_, flip_h_ ? 1 : 0);
    lv_subject_set_int(&flip_v_active_, flip_v_ ? 1 : 0);
}

// Static event callbacks — routed through the single active instance (s_active_).
void CameraConfigModal::on_source_clicked(lv_event_t* e) {
    // Row identity comes from the event_cb user_data ("${i}" of the repeat,
    // "-1" on the Automatic button).
    const char* ud = static_cast<const char*>(lv_event_get_user_data(e));
    if (auto* m = s_active_) {
        m->select_source(ud ? std::atoi(ud) : -1);
    }
}

void CameraConfigModal::on_rotate_0(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->rotation_ = 0;
        m->sync_rotation_subjects();
    }
}
void CameraConfigModal::on_rotate_90(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->rotation_ = 90;
        m->sync_rotation_subjects();
    }
}
void CameraConfigModal::on_rotate_180(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->rotation_ = 180;
        m->sync_rotation_subjects();
    }
}
void CameraConfigModal::on_rotate_270(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->rotation_ = 270;
        m->sync_rotation_subjects();
    }
}

void CameraConfigModal::on_flip_h_toggled(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->flip_h_ = !m->flip_h_;
        m->sync_flip_subjects();
    }
}

void CameraConfigModal::on_flip_v_toggled(lv_event_t* e) {
    (void)e;
    if (auto* m = s_active_) {
        m->flip_v_ = !m->flip_v_;
        m->sync_flip_subjects();
    }
}

} // namespace helix

#endif // HELIX_HAS_CAMERA
