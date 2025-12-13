// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_state.h"

#include "ams_backend_afc.h"
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
    int gate_index; // Only used if full_sync == false
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
        AmsState::instance().update_gate(sync_data->gate_index);
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
    lv_subject_init_int(&current_gate_, -1);
    lv_subject_init_int(&current_tool_, -1);
    lv_subject_init_int(&filament_loaded_, 0);
    lv_subject_init_int(&bypass_active_, 0);
    lv_subject_init_int(&gate_count_, 0);
    lv_subject_init_int(&gates_version_, 0);

    // String subject for action detail
    lv_subject_init_string(&ams_action_detail_, action_detail_buf_, nullptr,
                           sizeof(action_detail_buf_), "");

    // Filament path visualization subjects
    lv_subject_init_int(&path_topology_, static_cast<int>(PathTopology::HUB));
    lv_subject_init_int(&path_active_gate_, -1);
    lv_subject_init_int(&path_filament_segment_, static_cast<int>(PathSegment::NONE));
    lv_subject_init_int(&path_error_segment_, static_cast<int>(PathSegment::NONE));
    lv_subject_init_int(&path_anim_progress_, 0);

    // Per-gate subjects
    for (int i = 0; i < MAX_GATES; ++i) {
        lv_subject_init_int(&gate_colors_[i], static_cast<int>(AMS_DEFAULT_GATE_COLOR));
        lv_subject_init_int(&gate_statuses_[i], static_cast<int>(GateStatus::UNKNOWN));
    }

    // Register with XML system if requested
    if (register_xml) {
        lv_xml_register_subject(NULL, "ams_type", &ams_type_);
        lv_xml_register_subject(NULL, "ams_action", &ams_action_);
        lv_xml_register_subject(NULL, "ams_action_detail", &ams_action_detail_);
        lv_xml_register_subject(NULL, "ams_current_gate", &current_gate_);
        lv_xml_register_subject(NULL, "ams_current_tool", &current_tool_);
        lv_xml_register_subject(NULL, "ams_filament_loaded", &filament_loaded_);
        lv_xml_register_subject(NULL, "ams_bypass_active", &bypass_active_);
        lv_xml_register_subject(NULL, "ams_gate_count", &gate_count_);
        lv_xml_register_subject(NULL, "ams_gates_version", &gates_version_);

        // Filament path visualization subjects
        lv_xml_register_subject(NULL, "ams_path_topology", &path_topology_);
        lv_xml_register_subject(NULL, "ams_path_active_gate", &path_active_gate_);
        lv_xml_register_subject(NULL, "ams_path_filament_segment", &path_filament_segment_);
        lv_xml_register_subject(NULL, "ams_path_error_segment", &path_error_segment_);
        lv_xml_register_subject(NULL, "ams_path_anim_progress", &path_anim_progress_);

        // Register per-gate subjects with indexed names
        char name_buf[32];
        for (int i = 0; i < MAX_GATES; ++i) {
            snprintf(name_buf, sizeof(name_buf), "ams_gate_%d_color", i);
            lv_xml_register_subject(NULL, name_buf, &gate_colors_[i]);

            snprintf(name_buf, sizeof(name_buf), "ams_gate_%d_status", i);
            lv_xml_register_subject(NULL, name_buf, &gate_statuses_[i]);
        }

        spdlog::info(
            "[AMS State] Registered {} system subjects, {} path subjects, {} per-gate subjects", 9,
            5, MAX_GATES * 2);
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
    // Skip if no MMU detected
    if (!caps.has_mmu()) {
        spdlog::debug("[AMS State] No MMU detected, skipping backend initialization");
        return;
    }

    // Skip if already in mock mode (mock backend was created at startup)
    if (get_runtime_config().should_mock_ams()) {
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
        // The gate_count below reflects the initialized lanes (from discovery), not loaded filament
        // state.
        int gate_count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            gate_count = lv_subject_get_int(&gate_count_);
        }
        spdlog::info("[AMS State] {} backend initialized ({} gates)",
                     ams_type_to_string(detected_type), gate_count);
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

lv_subject_t* AmsState::get_gate_color_subject(int gate_index) {
    if (gate_index < 0 || gate_index >= MAX_GATES) {
        return nullptr;
    }
    return &gate_colors_[gate_index];
}

lv_subject_t* AmsState::get_gate_status_subject(int gate_index) {
    if (gate_index < 0 || gate_index >= MAX_GATES) {
        return nullptr;
    }
    return &gate_statuses_[gate_index];
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
    lv_subject_set_int(&current_gate_, info.current_gate);
    lv_subject_set_int(&current_tool_, info.current_tool);
    lv_subject_set_int(&filament_loaded_, info.filament_loaded ? 1 : 0);
    lv_subject_set_int(&bypass_active_, info.current_gate == -2 ? 1 : 0);
    lv_subject_set_int(&gate_count_, info.total_gates);

    // Update action detail string
    if (!info.operation_detail.empty()) {
        lv_subject_copy_string(&ams_action_detail_, info.operation_detail.c_str());
    } else {
        lv_subject_copy_string(&ams_action_detail_, ams_action_to_string(info.action));
    }

    // Update path visualization subjects
    lv_subject_set_int(&path_topology_, static_cast<int>(backend_->get_topology()));
    lv_subject_set_int(&path_active_gate_, info.current_gate);
    lv_subject_set_int(&path_filament_segment_, static_cast<int>(backend_->get_filament_segment()));
    lv_subject_set_int(&path_error_segment_, static_cast<int>(backend_->infer_error_segment()));
    // Note: path_anim_progress_ is controlled by UI animation, not synced from backend

    // Update per-gate subjects
    for (int i = 0; i < std::min(info.total_gates, MAX_GATES); ++i) {
        const GateInfo* gate = info.get_gate_global(i);
        if (gate) {
            lv_subject_set_int(&gate_colors_[i], static_cast<int>(gate->color_rgb));
            lv_subject_set_int(&gate_statuses_[i], static_cast<int>(gate->status));
        }
    }

    // Clear remaining gate subjects
    for (int i = info.total_gates; i < MAX_GATES; ++i) {
        lv_subject_set_int(&gate_colors_[i], static_cast<int>(AMS_DEFAULT_GATE_COLOR));
        lv_subject_set_int(&gate_statuses_[i], static_cast<int>(GateStatus::UNKNOWN));
    }

    bump_gates_version();

    spdlog::debug("[AMS State] Synced from backend - type={}, gates={}, action={}, segment={}",
                  ams_type_to_string(info.type), info.total_gates,
                  ams_action_to_string(info.action),
                  path_segment_to_string(backend_->get_filament_segment()));
}

void AmsState::update_gate(int gate_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!backend_ || gate_index < 0 || gate_index >= MAX_GATES) {
        return;
    }

    GateInfo gate = backend_->get_gate_info(gate_index);
    if (gate.gate_index >= 0) {
        lv_subject_set_int(&gate_colors_[gate_index], static_cast<int>(gate.color_rgb));
        lv_subject_set_int(&gate_statuses_[gate_index], static_cast<int>(gate.status));
        bump_gates_version();

        spdlog::trace("[AMS State] Updated gate {} - color=0x{:06X}, status={}", gate_index,
                      gate.color_rgb, gate_status_to_string(gate.status));
    }
}

void AmsState::on_backend_event(const std::string& event, const std::string& data) {
    spdlog::trace("[AMS State] Received event '{}' data='{}'", event, data);

    // Use lv_async_call to post updates to LVGL's main thread
    // This is required because backend events may come from background threads
    // and LVGL is not thread-safe

    // Helper to safely queue async call with error handling
    auto queue_sync = [](bool full_sync, int gate_index) {
        auto* sync_data = new AsyncSyncData{full_sync, gate_index};
        lv_result_t res = lv_async_call(async_sync_callback, sync_data);
        if (res != LV_RESULT_OK) {
            delete sync_data;
            spdlog::warn("[AMS State] lv_async_call failed, state update dropped");
        }
    };

    if (event == AmsBackend::EVENT_STATE_CHANGED) {
        queue_sync(true, -1);
    } else if (event == AmsBackend::EVENT_GATE_CHANGED) {
        // Parse gate index from data
        if (!data.empty()) {
            try {
                int gate_index = std::stoi(data);
                queue_sync(false, gate_index);
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

void AmsState::bump_gates_version() {
    int current = lv_subject_get_int(&gates_version_);
    lv_subject_set_int(&gates_version_, current + 1);
}
