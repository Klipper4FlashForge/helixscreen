// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_screen_manager.h"

namespace helix {

void RemoteScreenManager::add_sink(std::unique_ptr<RemoteScreenSink> sink) {
    if (sink) {
        sinks_.push_back(std::move(sink));
    }
}

void RemoteScreenManager::start() {
    for (auto& s : sinks_) {
        s->start();
    }
}

void RemoteScreenManager::stop() {
    for (auto& s : sinks_) {
        s->stop();
    }
}

bool RemoteScreenManager::any_active() const {
    for (const auto& s : sinks_) {
        if (s->wants_frames()) {
            return true;
        }
    }
    return false;
}

void RemoteScreenManager::on_frame(const RemoteScreenFrame& frame) {
    if (sinks_.empty()) {
        return;
    }
    for (auto& s : sinks_) {
        if (s->wants_frames()) {
            s->on_frame(frame);
        }
    }
}

} // namespace helix
