// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_state.h"

#include "ui_update_queue.h"

#include "ams_backend_afc.h"
#include "ams_backend_toolchanger.h"
#include "moonraker_api.h"
#include "printer_capabilities.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstring>

// Async callback data for thread-safe LVGL updates
namespace {

// Shutdown flag to prevent async callbacks from accessing destroyed singleton
static std::atomic<bool> s_shutdown_flag{false};

struct AsyncSyncData {
    bool full_sync;
    int slot_index; // Only used if full_sync == false
};

void async_sync_callback(void* data) {
    auto* sync_data = static_cast<AsyncSyncData*>(data);

    // Skip if shutdown is in progress - AmsState singleton may be destroyed
    if (s_shutdown_flag.load(std::memory_order_acquire)) {
        delete sync_data;
        return;
    }

    if (sync_data->full_sync) {
        AmsState::instance().sync_from_backend();
    } else {
        AmsState::instance().update_slot(sync_data->slot_index);
    }
    delete sync_data;
}
} // namespace

AmsState& AmsState::instance() {
    static AmsState instance;
    return instance;
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

    spdlog::debug("[AMS State] Initializing subjects");

    // System-level subjects
    lv_subject_init_int(&ams_type_, static_cast<int>(AmsType::NONE));
    lv_subject_init_int(&ams_action_, static_cast<int>(AmsAction::IDLE));
    lv_subject_init_int(&current_slot_, -1);
    lv_subject_init_int(&current_tool_, -1);
    lv_subject_init_int(&filament_loaded_, 0);
    lv_subject_init_int(&bypass_active_, 0);
    lv_subject_init_int(&supports_bypass_, 0);
    lv_subject_init_int(&slot_count_, 0);
    lv_subject_init_int(&slots_version_, 0);

    // String subjects
    lv_subject_init_string(&ams_action_detail_, action_detail_buf_, nullptr,
                           sizeof(action_detail_buf_), "");
    lv_subject_init_string(&ams_system_name_, system_name_buf_, nullptr, sizeof(system_name_buf_),
                           "");
    lv_subject_init_string(&current_tool_text_, current_tool_text_buf_, nullptr,
                           sizeof(current_tool_text_buf_), "---");

    // Filament path visualization subjects
    lv_subject_init_int(&path_topology_, static_cast<int>(PathTopology::HUB));
    lv_subject_init_int(&path_active_slot_, -1);
    lv_subject_init_int(&path_filament_segment_, static_cast<int>(PathSegment::NONE));
    lv_subject_init_int(&path_error_segment_, static_cast<int>(PathSegment::NONE));
    lv_subject_init_int(&path_anim_progress_, 0);

    // Dryer subjects (for AMS systems with integrated drying)
    lv_subject_init_int(&dryer_supported_, 0);
    lv_subject_init_int(&dryer_active_, 0);
    lv_subject_init_int(&dryer_current_temp_, 0);
    lv_subject_init_int(&dryer_target_temp_, 0);
    lv_subject_init_int(&dryer_remaining_min_, 0);
    lv_subject_init_int(&dryer_progress_pct_, -1);
    lv_subject_init_string(&dryer_current_temp_text_, dryer_current_temp_text_buf_, nullptr,
                           sizeof(dryer_current_temp_text_buf_), "---");
    lv_subject_init_string(&dryer_target_temp_text_, dryer_target_temp_text_buf_, nullptr,
                           sizeof(dryer_target_temp_text_buf_), "---");
    lv_subject_init_string(&dryer_time_text_, dryer_time_text_buf_, nullptr,
                           sizeof(dryer_time_text_buf_), "");

    // Dryer modal editing subjects
    lv_subject_init_string(&dryer_modal_temp_text_, dryer_modal_temp_text_buf_, nullptr,
                           sizeof(dryer_modal_temp_text_buf_), "55°C");
    lv_subject_init_string(&dryer_modal_duration_text_, dryer_modal_duration_text_buf_, nullptr,
                           sizeof(dryer_modal_duration_text_buf_), "4h");

    // Currently Loaded display subjects (for reactive UI binding)
    lv_subject_init_string(&current_material_text_, current_material_text_buf_, nullptr,
                           sizeof(current_material_text_buf_), "---");
    lv_subject_init_string(&current_slot_text_, current_slot_text_buf_, nullptr,
                           sizeof(current_slot_text_buf_), "None");
    lv_subject_init_string(&current_weight_text_, current_weight_text_buf_, nullptr,
                           sizeof(current_weight_text_buf_), "");
    lv_subject_init_int(&current_has_weight_, 0);
    lv_subject_init_int(&current_color_, 0x505050); // Default gray

    // Per-slot subjects
    for (int i = 0; i < MAX_SLOTS; ++i) {
        lv_subject_init_int(&slot_colors_[i], static_cast<int>(AMS_DEFAULT_SLOT_COLOR));
        lv_subject_init_int(&slot_statuses_[i], static_cast<int>(SlotStatus::UNKNOWN));
    }

    // Register with XML system if requested
    if (register_xml) {
        lv_xml_register_subject(NULL, "ams_type", &ams_type_);
        lv_xml_register_subject(NULL, "ams_action", &ams_action_);
        lv_xml_register_subject(NULL, "ams_action_detail", &ams_action_detail_);
        lv_xml_register_subject(NULL, "ams_system_name", &ams_system_name_);
        lv_xml_register_subject(NULL, "ams_current_slot", &current_slot_);
        lv_xml_register_subject(NULL, "ams_current_tool", &current_tool_);
        lv_xml_register_subject(NULL, "ams_current_tool_text", &current_tool_text_);
        lv_xml_register_subject(NULL, "ams_filament_loaded", &filament_loaded_);
        lv_xml_register_subject(NULL, "ams_bypass_active", &bypass_active_);
        lv_xml_register_subject(NULL, "ams_supports_bypass", &supports_bypass_);
        lv_xml_register_subject(NULL, "ams_slot_count", &slot_count_);
        lv_xml_register_subject(NULL, "ams_slots_version", &slots_version_);

        // Filament path visualization subjects
        lv_xml_register_subject(NULL, "ams_path_topology", &path_topology_);
        lv_xml_register_subject(NULL, "ams_path_active_slot", &path_active_slot_);
        lv_xml_register_subject(NULL, "ams_path_filament_segment", &path_filament_segment_);
        lv_xml_register_subject(NULL, "ams_path_error_segment", &path_error_segment_);
        lv_xml_register_subject(NULL, "ams_path_anim_progress", &path_anim_progress_);

        // Dryer subjects (for binding in ams_dryer_card.xml)
        lv_xml_register_subject(NULL, "dryer_supported", &dryer_supported_);
        lv_xml_register_subject(NULL, "dryer_active", &dryer_active_);
        lv_xml_register_subject(NULL, "dryer_current_temp", &dryer_current_temp_);
        lv_xml_register_subject(NULL, "dryer_target_temp", &dryer_target_temp_);
        lv_xml_register_subject(NULL, "dryer_remaining_min", &dryer_remaining_min_);
        lv_xml_register_subject(NULL, "dryer_progress_pct", &dryer_progress_pct_);
        lv_xml_register_subject(NULL, "dryer_current_temp_text", &dryer_current_temp_text_);
        lv_xml_register_subject(NULL, "dryer_target_temp_text", &dryer_target_temp_text_);
        lv_xml_register_subject(NULL, "dryer_time_text", &dryer_time_text_);
        lv_xml_register_subject(NULL, "dryer_modal_temp_text", &dryer_modal_temp_text_);
        lv_xml_register_subject(NULL, "dryer_modal_duration_text", &dryer_modal_duration_text_);

        // Currently Loaded display subjects (for binding in ams_panel.xml)
        lv_xml_register_subject(NULL, "ams_current_material_text", &current_material_text_);
        lv_xml_register_subject(NULL, "ams_current_slot_text", &current_slot_text_);
        lv_xml_register_subject(NULL, "ams_current_weight_text", &current_weight_text_);
        lv_xml_register_subject(NULL, "ams_current_has_weight", &current_has_weight_);
        lv_xml_register_subject(NULL, "ams_current_color", &current_color_);

        // Register per-slot subjects with indexed names
        char name_buf[32];
        for (int i = 0; i < MAX_SLOTS; ++i) {
            snprintf(name_buf, sizeof(name_buf), "ams_slot_%d_color", i);
            lv_xml_register_subject(NULL, name_buf, &slot_colors_[i]);

            snprintf(name_buf, sizeof(name_buf), "ams_slot_%d_status", i);
            lv_xml_register_subject(NULL, name_buf, &slot_statuses_[i]);
        }

        spdlog::info(
            "[AMS State] Registered {} system subjects, {} path subjects, {} dryer subjects, "
            "{} current-loaded subjects, {} per-slot subjects",
            10, 5, 10, 5, MAX_SLOTS * 2);
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
            spdlog::info("[AMS State] Backend initialized via factory ({} slots)",
                         lv_subject_get_int(&slot_count_));
        }
    }

    initialized_ = true;
}

void AmsState::reset_for_testing() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    initialized_ = false;
    backend_.reset();
}

void AmsState::init_backend_from_capabilities(const PrinterCapabilities& caps, MoonrakerAPI* api,
                                              MoonrakerClient* client) {
    // Skip if no MMU or tool changer detected
    if (!caps.has_mmu() && !caps.has_tool_changer()) {
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

    AmsType detected_type = caps.get_mmu_type();
    spdlog::info("[AMS State] Detected MMU system: {}", ams_type_to_string(detected_type));

    auto backend = AmsBackend::create(detected_type, api, client);
    if (backend) {
        // For AFC backend, pass discovered lane/hub names from capabilities
        // This works for ALL AFC versions (lane_data database requires v1.0.32+)
        if (detected_type == AmsType::AFC) {
            auto* afc_backend = dynamic_cast<AmsBackendAfc*>(backend.get());
            if (afc_backend) {
                afc_backend->set_discovered_lanes(caps.get_afc_lane_names(),
                                                  caps.get_afc_hub_names());
            }
        }

        // For Tool Changer backend, pass discovered tool names from capabilities
        // Tool names are extracted from "tool T0", "tool T1", etc. in printer.objects.list
        if (detected_type == AmsType::TOOL_CHANGER) {
            auto* tc_backend = dynamic_cast<AmsBackendToolChanger*>(backend.get());
            if (tc_backend) {
                tc_backend->set_discovered_tools(caps.get_tool_names());
            }
        }

        // Set backend (registers event callback)
        set_backend(std::move(backend));

        // Now start the backend - this subscribes to Moonraker updates
        // Event callback is already registered so any events will be processed
        // start() will query initial state asynchronously and emit STATE_CHANGED when data arrives
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (backend_) {
                spdlog::debug("[AMS State] Starting backend");
                auto result = backend_->start();
                spdlog::debug("[AMS State] backend->start() returned, result={}",
                              static_cast<bool>(result));
            }
        }

        // Note: Don't call sync_from_backend() here - with the early hardware discovery
        // callback architecture, the backend receives initial state naturally from the
        // printer.objects.subscribe response and will emit a STATE_CHANGED event.
        // The slot_count below reflects the initialized lanes (from discovery), not loaded filament
        // state.
        int slot_count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            slot_count = lv_subject_get_int(&slot_count_);
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

        spdlog::info("[AMS State] Backend set (type={})", ams_type_to_string(backend_->get_type()));
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
    lv_subject_set_int(&ams_action_, static_cast<int>(info.action));

    // Set system name from backend type_name or fallback to type string
    if (!info.type_name.empty()) {
        lv_subject_copy_string(&ams_system_name_, info.type_name.c_str());
    } else {
        lv_subject_copy_string(&ams_system_name_, ams_type_to_string(info.type));
    }
    lv_subject_set_int(&current_slot_, info.current_slot);
    lv_subject_set_int(&current_tool_, info.current_tool);

    // Update formatted tool text (e.g., "T0", "T1", or "---" when no tool active)
    if (info.current_tool >= 0) {
        snprintf(current_tool_text_buf_, sizeof(current_tool_text_buf_), "T%d", info.current_tool);
        lv_subject_copy_string(&current_tool_text_, current_tool_text_buf_);
    } else {
        lv_subject_copy_string(&current_tool_text_, "---");
    }

    lv_subject_set_int(&filament_loaded_, info.filament_loaded ? 1 : 0);
    lv_subject_set_int(&bypass_active_, info.current_slot == -2 ? 1 : 0);
    lv_subject_set_int(&supports_bypass_, info.supports_bypass ? 1 : 0);
    lv_subject_set_int(&slot_count_, info.total_slots);

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

    // Use lv_async_call to post updates to LVGL's main thread
    // This is required because backend events may come from background threads
    // and LVGL is not thread-safe

    // Helper to safely queue async call with error handling
    auto queue_sync = [](bool full_sync, int slot_index) {
        auto* sync_data = new AsyncSyncData{full_sync, slot_index};
        lv_result_t res = ui_async_call(async_sync_callback, sync_data);
        if (res != LV_RESULT_OK) {
            delete sync_data;
            spdlog::warn("[AMS State] lv_async_call failed, state update dropped");
        }
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
            int hours = dryer.remaining_min / 60;
            int mins = dryer.remaining_min % 60;
            if (hours > 0) {
                snprintf(dryer_time_text_buf_, sizeof(dryer_time_text_buf_), "%d:%02d left", hours,
                         mins);
            } else {
                snprintf(dryer_time_text_buf_, sizeof(dryer_time_text_buf_), "%d min left", mins);
            }
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

    // Format duration (e.g., "4h" or "4h 30m")
    int hours = modal_duration_min_ / 60;
    int mins = modal_duration_min_ % 60;
    if (mins == 0) {
        snprintf(dryer_modal_duration_text_buf_, sizeof(dryer_modal_duration_text_buf_), "%dh",
                 hours);
    } else if (hours == 0) {
        snprintf(dryer_modal_duration_text_buf_, sizeof(dryer_modal_duration_text_buf_), "%dm",
                 mins);
    } else {
        snprintf(dryer_modal_duration_text_buf_, sizeof(dryer_modal_duration_text_buf_), "%dh %dm",
                 hours, mins);
    }
    lv_subject_copy_string(&dryer_modal_duration_text_, dryer_modal_duration_text_buf_);
}
