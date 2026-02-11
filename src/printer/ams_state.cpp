// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ams_state.cpp
 * @brief Multi-filament system state singleton with async backend callbacks
 *
 * @pattern Singleton with static s_shutdown_flag atomic for callback safety
 * @threading Updated from WebSocket callbacks; shutdown flag prevents post-destruction access
 * @gotchas MoonrakerClient may be destroyed during static destruction
 *
 * @see ams_backend_afc.cpp, ams_backend_toolchanger.cpp
 */

#include "ams_state.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "format_utils.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <unordered_map>

// Async callback data for thread-safe LVGL updates
namespace {

// Shutdown flag to prevent async callbacks from accessing destroyed singleton
static std::atomic<bool> s_shutdown_flag{false};

// Polling interval for Spoolman weight updates (30 seconds)
static constexpr uint32_t SPOOLMAN_POLL_INTERVAL_MS = 30000;

struct AsyncSyncData {
    bool full_sync;
    int slot_index; // Only used if full_sync == false
};

} // namespace

AmsState& AmsState::instance() {
    static AmsState instance;
    return instance;
}

const char* AmsState::get_logo_path(const std::string& type_name) {
    // Normalize to lowercase for matching
    std::string lower_name = type_name;
    for (auto& c : lower_name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Strip common suffixes like " (mock)", " (test)", etc.
    size_t paren_pos = lower_name.find(" (");
    if (paren_pos != std::string::npos) {
        lower_name = lower_name.substr(0, paren_pos);
    }

    // Map system names to logo paths
    // Note: All logos are 64x64 white-on-transparent PNGs
    static const std::unordered_map<std::string, const char*> logo_map = {
        // AFC/Box Turtle (AFC firmware only runs on Box Turtle hardware)
        {"afc", "A:assets/images/ams/box_turtle_64.png"},
        {"box turtle", "A:assets/images/ams/box_turtle_64.png"},
        {"box_turtle", "A:assets/images/ams/box_turtle_64.png"},
        {"boxturtle", "A:assets/images/ams/box_turtle_64.png"},

        // Happy Hare - generic firmware, defaults to ERCF logo
        // (most common hardware running Happy Hare)
        {"happy hare", "A:assets/images/ams/ercf_64.png"},
        {"happy_hare", "A:assets/images/ams/ercf_64.png"},
        {"happyhare", "A:assets/images/ams/ercf_64.png"},

        // Specific hardware types (when detected or configured)
        {"ercf", "A:assets/images/ams/ercf_64.png"},
        {"3ms", "A:assets/images/ams/3ms_64.png"},
        {"tradrack", "A:assets/images/ams/tradrack_64.png"},
        {"mmx", "A:assets/images/ams/mmx_64.png"},
        {"night owl", "A:assets/images/ams/night_owl_64.png"},
        {"night_owl", "A:assets/images/ams/night_owl_64.png"},
        {"nightowl", "A:assets/images/ams/night_owl_64.png"},
        {"quattro box", "A:assets/images/ams/quattro_box_64.png"},
        {"quattro_box", "A:assets/images/ams/quattro_box_64.png"},
        {"quattrobox", "A:assets/images/ams/quattro_box_64.png"},
        {"btt vivid", "A:assets/images/ams/btt_vivid_64.png"},
        {"btt_vivid", "A:assets/images/ams/btt_vivid_64.png"},
        {"bttvivid", "A:assets/images/ams/btt_vivid_64.png"},
        {"vivid", "A:assets/images/ams/btt_vivid_64.png"},
        {"kms", "A:assets/images/ams/kms_64.png"},
    };

    auto it = logo_map.find(lower_name);
    if (it != logo_map.end()) {
        return it->second;
    }
    return nullptr;
}

AmsState::AmsState() {
    std::memset(action_detail_buf_, 0, sizeof(action_detail_buf_));
    std::memset(system_name_buf_, 0, sizeof(system_name_buf_));
    std::memset(current_material_text_buf_, 0, sizeof(current_material_text_buf_));
    std::memset(current_slot_text_buf_, 0, sizeof(current_slot_text_buf_));
    std::memset(current_weight_text_buf_, 0, sizeof(current_weight_text_buf_));
}

AmsState::~AmsState() {
    // Signal shutdown to prevent async callbacks from accessing this instance
    s_shutdown_flag.store(true, std::memory_order_release);

    // Clean up Spoolman poll timer if still active (check LVGL is initialized
    // to avoid crash during static destruction order issues)
    if (spoolman_poll_timer_ && lv_is_initialized()) {
        lv_timer_delete(spoolman_poll_timer_);
        spoolman_poll_timer_ = nullptr;
    }

    // Note: During static destruction, the MoonrakerClient may already be destroyed.
    // We just release the backend without calling stop() to avoid accessing
    // potentially destroyed dependencies. The RAII SubscriptionGuard in the backend
    // will handle cleanup safely.
    backend_.reset();
}

void AmsState::init_subjects(bool register_xml) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (initialized_) {
        return;
    }

    spdlog::trace("[AMS State] Initializing subjects");

    // System-level subjects
    INIT_SUBJECT_INT(ams_type, static_cast<int>(AmsType::NONE), subjects_, register_xml);
    INIT_SUBJECT_INT(ams_action, static_cast<int>(AmsAction::IDLE), subjects_, register_xml);
    INIT_SUBJECT_INT(current_slot, -1, subjects_, register_xml);
    INIT_SUBJECT_INT(ams_current_tool, -1, subjects_, register_xml);
    // These subjects need ams_ prefix for XML but member vars don't have it
    lv_subject_init_int(&filament_loaded_, 0);
    subjects_.register_subject(&filament_loaded_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_filament_loaded", &filament_loaded_);

    lv_subject_init_int(&bypass_active_, 0);
    subjects_.register_subject(&bypass_active_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_bypass_active", &bypass_active_);

    lv_subject_init_int(&supports_bypass_, 0);
    subjects_.register_subject(&supports_bypass_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_supports_bypass", &supports_bypass_);
    INIT_SUBJECT_INT(ams_slot_count, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(slots_version, 0, subjects_, register_xml);

    // String subjects (buffer names don't match macro convention)
    lv_subject_init_string(&ams_action_detail_, action_detail_buf_, nullptr,
                           sizeof(action_detail_buf_), "");
    subjects_.register_subject(&ams_action_detail_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_action_detail", &ams_action_detail_);

    lv_subject_init_string(&ams_system_name_, system_name_buf_, nullptr, sizeof(system_name_buf_),
                           "");
    subjects_.register_subject(&ams_system_name_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_system_name", &ams_system_name_);

    INIT_SUBJECT_STRING(ams_current_tool_text, "---", subjects_, register_xml);

    // Filament path visualization subjects
    INIT_SUBJECT_INT(path_topology, static_cast<int>(PathTopology::HUB), subjects_, register_xml);
    INIT_SUBJECT_INT(path_active_slot, -1, subjects_, register_xml);
    INIT_SUBJECT_INT(path_filament_segment, static_cast<int>(PathSegment::NONE), subjects_,
                     register_xml);
    INIT_SUBJECT_INT(path_error_segment, static_cast<int>(PathSegment::NONE), subjects_,
                     register_xml);
    INIT_SUBJECT_INT(path_anim_progress, 0, subjects_, register_xml);

    // Dryer subjects (for AMS systems with integrated drying)
    INIT_SUBJECT_INT(dryer_supported, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(dryer_active, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(dryer_current_temp, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(dryer_target_temp, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(dryer_remaining_min, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(dryer_progress_pct, -1, subjects_, register_xml);
    INIT_SUBJECT_STRING(dryer_current_temp_text, "---", subjects_, register_xml);
    INIT_SUBJECT_STRING(dryer_target_temp_text, "---", subjects_, register_xml);
    INIT_SUBJECT_STRING(dryer_time_text, "", subjects_, register_xml);

    // Dryer modal editing subjects
    INIT_SUBJECT_STRING(dryer_modal_temp_text, "55°C", subjects_, register_xml);
    INIT_SUBJECT_STRING(dryer_modal_duration_text, "4h", subjects_, register_xml);

    // Currently Loaded display subjects (for reactive UI binding)
    // These subjects need ams_ prefix for XML but member vars don't have it
    lv_subject_init_string(&current_material_text_, current_material_text_buf_, nullptr,
                           sizeof(current_material_text_buf_), "---");
    subjects_.register_subject(&current_material_text_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_current_material_text", &current_material_text_);

    lv_subject_init_string(&current_slot_text_, current_slot_text_buf_, nullptr,
                           sizeof(current_slot_text_buf_), "None");
    subjects_.register_subject(&current_slot_text_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_current_slot_text", &current_slot_text_);

    lv_subject_init_string(&current_weight_text_, current_weight_text_buf_, nullptr,
                           sizeof(current_weight_text_buf_), "");
    subjects_.register_subject(&current_weight_text_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_current_weight_text", &current_weight_text_);

    lv_subject_init_int(&current_has_weight_, 0);
    subjects_.register_subject(&current_has_weight_);
    if (register_xml)
        lv_xml_register_subject(nullptr, "ams_current_has_weight", &current_has_weight_);

    INIT_SUBJECT_INT(current_color, 0x505050, subjects_, register_xml);

    // Per-slot subjects (dynamic names require manual init)
    char name_buf[32];
    for (int i = 0; i < MAX_SLOTS; ++i) {
        lv_subject_init_int(&slot_colors_[i], static_cast<int>(AMS_DEFAULT_SLOT_COLOR));
        subjects_.register_subject(&slot_colors_[i]);
        if (register_xml) {
            snprintf(name_buf, sizeof(name_buf), "ams_slot_%d_color", i);
            lv_xml_register_subject(nullptr, name_buf, &slot_colors_[i]);
        }

        lv_subject_init_int(&slot_statuses_[i], static_cast<int>(SlotStatus::UNKNOWN));
        subjects_.register_subject(&slot_statuses_[i]);
        if (register_xml) {
            snprintf(name_buf, sizeof(name_buf), "ams_slot_%d_status", i);
            lv_xml_register_subject(nullptr, name_buf, &slot_statuses_[i]);
        }
    }

    // Ask the factory for a backend. In mock mode, it returns a mock backend.
    // In real mode with no printer connected, it returns nullptr.
    // This keeps mock/real decision entirely in the factory.
    if (!backend_) {
        auto backend = AmsBackend::create(AmsType::NONE);
        if (backend) {
            backend->start();
            set_backend(std::move(backend));
            sync_from_backend();
            spdlog::debug("[AMS State] Backend initialized via factory ({} slots)",
                          lv_subject_get_int(&ams_slot_count_));
        }
    }

    // Create observer for print state changes to auto-refresh Spoolman weights.
    // Refreshes when print starts, ends, or pauses to keep weight data current.
    using helix::ui::observe_int_sync;
    print_state_observer_ = observe_int_sync<AmsState>(
        get_printer_state().get_print_state_enum_subject(), this, [](AmsState* self, int state) {
            auto print_state = static_cast<PrintJobState>(state);
            // Refresh on: PRINTING (start), COMPLETE (end), PAUSED (pause/resume)
            if (print_state == PrintJobState::PRINTING || print_state == PrintJobState::COMPLETE ||
                print_state == PrintJobState::PAUSED) {
                spdlog::debug("[AmsState] Print state changed to {}, refreshing Spoolman weights",
                              static_cast<int>(print_state));
                self->refresh_spoolman_weights();
            }
        });

    initialized_ = true;
}

void AmsState::deinit_subjects() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    spdlog::trace("[AMS State] Deinitializing subjects");

    // Use SubjectManager for automatic cleanup of all registered subjects
    subjects_.deinit_all();

    initialized_ = false;
    spdlog::trace("[AMS State] Subjects deinitialized");
}

void AmsState::init_backend_from_hardware(const helix::PrinterDiscovery& hardware,
                                          MoonrakerAPI* api, MoonrakerClient* client) {
    // Skip if no MMU or tool changer detected
    if (!hardware.has_mmu() && !hardware.has_tool_changer()) {
        spdlog::debug(
            "[AMS State] No MMU or tool changer detected, skipping backend initialization");
        return;
    }

    // Skip if already in mock mode (mock backend was created at startup)
    if (get_runtime_config()->should_mock_ams()) {
        spdlog::debug("[AMS State] Mock mode active, skipping real backend initialization");
        return;
    }

    // Check if backend already exists (with lock)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (backend_) {
            spdlog::debug("[AMS State] Backend already exists, skipping initialization");
            return;
        }
    }

    AmsType detected_type = hardware.mmu_type();
    spdlog::info("[AMS State] Detected MMU system: {}", ams_type_to_string(detected_type));

    auto backend = AmsBackend::create(detected_type, api, client);
    if (backend) {
        // Pass discovered configuration to backend through base class interface.
        backend->set_discovered_lanes(hardware.afc_lane_names(), hardware.afc_hub_names());
        backend->set_discovered_tools(hardware.tool_names());

        // Set backend (registers event callback)
        set_backend(std::move(backend));

        // Now start the backend
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (backend_) {
                spdlog::debug("[AMS State] Starting backend");
                auto result = backend_->start();
                spdlog::debug("[AMS State] backend->start() returned, result={}",
                              static_cast<bool>(result));
            }
        }

        int slot_count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            slot_count = lv_subject_get_int(&ams_slot_count_);
        }
        spdlog::info("[AMS State] {} backend initialized ({} slots)",
                     ams_type_to_string(detected_type), slot_count);
    } else {
        spdlog::warn("[AMS State] Failed to create {} backend", ams_type_to_string(detected_type));
    }
}

void AmsState::set_backend(std::unique_ptr<AmsBackend> backend) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Stop existing backend
    if (backend_) {
        backend_->stop();
    }

    backend_ = std::move(backend);

    if (backend_) {
        // Register event callback
        backend_->set_event_callback([this](const std::string& event, const std::string& data) {
            on_backend_event(event, data);
        });

        spdlog::debug("[AMS State] Backend set (type={})",
                      ams_type_to_string(backend_->get_type()));
    }
}

AmsBackend* AmsState::get_backend() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return backend_.get();
}

bool AmsState::is_available() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return backend_ && backend_->get_type() != AmsType::NONE;
}

void AmsState::set_moonraker_api(MoonrakerAPI* api) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    api_ = api;
    last_synced_spoolman_id_ = 0; // Reset tracking on API change
    spdlog::debug("[AMS State] Moonraker API {} for Spoolman integration", api ? "set" : "cleared");
}

lv_subject_t* AmsState::get_slot_color_subject(int slot_index) {
    if (slot_index < 0 || slot_index >= MAX_SLOTS) {
        return nullptr;
    }
    return &slot_colors_[slot_index];
}

lv_subject_t* AmsState::get_slot_status_subject(int slot_index) {
    if (slot_index < 0 || slot_index >= MAX_SLOTS) {
        return nullptr;
    }
    return &slot_statuses_[slot_index];
}

void AmsState::sync_from_backend() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!backend_) {
        return;
    }

    AmsSystemInfo info = backend_->get_system_info();

    // Update system-level subjects
    lv_subject_set_int(&ams_type_, static_cast<int>(info.type));
    spdlog::debug("[AmsState] sync_from_backend: action={} ({})", static_cast<int>(info.action),
                  ams_action_to_string(info.action));
    lv_subject_set_int(&ams_action_, static_cast<int>(info.action));

    // Set system name from backend type_name or fallback to type string
    if (!info.type_name.empty()) {
        lv_subject_copy_string(&ams_system_name_, info.type_name.c_str());
    } else {
        lv_subject_copy_string(&ams_system_name_, ams_type_to_string(info.type));
    }
    lv_subject_set_int(&current_slot_, info.current_slot);
    lv_subject_set_int(&ams_current_tool_, info.current_tool);

    // Update formatted tool text (e.g., "T0", "T1", or "---" when no tool active)
    if (info.current_tool >= 0) {
        snprintf(ams_current_tool_text_buf_, sizeof(ams_current_tool_text_buf_), "T%d",
                 info.current_tool);
        lv_subject_copy_string(&ams_current_tool_text_, ams_current_tool_text_buf_);
    } else {
        lv_subject_copy_string(&ams_current_tool_text_, "---");
    }

    lv_subject_set_int(&filament_loaded_, info.filament_loaded ? 1 : 0);
    lv_subject_set_int(&bypass_active_, info.current_slot == -2 ? 1 : 0);
    lv_subject_set_int(&supports_bypass_, info.supports_bypass ? 1 : 0);
    lv_subject_set_int(&ams_slot_count_, info.total_slots);

    // Update action detail string
    if (!info.operation_detail.empty()) {
        lv_subject_copy_string(&ams_action_detail_, info.operation_detail.c_str());
    } else {
        lv_subject_copy_string(&ams_action_detail_, ams_action_to_string(info.action));
    }

    // Update path visualization subjects
    lv_subject_set_int(&path_topology_, static_cast<int>(backend_->get_topology()));
    lv_subject_set_int(&path_active_slot_, info.current_slot);
    lv_subject_set_int(&path_filament_segment_, static_cast<int>(backend_->get_filament_segment()));
    lv_subject_set_int(&path_error_segment_, static_cast<int>(backend_->infer_error_segment()));
    // Note: path_anim_progress_ is controlled by UI animation, not synced from backend

    // Update per-slot subjects
    for (int i = 0; i < std::min(info.total_slots, MAX_SLOTS); ++i) {
        const SlotInfo* slot = info.get_slot_global(i);
        if (slot) {
            lv_subject_set_int(&slot_colors_[i], static_cast<int>(slot->color_rgb));
            lv_subject_set_int(&slot_statuses_[i], static_cast<int>(slot->status));
        }
    }

    // Clear remaining slot subjects
    for (int i = info.total_slots; i < MAX_SLOTS; ++i) {
        lv_subject_set_int(&slot_colors_[i], static_cast<int>(AMS_DEFAULT_SLOT_COLOR));
        lv_subject_set_int(&slot_statuses_[i], static_cast<int>(SlotStatus::UNKNOWN));
    }

    bump_slots_version();

    // Sync dryer state (for systems with integrated drying like ValgACE)
    sync_dryer_from_backend();

    // Sync "Currently Loaded" display subjects
    sync_current_loaded_from_backend();

    spdlog::debug("[AMS State] Synced from backend - type={}, slots={}, action={}, segment={}",
                  ams_type_to_string(info.type), info.total_slots,
                  ams_action_to_string(info.action),
                  path_segment_to_string(backend_->get_filament_segment()));

    // Refresh Spoolman weights now that slot data is available
    // (this catches initial load and any re-syncs)
    refresh_spoolman_weights();
}

void AmsState::update_slot(int slot_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!backend_ || slot_index < 0 || slot_index >= MAX_SLOTS) {
        return;
    }

    SlotInfo slot = backend_->get_slot_info(slot_index);
    if (slot.slot_index >= 0) {
        lv_subject_set_int(&slot_colors_[slot_index], static_cast<int>(slot.color_rgb));
        lv_subject_set_int(&slot_statuses_[slot_index], static_cast<int>(slot.status));
        bump_slots_version();

        spdlog::trace("[AMS State] Updated slot {} - color=0x{:06X}, status={}", slot_index,
                      slot.color_rgb, slot_status_to_string(slot.status));
    }
}

void AmsState::on_backend_event(const std::string& event, const std::string& data) {
    spdlog::trace("[AMS State] Received event '{}' data='{}'", event, data);

    // Use ui_queue_update to post updates to LVGL's main thread
    // This is required because backend events may come from background threads
    // and LVGL is not thread-safe

    // Helper to safely queue async call using RAII pattern
    auto queue_sync = [](bool full_sync, int slot_index) {
        auto sync_data = std::make_unique<AsyncSyncData>(AsyncSyncData{full_sync, slot_index});
        ui_queue_update<AsyncSyncData>(std::move(sync_data), [](AsyncSyncData* d) {
            // Skip if shutdown is in progress - AmsState singleton may be destroyed
            if (s_shutdown_flag.load(std::memory_order_acquire)) {
                return;
            }

            if (d->full_sync) {
                AmsState::instance().sync_from_backend();
            } else {
                AmsState::instance().update_slot(d->slot_index);
            }
        });
    };

    if (event == AmsBackend::EVENT_STATE_CHANGED) {
        queue_sync(true, -1);
    } else if (event == AmsBackend::EVENT_SLOT_CHANGED) {
        // Parse slot index from data
        if (!data.empty()) {
            try {
                int slot_index = std::stoi(data);
                queue_sync(false, slot_index);
            } catch (...) {
                // Invalid data, do full sync
                queue_sync(true, -1);
            }
        }
    } else if (event == AmsBackend::EVENT_LOAD_COMPLETE ||
               event == AmsBackend::EVENT_UNLOAD_COMPLETE ||
               event == AmsBackend::EVENT_TOOL_CHANGED) {
        // These events indicate state change, sync everything
        queue_sync(true, -1);
    } else if (event == AmsBackend::EVENT_ERROR) {
        // Error occurred, sync to get error state
        queue_sync(true, -1);
        spdlog::warn("[AMS State] Backend error - {}", data);
    } else if (event == AmsBackend::EVENT_ATTENTION_REQUIRED) {
        // User intervention needed
        queue_sync(true, -1);
        spdlog::warn("[AMS State] Attention required - {}", data);
    }
}

void AmsState::bump_slots_version() {
    int current = lv_subject_get_int(&slots_version_);
    lv_subject_set_int(&slots_version_, current + 1);
}

void AmsState::sync_dryer_from_backend() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!backend_) {
        // No backend - clear dryer state
        lv_subject_set_int(&dryer_supported_, 0);
        lv_subject_set_int(&dryer_active_, 0);
        return;
    }

    DryerInfo dryer = backend_->get_dryer_info();

    // Update integer subjects
    lv_subject_set_int(&dryer_supported_, dryer.supported ? 1 : 0);
    lv_subject_set_int(&dryer_active_, dryer.active ? 1 : 0);
    lv_subject_set_int(&dryer_current_temp_, static_cast<int>(dryer.current_temp_c));
    lv_subject_set_int(&dryer_target_temp_, static_cast<int>(dryer.target_temp_c));
    lv_subject_set_int(&dryer_remaining_min_, dryer.remaining_min);
    lv_subject_set_int(&dryer_progress_pct_, dryer.get_progress_pct());

    // Format temperature text strings
    if (dryer.supported) {
        snprintf(dryer_current_temp_text_buf_, sizeof(dryer_current_temp_text_buf_), "%d°C",
                 static_cast<int>(dryer.current_temp_c));
        lv_subject_copy_string(&dryer_current_temp_text_, dryer_current_temp_text_buf_);

        if (dryer.target_temp_c > 0) {
            snprintf(dryer_target_temp_text_buf_, sizeof(dryer_target_temp_text_buf_), "%d°C",
                     static_cast<int>(dryer.target_temp_c));
        } else {
            snprintf(dryer_target_temp_text_buf_, sizeof(dryer_target_temp_text_buf_), "Off");
        }
        lv_subject_copy_string(&dryer_target_temp_text_, dryer_target_temp_text_buf_);

        // Format time remaining text
        if (dryer.active && dryer.remaining_min > 0) {
            std::string time_str = helix::fmt::duration_remaining(dryer.remaining_min * 60);
            std::strncpy(dryer_time_text_buf_, time_str.c_str(), sizeof(dryer_time_text_buf_) - 1);
            dryer_time_text_buf_[sizeof(dryer_time_text_buf_) - 1] = '\0';
        } else {
            dryer_time_text_buf_[0] = '\0';
        }
        lv_subject_copy_string(&dryer_time_text_, dryer_time_text_buf_);
    } else {
        lv_subject_copy_string(&dryer_current_temp_text_, "---");
        lv_subject_copy_string(&dryer_target_temp_text_, "---");
        lv_subject_copy_string(&dryer_time_text_, "");
    }

    spdlog::trace("[AMS State] Synced dryer - supported={}, active={}, temp={}→{}°C, {}min left",
                  dryer.supported, dryer.active, static_cast<int>(dryer.current_temp_c),
                  static_cast<int>(dryer.target_temp_c), dryer.remaining_min);
}

void AmsState::set_action_detail(const std::string& detail) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    lv_subject_copy_string(&ams_action_detail_, detail.c_str());
    spdlog::debug("[AMS State] Action detail set: {}", detail);
}

void AmsState::set_action(AmsAction action) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    lv_subject_set_int(&ams_action_, static_cast<int>(action));
    spdlog::debug("[AMS State] Action set: {}", ams_action_to_string(action));
}

void AmsState::sync_current_loaded_from_backend() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!backend_) {
        // No backend - show empty state
        lv_subject_copy_string(&current_material_text_, "---");
        lv_subject_copy_string(&current_slot_text_, "None");
        lv_subject_copy_string(&current_weight_text_, "");
        lv_subject_set_int(&current_has_weight_, 0);
        lv_subject_set_int(&current_color_, 0x505050);
        return;
    }

    int slot_index = lv_subject_get_int(&current_slot_);
    bool filament_loaded = lv_subject_get_int(&filament_loaded_) != 0;

    // Check for bypass mode (slot_index == -2)
    if (slot_index == -2 && backend_->is_bypass_active()) {
        lv_subject_copy_string(&current_material_text_, "External");
        lv_subject_copy_string(&current_slot_text_, "Bypass");
        lv_subject_copy_string(&current_weight_text_, "");
        lv_subject_set_int(&current_has_weight_, 0);
        lv_subject_set_int(&current_color_, 0x888888);
    } else if (slot_index >= 0 && filament_loaded) {
        // Filament is loaded - show slot info
        SlotInfo slot_info = backend_->get_slot_info(slot_index);

        // Sync Spoolman active spool when slot with spoolman_id is loaded
        if (api_ && slot_info.spoolman_id > 0 &&
            slot_info.spoolman_id != last_synced_spoolman_id_) {
            last_synced_spoolman_id_ = slot_info.spoolman_id;
            spdlog::info("[AMS State] Setting active Spoolman spool to {} (slot {})",
                         slot_info.spoolman_id, slot_index);
            api_->set_active_spool(slot_info.spoolman_id, []() {}, [](const MoonrakerError&) {});
        }

        // Set color
        lv_subject_set_int(&current_color_, static_cast<int>(slot_info.color_rgb));

        // Build material label - combine color name with material when Spoolman linked
        if (slot_info.spoolman_id > 0 && !slot_info.color_name.empty()) {
            std::string label = slot_info.color_name;
            if (!slot_info.material.empty()) {
                label += " " + slot_info.material;
            }
            lv_subject_copy_string(&current_material_text_, label.c_str());
        } else if (!slot_info.material.empty()) {
            lv_subject_copy_string(&current_material_text_, slot_info.material.c_str());
        } else {
            lv_subject_copy_string(&current_material_text_, "Filament");
        }

        // Set slot label (1-based for user display)
        snprintf(current_slot_text_buf_, sizeof(current_slot_text_buf_), "Slot %d", slot_index + 1);
        lv_subject_copy_string(&current_slot_text_, current_slot_text_buf_);

        // Show remaining weight if available (Spoolman linked with weight data)
        if (slot_info.spoolman_id > 0 && slot_info.total_weight_g > 0.0f) {
            snprintf(current_weight_text_buf_, sizeof(current_weight_text_buf_), "%.0fg",
                     slot_info.remaining_weight_g);
            lv_subject_copy_string(&current_weight_text_, current_weight_text_buf_);
            lv_subject_set_int(&current_has_weight_, 1);
        } else {
            lv_subject_copy_string(&current_weight_text_, "");
            lv_subject_set_int(&current_has_weight_, 0);
        }
    } else {
        // No filament loaded - show empty state
        lv_subject_copy_string(&current_material_text_, "---");
        lv_subject_copy_string(&current_slot_text_, "None");
        lv_subject_copy_string(&current_weight_text_, "");
        lv_subject_set_int(&current_has_weight_, 0);
        lv_subject_set_int(&current_color_, 0x505050);
    }

    spdlog::trace("[AMS State] Synced current loaded - slot={}, has_weight={}", slot_index,
                  lv_subject_get_int(&current_has_weight_));
}

// ============================================================================
// Dryer Modal Editing Methods
// ============================================================================

void AmsState::adjust_modal_temp(int delta_c) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Get limits from backend if available, fallback to constants
    float min_temp = static_cast<float>(MIN_DRYER_TEMP_C);
    float max_temp = static_cast<float>(MAX_DRYER_TEMP_C);
    if (backend_) {
        DryerInfo dryer = backend_->get_dryer_info();
        min_temp = dryer.min_temp_c;
        max_temp = dryer.max_temp_c;
    }

    int new_temp = modal_target_temp_c_ + delta_c;
    new_temp = std::max(static_cast<int>(min_temp), std::min(new_temp, static_cast<int>(max_temp)));
    modal_target_temp_c_ = new_temp;

    update_modal_text_subjects();
    spdlog::debug("[AMS State] Modal temp adjusted to {}°C", modal_target_temp_c_);
}

void AmsState::adjust_modal_duration(int delta_min) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Get max duration from backend if available, fallback to constant
    int max_duration = MAX_DRYER_DURATION_MIN;
    if (backend_) {
        DryerInfo dryer = backend_->get_dryer_info();
        max_duration = dryer.max_duration_min;
    }

    int new_duration = modal_duration_min_ + delta_min;
    new_duration = std::max(MIN_DRYER_DURATION_MIN, std::min(new_duration, max_duration));
    modal_duration_min_ = new_duration;

    update_modal_text_subjects();
    spdlog::debug("[AMS State] Modal duration adjusted to {} min", modal_duration_min_);
}

void AmsState::set_modal_preset(int temp_c, int duration_min) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    modal_target_temp_c_ = temp_c;
    modal_duration_min_ = duration_min;
    update_modal_text_subjects();
    spdlog::debug("[AMS State] Modal preset set: {}°C for {} min", temp_c, duration_min);
}

void AmsState::update_modal_text_subjects() {
    // Format temperature (e.g., "55°C")
    snprintf(dryer_modal_temp_text_buf_, sizeof(dryer_modal_temp_text_buf_), "%d°C",
             modal_target_temp_c_);
    lv_subject_copy_string(&dryer_modal_temp_text_, dryer_modal_temp_text_buf_);

    // Format duration using utility (e.g., "4h", "30m", "4h 30m")
    std::string duration = helix::fmt::duration(modal_duration_min_ * 60);
    snprintf(dryer_modal_duration_text_buf_, sizeof(dryer_modal_duration_text_buf_), "%s",
             duration.c_str());
    lv_subject_copy_string(&dryer_modal_duration_text_, dryer_modal_duration_text_buf_);
}

// ============================================================================
// Spoolman Weight Polling
// ============================================================================

void AmsState::refresh_spoolman_weights() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!api_) {
        return;
    }

    if (!backend_) {
        return;
    }

    int slot_count = backend_->get_system_info().total_slots;
    int linked_count = 0;

    for (int i = 0; i < slot_count; ++i) {
        SlotInfo slot = backend_->get_slot_info(i);
        if (slot.spoolman_id > 0) {
            ++linked_count;
            int slot_index = i;
            int spoolman_id = slot.spoolman_id;

            api_->get_spoolman_spool(
                spoolman_id,
                [slot_index, spoolman_id](const std::optional<SpoolInfo>& spool_opt) {
                    if (!spool_opt.has_value()) {
                        spdlog::warn("[AmsState] Spoolman spool {} not found", spoolman_id);
                        return;
                    }

                    const SpoolInfo& spool = spool_opt.value();

                    // Data to pass to UI thread
                    struct WeightUpdate {
                        int slot_index;
                        int expected_spoolman_id; // To verify slot wasn't reassigned
                        float remaining_weight_g;
                        float total_weight_g;
                    };

                    auto update_data = std::make_unique<WeightUpdate>(WeightUpdate{
                        slot_index, spoolman_id, static_cast<float>(spool.remaining_weight_g),
                        static_cast<float>(spool.initial_weight_g)});

                    ui_queue_update<WeightUpdate>(std::move(update_data), [](WeightUpdate* d) {
                        // Skip if shutdown is in progress
                        if (s_shutdown_flag.load(std::memory_order_acquire)) {
                            return;
                        }

                        AmsState& state = AmsState::instance();
                        std::lock_guard<std::recursive_mutex> lock(state.mutex_);

                        if (!state.backend_) {
                            return;
                        }

                        // Get current slot info and verify it wasn't reassigned
                        SlotInfo slot = state.backend_->get_slot_info(d->slot_index);
                        if (slot.spoolman_id != d->expected_spoolman_id) {
                            spdlog::debug(
                                "[AmsState] Slot {} spoolman_id changed ({} -> {}), skipping stale "
                                "weight update",
                                d->slot_index, d->expected_spoolman_id, slot.spoolman_id);
                            return;
                        }

                        // Update weights and set back
                        slot.remaining_weight_g = d->remaining_weight_g;
                        slot.total_weight_g = d->total_weight_g;
                        state.backend_->set_slot_info(d->slot_index, slot);
                        state.bump_slots_version();

                        spdlog::trace("[AmsState] Updated slot {} weights: {:.0f}g / {:.0f}g",
                                      d->slot_index, d->remaining_weight_g, d->total_weight_g);
                    });
                },
                [spoolman_id](const MoonrakerError& err) {
                    spdlog::warn("[AmsState] Failed to fetch Spoolman spool {}: {}", spoolman_id,
                                 err.message);
                });
        }
    }

    if (linked_count > 0) {
        spdlog::debug("[AmsState] Refreshing Spoolman weights for {} linked slots", linked_count);
    }
}

void AmsState::start_spoolman_polling() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    ++spoolman_poll_refcount_;
    spdlog::debug("[AmsState] Starting Spoolman polling (refcount: {})", spoolman_poll_refcount_);

    // Only create timer on first reference
    if (spoolman_poll_refcount_ == 1 && !spoolman_poll_timer_) {
        spoolman_poll_timer_ = lv_timer_create(
            [](lv_timer_t* timer) {
                auto* self = static_cast<AmsState*>(lv_timer_get_user_data(timer));
                self->refresh_spoolman_weights();
            },
            SPOOLMAN_POLL_INTERVAL_MS, this);

        // Also do an immediate refresh
        refresh_spoolman_weights();
    }
}

void AmsState::stop_spoolman_polling() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (spoolman_poll_refcount_ > 0) {
        --spoolman_poll_refcount_;
    }

    spdlog::debug("[AmsState] Stopping Spoolman polling (refcount: {})", spoolman_poll_refcount_);

    // Only delete timer when refcount reaches zero
    // Guard against LVGL already being deinitialized during shutdown
    if (spoolman_poll_refcount_ == 0 && spoolman_poll_timer_ && lv_is_initialized()) {
        lv_timer_delete(spoolman_poll_timer_);
        spoolman_poll_timer_ = nullptr;
    }
}
