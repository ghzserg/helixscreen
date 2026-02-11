// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temperature_history_manager.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>

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

bool TemperatureHistoryManager::add_sample_internal(const std::string& heater_name, int temp_centi,
                                                    int target_centi, int64_t timestamp_ms) {
    // Get or create heater history
    HeaterHistory& history = heaters_[heater_name];

    // Throttle: reject if within SAMPLE_INTERVAL_MS of last sample
    if (history.last_sample_ms > 0 &&
        (timestamp_ms - history.last_sample_ms) < SAMPLE_INTERVAL_MS) {
        return false;
    }

    // Store sample in circular buffer
    TempSample sample;
    sample.temp_centi = temp_centi;
    sample.target_centi = target_centi;
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

    int temp_centi = lv_subject_get_int(subject);
    // Read target from the manager's cached value
    int target_centi = ctx->manager->get_cached_target(ctx->heater_name);

    bool stored;
    {
        std::lock_guard<std::mutex> lock(ctx->manager->mutex_);
        stored =
            ctx->manager->add_sample_internal(ctx->heater_name, temp_centi, target_centi, now_ms());
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

    int target_centi = lv_subject_get_int(subject);

    ctx->manager->set_cached_target(ctx->heater_name, target_centi);

    // Update the most recent sample if it was stored very recently
    ctx->manager->update_recent_sample_target(ctx->heater_name, target_centi);
}

void TemperatureHistoryManager::subscribe_to_subjects() {
    // Subscribe to extruder temperature subject
    lv_subject_t* extruder_temp = printer_state_.get_extruder_temp_subject();
    if (extruder_temp != nullptr) {
        extruder_temp_ctx_ = std::make_unique<ObserverContext>();
        extruder_temp_ctx_->manager = this;
        extruder_temp_ctx_->heater_name = "extruder";
        extruder_temp_observer_ =
            ObserverGuard(extruder_temp, temp_observer_callback, extruder_temp_ctx_.get());
    }

    // Subscribe to extruder target subject
    lv_subject_t* extruder_target = printer_state_.get_extruder_target_subject();
    if (extruder_target != nullptr) {
        extruder_target_ctx_ = std::make_unique<ObserverContext>();
        extruder_target_ctx_->manager = this;
        extruder_target_ctx_->heater_name = "extruder";
        extruder_target_observer_ =
            ObserverGuard(extruder_target, target_observer_callback, extruder_target_ctx_.get());
    }

    // Subscribe to bed temperature subject
    lv_subject_t* bed_temp = printer_state_.get_bed_temp_subject();
    if (bed_temp != nullptr) {
        bed_temp_ctx_ = std::make_unique<ObserverContext>();
        bed_temp_ctx_->manager = this;
        bed_temp_ctx_->heater_name = "heater_bed";
        bed_temp_observer_ = ObserverGuard(bed_temp, temp_observer_callback, bed_temp_ctx_.get());
    }

    // Subscribe to bed target subject
    lv_subject_t* bed_target = printer_state_.get_bed_target_subject();
    if (bed_target != nullptr) {
        bed_target_ctx_ = std::make_unique<ObserverContext>();
        bed_target_ctx_->manager = this;
        bed_target_ctx_->heater_name = "heater_bed";
        bed_target_observer_ =
            ObserverGuard(bed_target, target_observer_callback, bed_target_ctx_.get());
    }
}

void TemperatureHistoryManager::unsubscribe_from_subjects() {
    // ObserverGuard::reset() handles nullptr checks and lv_is_initialized() safety
    extruder_temp_observer_.reset();
    extruder_target_observer_.reset();
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

void TemperatureHistoryManager::set_cached_target(const std::string& heater_name,
                                                  int target_centi) {
    if (heater_name == "extruder") {
        cached_extruder_target_ = target_centi;
    } else if (heater_name == "heater_bed") {
        cached_bed_target_ = target_centi;
    }
}

void TemperatureHistoryManager::update_recent_sample_target(const std::string& heater_name,
                                                            int target_centi) {
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
        recent.target_centi = target_centi;
    }
}
