// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_start_collector.h"

#include <spdlog/spdlog.h>

#include <atomic>

using json = nlohmann::json;

// ============================================================================
// STATIC PATTERN DEFINITIONS
// ============================================================================

// Phase detection patterns with progress weights
// Weights should roughly sum to 100% for typical PRINT_START macros
const std::vector<PrintStartCollector::PhasePattern> PrintStartCollector::phase_patterns_ = {
    // Homing (usually first step)
    {PrintStartPhase::HOMING, std::regex(R"(G28|Homing|Home All Axes|homing)", std::regex::icase),
     "Homing...", 10},

    // Heating - bed
    {PrintStartPhase::HEATING_BED,
     std::regex(R"(M190|M140\s+S[1-9]|Heating bed|Heat Bed|BED_TEMP|bed.*heat)", std::regex::icase),
     "Heating Bed...", 20},

    // Heating - nozzle
    {PrintStartPhase::HEATING_NOZZLE,
     std::regex(R"(M109|M104\s+S[1-9]|Heating (nozzle|hotend|extruder)|EXTRUDER_TEMP)",
                std::regex::icase),
     "Heating Nozzle...", 20},

    // Quad gantry level
    {PrintStartPhase::QGL,
     std::regex(R"(QUAD_GANTRY_LEVEL|quad.?gantry.?level|QGL)", std::regex::icase),
     "Leveling Gantry...", 15},

    // Z tilt adjust
    {PrintStartPhase::Z_TILT, std::regex(R"(Z_TILT_ADJUST|z.?tilt.?adjust)", std::regex::icase),
     "Z Tilt Adjust...", 15},

    // Bed mesh
    {PrintStartPhase::BED_MESH,
     std::regex(R"(BED_MESH_CALIBRATE|BED_MESH_PROFILE\s+LOAD=|Loading bed mesh|mesh.*load)",
                std::regex::icase),
     "Loading Bed Mesh...", 10},

    // Nozzle cleaning
    {PrintStartPhase::CLEANING,
     std::regex(R"(CLEAN_NOZZLE|NOZZLE_CLEAN|WIPE_NOZZLE|nozzle.?wipe|clean.?nozzle)",
                std::regex::icase),
     "Cleaning Nozzle...", 5},

    // Purge line
    {PrintStartPhase::PURGING,
     std::regex(R"(VORON_PURGE|LINE_PURGE|PURGE_LINE|Prime.?Line|Priming|KAMP_.*PURGE|purge.?line)",
                std::regex::icase),
     "Purging...", 5},
};

// Pattern to detect PRINT_START macro invocation
const std::regex
    PrintStartCollector::print_start_pattern_(R"(PRINT_START|START_PRINT|_PRINT_START)",
                                              std::regex::icase);

// Pattern to detect print start completion (first layer indicator)
// Includes HELIX:READY for our custom macro integration
const std::regex PrintStartCollector::completion_pattern_(
    R"(SET_PRINT_STATS_INFO\s+CURRENT_LAYER=|LAYER:?\s*1\b|;LAYER:1|First layer|HELIX:READY)",
    std::regex::icase);

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PrintStartCollector::PrintStartCollector(MoonrakerClient& client, PrinterState& state)
    : client_(client), state_(state) {
    spdlog::debug("[PrintStartCollector] Constructed");
}

PrintStartCollector::~PrintStartCollector() {
    // Don't call stop() here - it uses client_ and state_ references which may
    // already be destroyed during static destruction order. Callers should
    // explicitly call stop() before letting the shared_ptr go out of scope.
    // Just mark as inactive to prevent any pending callbacks from running.
    active_.store(false);
    registered_.store(false);
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

void PrintStartCollector::start() {
    if (active_.load()) {
        spdlog::debug("[PrintStartCollector] Already active, ignoring start()");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // Record start time for timeout fallback
        printing_state_start_ = std::chrono::steady_clock::now();
        detected_phases_.clear();
        current_phase_ = PrintStartPhase::IDLE;
        print_start_detected_ = false;
    }
    fallbacks_enabled_.store(false); // Will be enabled after initial window

    // Generate unique handler name for G-code response callback
    static std::atomic<uint64_t> s_collector_id{0};
    handler_name_ = "print_start_collector_" + std::to_string(++s_collector_id);

    // Register for G-code responses (primary detection method)
    auto self = shared_from_this();
    client_.register_method_callback("notify_gcode_response", handler_name_,
                                     [self](const json& msg) { self->on_gcode_response(msg); });

    // Register for printer status updates (fallback for printers with KAMP/custom macros)
    // This watches for _START_PRINT.print_started, START_PRINT.preparation_done, etc.
    macro_subscription_id_ = client_.register_notify_update([self](const json& notification) {
        if (!self->active_.load())
            return;

        if (!notification.contains("params") || !notification["params"].is_array() ||
            notification["params"].empty()) {
            return;
        }

        const auto& status = notification["params"][0];

        // Check _START_PRINT.print_started (AD5M KAMP macro)
        if (status.contains("gcode_macro _START_PRINT")) {
            const auto& macro = status["gcode_macro _START_PRINT"];
            if (macro.contains("print_started") && macro["print_started"].is_boolean() &&
                macro["print_started"].get<bool>()) {
                spdlog::info("[PrintStartCollector] Macro signal: print_started=true");
                self->update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
                return;
            }
        }

        // Check START_PRINT.preparation_done
        if (status.contains("gcode_macro START_PRINT")) {
            const auto& macro = status["gcode_macro START_PRINT"];
            if (macro.contains("preparation_done") && macro["preparation_done"].is_boolean() &&
                macro["preparation_done"].get<bool>()) {
                spdlog::info("[PrintStartCollector] Macro signal: preparation_done=true");
                self->update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
                return;
            }
        }

        // Check _HELIX_STATE.print_started (our custom macro)
        if (status.contains("gcode_macro _HELIX_STATE")) {
            const auto& macro = status["gcode_macro _HELIX_STATE"];
            if (macro.contains("print_started") && macro["print_started"].is_boolean() &&
                macro["print_started"].get<bool>()) {
                spdlog::info("[PrintStartCollector] Helix macro signal: print_started=true");
                self->update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
                return;
            }
        }
    });

    registered_.store(true);
    active_.store(true);

    // Set initial state
    state_.set_print_start_state(PrintStartPhase::INITIALIZING, "Preparing Print...", 0);

    spdlog::info("[PrintStartCollector] Started monitoring (handler: {})", handler_name_);
}

void PrintStartCollector::stop() {
    // Mark inactive first to stop callbacks from processing
    bool was_active = active_.exchange(false);
    bool was_registered = registered_.exchange(false);

    if (was_registered) {
        client_.unregister_method_callback("notify_gcode_response", handler_name_);
        spdlog::debug("[PrintStartCollector] Unregistered G-code callback");
    }

    // Unregister macro subscription using atomic exchange to prevent double-unsubscribe
    SubscriptionId sub_id = macro_subscription_id_.exchange(0);
    if (sub_id != 0) {
        client_.unsubscribe_notify_update(sub_id);
        spdlog::debug("[PrintStartCollector] Unsubscribed from status updates");
    }

    fallbacks_enabled_.store(false);

    if (was_active) {
        state_.reset_print_start_state();
        spdlog::info("[PrintStartCollector] Stopped monitoring");
    }
}

void PrintStartCollector::reset() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        detected_phases_.clear();
        current_phase_ = PrintStartPhase::IDLE;
        print_start_detected_ = false;
        printing_state_start_ = std::chrono::steady_clock::now();
    }
    fallbacks_enabled_.store(false);

    if (active_.load()) {
        state_.set_print_start_state(PrintStartPhase::INITIALIZING, "Preparing Print...", 0);
    }

    spdlog::debug("[PrintStartCollector] Reset state");
}

void PrintStartCollector::enable_fallbacks() {
    if (!active_.load())
        return;

    fallbacks_enabled_.store(true);
    spdlog::debug("[PrintStartCollector] Fallback detection enabled");

    // Immediately check if any fallback conditions are already met
    check_fallback_completion();
}

void PrintStartCollector::check_fallback_completion() {
    if (!active_.load() || !fallbacks_enabled_.load()) {
        return;
    }

    // Check current phase under lock
    std::chrono::steady_clock::time_point start_time;
    PrintStartPhase current;
    bool print_start_was_detected;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // Already complete - nothing to do
        if (current_phase_ == PrintStartPhase::COMPLETE) {
            return;
        }
        current = current_phase_;
        print_start_was_detected = print_start_detected_;
        start_time = printing_state_start_;
    }

    // Get temperature data for proactive and completion fallback checks
    int ext_temp = lv_subject_get_int(state_.get_extruder_temp_subject());
    int ext_target = lv_subject_get_int(state_.get_extruder_target_subject());
    int bed_temp = lv_subject_get_int(state_.get_bed_temp_subject());
    int bed_target = lv_subject_get_int(state_.get_bed_target_subject());

    // Temps are in decidegrees (value * 10), targets may be 0 if not set
    bool bed_heating = bed_target > 0 && bed_temp < bed_target - TEMP_TOLERANCE_DECIDEGREES;
    bool nozzle_heating = ext_target > 0 && ext_temp < ext_target - TEMP_TOLERANCE_DECIDEGREES;
    bool temps_ready = !bed_heating && !nozzle_heating;

    // =========================================================================
    // PROACTIVE DETECTION: Detect PREPARING phase when heaters are ramping
    // This ensures "Preparing" shows even without HELIX:PHASE signals
    // =========================================================================
    if (current == PrintStartPhase::IDLE && !print_start_was_detected) {
        // We're in IDLE but collector is active - this means print just started
        // If heaters are ramping, we're definitely in PRINT_START preparation
        if (bed_heating || nozzle_heating) {
            // Determine which heating phase to show
            if (bed_heating && bed_temp < bed_target / 2) {
                // Bed is far from target - primary heating phase
                spdlog::info("[PrintStartCollector] Proactive: bed heating ({}/{})", bed_temp / 10,
                             bed_target / 10);
                update_phase(PrintStartPhase::HEATING_BED, "Heating Bed...");
            } else if (nozzle_heating) {
                // Nozzle heating (bed may be close or done)
                spdlog::info("[PrintStartCollector] Proactive: nozzle heating ({}/{})",
                             ext_temp / 10, ext_target / 10);
                update_phase(PrintStartPhase::HEATING_NOZZLE, "Heating Nozzle...");
            } else {
                // Generic initializing state
                spdlog::info("[PrintStartCollector] Proactive: initializing (heaters ramping)");
                update_phase(PrintStartPhase::INITIALIZING, "Preparing Print...");
            }
            return;
        }
    }

    // =========================================================================
    // COMPLETION DETECTION: Detect when PRINT_START is done
    // =========================================================================

    // Fallback 1: Layer count (slicer-dependent but reliable when present)
    int layer = lv_subject_get_int(state_.get_print_layer_current_subject());
    if (layer >= 1) {
        spdlog::info("[PrintStartCollector] Fallback: layer {} detected", layer);
        update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
        return;
    }

    // Fallback 2: Progress threshold with temps at target
    // 2% progress means file has advanced past typical preamble/macros
    int progress = lv_subject_get_int(state_.get_print_progress_subject());
    if (progress >= 2 && temps_ready) {
        spdlog::info("[PrintStartCollector] Fallback: progress {}% with temps ready", progress);
        update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
        return;
    }

    // Fallback 3: Timeout with temps near target (90% of target)
    bool temps_near = (ext_target <= 0 || ext_temp >= static_cast<int>(ext_target * 0.9)) &&
                      (bed_target <= 0 || bed_temp >= static_cast<int>(bed_target * 0.9));

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed > FALLBACK_TIMEOUT && temps_near) {
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        spdlog::info("[PrintStartCollector] Fallback: timeout ({} sec)", elapsed_sec);
        update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
        return;
    }
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void PrintStartCollector::on_gcode_response(const json& msg) {
    if (!active_.load()) {
        return;
    }

    // Parse notify_gcode_response format: {"method": "...", "params": ["line"]}
    if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
        return;
    }

    const std::string& line = msg["params"][0].get_ref<const std::string&>();

    // Skip empty lines and common noise
    if (line.empty() || line == "ok") {
        return;
    }

    spdlog::trace("[PrintStartCollector] G-code: {}", line);

    // Check for HELIX:PHASE signals (highest priority - definitive signals from plugin/macros)
    if (check_helix_phase_signal(line)) {
        return; // Signal handled
    }

    // Check for PRINT_START marker (once per session)
    bool should_set_initializing = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!print_start_detected_ && is_print_start_marker(line)) {
            print_start_detected_ = true;
            should_set_initializing = true;
        }
    }
    if (should_set_initializing) {
        update_phase(PrintStartPhase::INITIALIZING, "Starting Print...");
        spdlog::info("[PrintStartCollector] PRINT_START detected");
        return;
    }

    // Check for completion (layer 1 indicator)
    if (is_completion_marker(line)) {
        update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
        spdlog::info("[PrintStartCollector] Print start complete - layer 1 detected");
        // Note: The caller (main.cpp) should stop the collector when print state becomes PRINTING
        return;
    }

    // Check phase patterns
    check_phase_patterns(line);
}

void PrintStartCollector::check_phase_patterns(const std::string& line) {
    for (const auto& pattern : phase_patterns_) {
        if (std::regex_search(line, pattern.pattern)) {
            // Only update if this is a new phase
            bool is_new_phase = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (detected_phases_.find(pattern.phase) == detected_phases_.end()) {
                    detected_phases_.insert(pattern.phase);
                    is_new_phase = true;
                }
            }
            if (is_new_phase) {
                update_phase(pattern.phase, pattern.message);

                spdlog::debug("[PrintStartCollector] Detected phase: {} (progress: {}%)",
                              static_cast<int>(pattern.phase), calculate_progress());
            }
            return; // Only match first pattern per line
        }
    }
}

bool PrintStartCollector::check_helix_phase_signal(const std::string& line) {
    // Check for HELIX:PHASE:* signals (definitive markers from plugin/macros)
    static const char* HELIX_PHASE_PREFIX = "HELIX:PHASE:";
    constexpr size_t PREFIX_LEN = 12; // strlen("HELIX:PHASE:")

    // Find the prefix in the line (may have quotes or other wrappers)
    size_t pos = line.find(HELIX_PHASE_PREFIX);
    if (pos == std::string::npos) {
        return false;
    }

    // Extract the phase name
    std::string phase_name = line.substr(pos + PREFIX_LEN);
    // Trim trailing whitespace, quotes, etc.
    size_t end = phase_name.find_first_of(" \t\n\r\"'");
    if (end != std::string::npos) {
        phase_name = phase_name.substr(0, end);
    }

    spdlog::info("[PrintStartCollector] HELIX:PHASE signal: {}", phase_name);

    // Map phase name to PrintStartPhase
    if (phase_name == "STARTING" || phase_name == "START") {
        // Mark print start detected and transition to INITIALIZING
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            print_start_detected_ = true;
        }
        update_phase(PrintStartPhase::INITIALIZING, "Preparing Print...");
        return true;
    }

    if (phase_name == "COMPLETE" || phase_name == "DONE") {
        update_phase(PrintStartPhase::COMPLETE, "Starting Print...");
        spdlog::info("[PrintStartCollector] Print start complete via HELIX:PHASE signal");
        return true;
    }

    // Individual phases
    if (phase_name == "HOMING") {
        update_phase(PrintStartPhase::HOMING, "Homing...");
        return true;
    }
    if (phase_name == "HEATING_BED" || phase_name == "BED_HEATING") {
        update_phase(PrintStartPhase::HEATING_BED, "Heating Bed...");
        return true;
    }
    if (phase_name == "HEATING_NOZZLE" || phase_name == "NOZZLE_HEATING" ||
        phase_name == "HEATING_HOTEND") {
        update_phase(PrintStartPhase::HEATING_NOZZLE, "Heating Nozzle...");
        return true;
    }
    if (phase_name == "QGL" || phase_name == "QUAD_GANTRY_LEVEL") {
        update_phase(PrintStartPhase::QGL, "Leveling Gantry...");
        return true;
    }
    if (phase_name == "Z_TILT" || phase_name == "Z_TILT_ADJUST") {
        update_phase(PrintStartPhase::Z_TILT, "Z Tilt Adjust...");
        return true;
    }
    if (phase_name == "BED_MESH" || phase_name == "BED_LEVELING") {
        update_phase(PrintStartPhase::BED_MESH, "Loading Bed Mesh...");
        return true;
    }
    if (phase_name == "CLEANING" || phase_name == "NOZZLE_CLEAN") {
        update_phase(PrintStartPhase::CLEANING, "Cleaning Nozzle...");
        return true;
    }
    if (phase_name == "PURGING" || phase_name == "PURGE" || phase_name == "PRIMING") {
        update_phase(PrintStartPhase::PURGING, "Purging...");
        return true;
    }

    // Unknown phase - log but don't block
    spdlog::warn("[PrintStartCollector] Unknown HELIX:PHASE: {}", phase_name);
    return false;
}

void PrintStartCollector::update_phase(PrintStartPhase phase, const char* message) {
    int progress;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_phase_ = phase;
        progress = calculate_progress_locked();
    }
    // Call PrinterState outside the lock to avoid potential deadlocks
    state_.set_print_start_state(phase, message, progress);
}

int PrintStartCollector::calculate_progress() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return calculate_progress_locked();
}

int PrintStartCollector::calculate_progress_locked() const {
    int total_weight = 0;

    for (const auto& pattern : phase_patterns_) {
        if (detected_phases_.find(pattern.phase) != detected_phases_.end()) {
            total_weight += pattern.weight;
        }
    }

    // Cap at 95% - final 5% is for completion transition
    return std::min(total_weight, 95);
}

bool PrintStartCollector::is_print_start_marker(const std::string& line) const {
    return std::regex_search(line, print_start_pattern_);
}

bool PrintStartCollector::is_completion_marker(const std::string& line) const {
    return std::regex_search(line, completion_pattern_);
}
