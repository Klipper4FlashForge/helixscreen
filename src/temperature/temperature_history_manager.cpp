// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temperature_history_manager.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace helix;

// ============================================================================
// Construction / Destruction
// ============================================================================

TemperatureHistoryManager::TemperatureHistoryManager(PrinterState& printer_state)
    : printer_state_(printer_state) {
    // Pre-populate heater map with standard heaters
    heaters_["extruder"] = HeaterHistory{};
    heaters_["heater_bed"] = HeaterHistory{};

    // Subscribe to temperature subjects for automatic sample collection
    subscribe_to_subjects();

    spdlog::debug("TemperatureHistoryManager: initialized with {} heaters", heaters_.size());
}

TemperatureHistoryManager::~TemperatureHistoryManager() {
    unsubscribe_from_subjects();
    spdlog::debug("TemperatureHistoryManager: destroyed");
}

// ============================================================================
// Data Access (thread-safe reads)
// ============================================================================

std::vector<TempSample>
TemperatureHistoryManager::get_samples(const std::string& heater_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = heaters_.find(heater_name);
    if (it == heaters_.end()) {
        return {};
    }

    const HeaterHistory& history = it->second;
    std::vector<TempSample> result;
    result.reserve(static_cast<size_t>(history.count));

    if (history.count == 0) {
        return result;
    }

    // Calculate where the oldest sample is
    // If buffer is not full: oldest is at index 0
    // If buffer is full: oldest is at write_index (next to be overwritten)
    int oldest_index;
    int num_samples;

    if (history.count < HISTORY_SIZE) {
        // Buffer not full yet - samples start at 0
        oldest_index = 0;
        num_samples = history.count;
    } else {
        // Buffer full - oldest is at current write position
        oldest_index = history.write_index;
        num_samples = HISTORY_SIZE;
    }

    // Copy samples in chronological order (oldest first)
    for (int i = 0; i < num_samples; ++i) {
        int idx = (oldest_index + i) % HISTORY_SIZE;
        result.push_back(history.samples[static_cast<size_t>(idx)]);
    }

    return result;
}

std::vector<TempSample> TemperatureHistoryManager::get_samples_since(const std::string& heater_name,
                                                                     int64_t since_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = heaters_.find(heater_name);
    if (it == heaters_.end()) {
        return {};
    }

    const HeaterHistory& history = it->second;
    if (history.count == 0) {
        return {};
    }

    // Calculate where the oldest sample is
    int oldest_index;
    int num_samples;

    if (history.count < HISTORY_SIZE) {
        oldest_index = 0;
        num_samples = history.count;
    } else {
        oldest_index = history.write_index;
        num_samples = HISTORY_SIZE;
    }

    // Filter samples in chronological order, collecting only those after since_ms
    std::vector<TempSample> result;
    result.reserve(static_cast<size_t>(num_samples));

    for (int i = 0; i < num_samples; ++i) {
        int idx = (oldest_index + i) % HISTORY_SIZE;
        const TempSample& sample = history.samples[static_cast<size_t>(idx)];
        if (sample.timestamp_ms > since_ms) {
            result.push_back(sample);
        }
    }

    return result;
}

std::vector<std::string> TemperatureHistoryManager::get_heater_names() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> names;
    names.reserve(heaters_.size());

    for (const auto& [name, history] : heaters_) {
        names.push_back(name);
    }

    return names;
}

int TemperatureHistoryManager::get_sample_count(const std::string& heater_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = heaters_.find(heater_name);
    if (it == heaters_.end()) {
        return 0;
    }

    return std::min(it->second.count, HISTORY_SIZE);
}

// ============================================================================
// Observer Pattern
// ============================================================================

void TemperatureHistoryManager::add_observer(HistoryCallback* cb) {
    if (cb == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already registered
    auto it = std::find(observers_.begin(), observers_.end(), cb);
    if (it == observers_.end()) {
        observers_.push_back(cb);
    }
}

void TemperatureHistoryManager::remove_observer(HistoryCallback* cb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(observers_.begin(), observers_.end(), cb);
    if (it != observers_.end()) {
        observers_.erase(it);
    }
}

void TemperatureHistoryManager::notify_observers(const std::string& heater_name) {
    // Copy observers under lock, then call outside lock to avoid deadlock
    std::vector<HistoryCallback*> observers_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        observers_copy = observers_;
    }

    for (auto* cb : observers_copy) {
        if (cb != nullptr && *cb) {
            (*cb)(heater_name);
        }
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

bool TemperatureHistoryManager::add_sample_internal(const std::string& heater_name, int temp_deci,
                                                    int target_deci, int64_t timestamp_ms) {
    // Reject "no data" / disconnect / inactive-extruder readings BEFORE storing.
    // The temp subject momentarily reads 0 on disconnect, on partial Klipper
    // status updates, and for an idle extruder on a multi-tool printer (the U1).
    // Storing a 0 makes the history backfill/replay path draw a real data point
    // at the chart's 0°C floor — a solid vertical line dropping to the baseline
    // at the replayed live edge (the reported U1 artifact). The live-push filter
    // in TempGraphController only guards new pushes; the 0 enters here, via the
    // recorder, and is replayed later, so it must be rejected at this boundary —
    // the single source feeding every consumer (overlay, mini graph, panel).
    // Upper bound rejects obviously-bogus spikes (deci-degrees; 4000 = 400°C).
    constexpr int kMaxValidTempDeci = 4000;
    if (temp_deci <= 0 || temp_deci > kMaxValidTempDeci) {
        spdlog::debug("[TempHistory] dropping invalid sample for '{}': {} deci-°C", heater_name,
                      temp_deci);
        return false;
    }

    // Get or create heater history
    HeaterHistory& history = heaters_[heater_name];

    // Throttle: reject if within SAMPLE_INTERVAL_MS of last sample
    if (history.last_sample_ms > 0 &&
        (timestamp_ms - history.last_sample_ms) < SAMPLE_INTERVAL_MS) {
        return false;
    }

    // Store sample in circular buffer
    TempSample sample;
    sample.temp_deci = temp_deci;
    sample.target_deci = target_deci;
    sample.timestamp_ms = timestamp_ms;

    history.samples[static_cast<size_t>(history.write_index)] = sample;

    // Advance write index (circular)
    history.write_index = (history.write_index + 1) % HISTORY_SIZE;

    // Update count (capped at HISTORY_SIZE)
    if (history.count < HISTORY_SIZE) {
        history.count++;
    }

    // Update last sample time for throttling
    history.last_sample_ms = timestamp_ms;

    return true;
}

// ============================================================================
// Bulk Seeding
// ============================================================================

void TemperatureHistoryManager::seed_from_store(const TemperatureStore& store, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    int keys_seeded = 0;

    for (const auto& [key, series] : store) {
        const size_t n = series.temperatures.size();
        if (n == 0) {
            continue;
        }

        // Collect existing samples for this key (oldest first) so the seed can
        // merge with any live sample already recorded before the fetch returned.
        std::vector<TempSample> merged;
        auto it = heaters_.find(key);
        if (it != heaters_.end() && it->second.count > 0) {
            const HeaterHistory& h = it->second;
            int oldest_index;
            int num_samples;
            if (h.count < HISTORY_SIZE) {
                oldest_index = 0;
                num_samples = h.count;
            } else {
                oldest_index = h.write_index;
                num_samples = HISTORY_SIZE;
            }
            merged.reserve(static_cast<size_t>(num_samples) + n);
            for (int i = 0; i < num_samples; ++i) {
                int idx = (oldest_index + i) % HISTORY_SIZE;
                merged.push_back(h.samples[static_cast<size_t>(idx)]);
            }
        } else {
            merged.reserve(n);
        }

        // Append synthesized seed samples. Timestamp of sample i is offset back
        // from now_ms so the newest (i == n-1) lands exactly at now_ms.
        for (size_t i = 0; i < n; ++i) {
            TempSample s;
            s.temp_deci = static_cast<int>(std::lround(series.temperatures[i] * 10.0f));
            s.target_deci = (i < series.targets.size())
                                ? static_cast<int>(std::lround(series.targets[i] * 10.0f))
                                : 0;
            s.timestamp_ms = now_ms - static_cast<int64_t>(n - 1 - i) * SAMPLE_INTERVAL_MS;
            merged.push_back(s);
        }

        // Sort by timestamp. Stable so that, for an exact-timestamp collision,
        // the seed sample (appended after existing samples) stays after the
        // existing one and wins the de-dup below.
        std::stable_sort(merged.begin(), merged.end(),
                         [](const TempSample& a, const TempSample& b) {
                             return a.timestamp_ms < b.timestamp_ms;
                         });

        // Collapse exact-timestamp collisions, keeping the later (seed) sample.
        std::vector<TempSample> deduped;
        deduped.reserve(merged.size());
        for (const auto& sample : merged) {
            if (!deduped.empty() && deduped.back().timestamp_ms == sample.timestamp_ms) {
                deduped.back() = sample;
            } else {
                deduped.push_back(sample);
            }
        }

        // Keep only the newest HISTORY_SIZE samples.
        if (deduped.size() > static_cast<size_t>(HISTORY_SIZE)) {
            deduped.erase(deduped.begin(), deduped.begin() + static_cast<std::ptrdiff_t>(
                                                                 deduped.size() - HISTORY_SIZE));
        }

        // Rewrite the ring buffer from the merged result.
        HeaterHistory& hh = heaters_[key];
        for (size_t i = 0; i < deduped.size(); ++i) {
            hh.samples[i] = deduped[i];
        }
        hh.count = static_cast<int>(deduped.size());
        hh.write_index = hh.count % HISTORY_SIZE;
        hh.last_sample_ms = deduped.empty() ? 0 : deduped.back().timestamp_ms;

        spdlog::debug("[TempHistory] seeded '{}': {} samples (newest ts {})", key, hh.count,
                      hh.last_sample_ms);
        ++keys_seeded;
    }

    spdlog::debug("[TempHistory] seed_from_store: {} of {} store keys seeded", keys_seeded,
                  store.size());
}

// ============================================================================
// Subject Subscription
// ============================================================================

namespace {

/**
 * @brief Get current Unix timestamp in milliseconds
 */
int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

void TemperatureHistoryManager::temp_observer_callback(lv_observer_t* observer,
                                                       lv_subject_t* subject) {
    auto* ctx = static_cast<ObserverContext*>(lv_observer_get_user_data(observer));
    if (ctx == nullptr || ctx->manager == nullptr) {
        return;
    }

    // Skip the initial callback fired during subscription (value is just initial 0)
    if (!ctx->first_callback_skipped) {
        ctx->first_callback_skipped = true;
        return;
    }

    int temp_deci = lv_subject_get_int(subject);
    // Read target from the manager's cached value
    int target_deci = ctx->manager->get_cached_target(ctx->heater_name);

    bool stored;
    {
        std::lock_guard<std::mutex> lock(ctx->manager->mutex_);
        stored =
            ctx->manager->add_sample_internal(ctx->heater_name, temp_deci, target_deci, now_ms());
    }
    if (stored) {
        ctx->manager->notify_observers(ctx->heater_name);
    }
}

void TemperatureHistoryManager::target_observer_callback(lv_observer_t* observer,
                                                         lv_subject_t* subject) {
    auto* ctx = static_cast<ObserverContext*>(lv_observer_get_user_data(observer));
    if (ctx == nullptr || ctx->manager == nullptr) {
        return;
    }

    int target_deci = lv_subject_get_int(subject);

    ctx->manager->set_cached_target(ctx->heater_name, target_deci);

    // Update the most recent sample if it was stored very recently
    ctx->manager->update_recent_sample_target(ctx->heater_name, target_deci);
}

void TemperatureHistoryManager::subscribe_to_subjects() {
    // Subscribe to extruder temperature subject
    lv_subject_t* extruder_temp = printer_state_.get_active_extruder_temp_subject();
    if (extruder_temp != nullptr) {
        extruder_temp_ctx_ = std::make_unique<ObserverContext>();
        extruder_temp_ctx_->manager = this;
        extruder_temp_ctx_->heater_name = "extruder";
        extruder_temp_observer_ =
            ObserverGuard(extruder_temp, temp_observer_callback, extruder_temp_ctx_.get());
    }

    // Subscribe to extruder target subject
    lv_subject_t* extruder_target = printer_state_.get_active_extruder_target_subject();
    if (extruder_target != nullptr) {
        extruder_target_ctx_ = std::make_unique<ObserverContext>();
        extruder_target_ctx_->manager = this;
        extruder_target_ctx_->heater_name = "extruder";
        extruder_target_observer_ =
            ObserverGuard(extruder_target, target_observer_callback, extruder_target_ctx_.get());
    }

    // Subscribe to bed temperature subject (lifetime token for reconnection safety #734)
    lv_subject_t* bed_temp = printer_state_.get_bed_temp_subject(bed_temp_lifetime_);
    if (bed_temp != nullptr) {
        bed_temp_ctx_ = std::make_unique<ObserverContext>();
        bed_temp_ctx_->manager = this;
        bed_temp_ctx_->heater_name = "heater_bed";
        bed_temp_observer_ = ObserverGuard(bed_temp, temp_observer_callback, bed_temp_ctx_.get());
        bed_temp_observer_.set_alive_token(bed_temp_lifetime_);
    }

    // Subscribe to bed target subject
    lv_subject_t* bed_target = printer_state_.get_bed_target_subject(bed_target_lifetime_);
    if (bed_target != nullptr) {
        bed_target_ctx_ = std::make_unique<ObserverContext>();
        bed_target_ctx_->manager = this;
        bed_target_ctx_->heater_name = "heater_bed";
        bed_target_observer_ =
            ObserverGuard(bed_target, target_observer_callback, bed_target_ctx_.get());
        bed_target_observer_.set_alive_token(bed_target_lifetime_);
    }
}

void TemperatureHistoryManager::unsubscribe_from_subjects() {
    // ObserverGuard::reset() handles nullptr checks and lv_is_initialized() safety
    extruder_temp_observer_.reset();
    extruder_target_observer_.reset();
    bed_temp_lifetime_.reset();
    bed_target_lifetime_.reset();
    bed_temp_observer_.reset();
    bed_target_observer_.reset();
}

// ============================================================================
// Cached Target Methods
// ============================================================================

int TemperatureHistoryManager::get_cached_target(const std::string& heater_name) const {
    if (heater_name == "extruder") {
        return cached_extruder_target_;
    } else if (heater_name == "heater_bed") {
        return cached_bed_target_;
    }
    return 0;
}

void TemperatureHistoryManager::set_cached_target(const std::string& heater_name, int target_deci) {
    if (heater_name == "extruder") {
        cached_extruder_target_ = target_deci;
    } else if (heater_name == "heater_bed") {
        cached_bed_target_ = target_deci;
    }
}

void TemperatureHistoryManager::update_recent_sample_target(const std::string& heater_name,
                                                            int target_deci) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = heaters_.find(heater_name);
    if (it == heaters_.end() || it->second.count == 0) {
        return;
    }

    HeaterHistory& history = it->second;

    // Find the most recent sample
    int recent_idx = (history.write_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    TempSample& recent = history.samples[static_cast<size_t>(recent_idx)];

    // Check if it was stored recently (within RECENT_SAMPLE_WINDOW_MS)
    using namespace std::chrono;
    int64_t current_ms =
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    int64_t age_ms = current_ms - recent.timestamp_ms;

    // Always update if sample was stored very recently
    // Use a generous window since temp and target are typically set together
    if (age_ms <= RECENT_SAMPLE_WINDOW_MS) {
        recent.target_deci = target_deci;
    }
}
