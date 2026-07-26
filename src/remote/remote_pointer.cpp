// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_pointer.h"

#include <spdlog/spdlog.h>

namespace helix::remote {

RemotePointer& RemotePointer::instance() {
    static RemotePointer inst;
    return inst;
}

void RemotePointer::read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    auto& self = instance();
    data->point.x = self.x_.load(std::memory_order_acquire);
    data->point.y = self.y_.load(std::memory_order_acquire);
    data->state = self.pressed_.load(std::memory_order_acquire) ? LV_INDEV_STATE_PRESSED
                                                                : LV_INDEV_STATE_RELEASED;
    // Published last, so a transport-thread waiter that sees this count has
    // necessarily seen the state it was set against.
    self.reads_.fetch_add(1, std::memory_order_release);
}

bool RemotePointer::ensure_created() {
    if (indev_ != nullptr) {
        return true;
    }

    lv_display_t* disp = lv_display_get_default();
    if (disp == nullptr) {
        spdlog::warn("[RemotePointer] No default display — cannot create synthetic pointer");
        return false;
    }

    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        spdlog::error("[RemotePointer] lv_indev_create failed");
        return false;
    }

    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, read_cb);
    lv_indev_set_display(indev_, disp);

    spdlog::info("[RemotePointer] Synthetic pointer device created");
    return true;
}

void RemotePointer::set_state(int32_t x, int32_t y, bool pressed) {
    // Position before button state: a reader that observes the press has also
    // observed the coordinates it happened at, which is what LVGL's press
    // handling assumes.
    x_.store(x, std::memory_order_release);
    y_.store(y, std::memory_order_release);
    pressed_.store(pressed, std::memory_order_release);
}

uint64_t RemotePointer::read_count() const {
    return reads_.load(std::memory_order_acquire);
}

bool RemotePointer::is_created() const {
    return indev_ != nullptr;
}

int32_t RemotePointer::x() const {
    return x_.load(std::memory_order_acquire);
}

int32_t RemotePointer::y() const {
    return y_.load(std::memory_order_acquire);
}

bool RemotePointer::pressed() const {
    return pressed_.load(std::memory_order_acquire);
}

} // namespace helix::remote
