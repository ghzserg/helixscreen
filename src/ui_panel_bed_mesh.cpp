// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_bed_mesh.h"

#include "ui_bed_mesh.h"
#include "ui_error_reporting.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_subject_registry.h"
#include "ui_toast.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <limits>

static std::unique_ptr<BedMeshPanel> g_bed_mesh_panel;

// ============================================================================
// Forward declarations for static event callbacks
// ============================================================================
static void on_profile_clicked_cb(lv_event_t* e);
static void on_profile_rename_cb(lv_event_t* e);
static void on_profile_delete_cb(lv_event_t* e);
static void on_calibrate_header_clicked_cb(lv_event_t* e);
static void on_calibrate_cancel_cb(lv_event_t* e);
static void on_calibrate_start_cb(lv_event_t* e);
static void on_rename_cancel_cb(lv_event_t* e);
static void on_rename_confirm_cb(lv_event_t* e);
static void on_delete_cancel_cb(lv_event_t* e);
static void on_delete_confirm_cb(lv_event_t* e);
static void on_save_config_no_cb(lv_event_t* e);
static void on_save_config_yes_cb(lv_event_t* e);

// ============================================================================
// Constructor / Destructor
// ============================================================================

BedMeshPanel::BedMeshPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    // Initialize buffer contents
    std::memset(profile_name_buf_, 0, sizeof(profile_name_buf_));
    std::strncpy(dimensions_buf_, "No mesh data", sizeof(dimensions_buf_) - 1);
    std::memset(max_label_buf_, 0, sizeof(max_label_buf_));
    std::memset(max_value_buf_, 0, sizeof(max_value_buf_));
    std::memset(min_label_buf_, 0, sizeof(min_label_buf_));
    std::memset(min_value_buf_, 0, sizeof(min_value_buf_));
    std::memset(variance_buf_, 0, sizeof(variance_buf_));
    std::memset(rename_old_name_buf_, 0, sizeof(rename_old_name_buf_));

    // Initialize profile buffers
    for (int i = 0; i < BED_MESH_MAX_PROFILES; i++) {
        std::memset(profile_name_bufs_[static_cast<size_t>(i)].data(), 0, 64);
        std::memset(profile_range_bufs_[static_cast<size_t>(i)].data(), 0, 32);
    }
}

BedMeshPanel::~BedMeshPanel() {
    // Signal to async callbacks that this panel is being destroyed [L012]
    // Must happen BEFORE any cleanup that callbacks might reference
    alive_->store(false);

    // CRITICAL: Check if LVGL is still initialized before calling LVGL functions.
    // During static destruction, LVGL may already be torn down.
    if (lv_is_initialized()) {
        // Modal dialogs: use ui_modal_hide() - NOT lv_obj_del()!
        // See docs/QUICK_REFERENCE.md "Modal Dialog Lifecycle"
        if (calibrate_modal_widget_) {
            ui_modal_hide(calibrate_modal_widget_);
            calibrate_modal_widget_ = nullptr;
        }
        if (rename_modal_widget_) {
            ui_modal_hide(rename_modal_widget_);
            rename_modal_widget_ = nullptr;
        }
        if (save_config_modal_widget_) {
            ui_modal_hide(save_config_modal_widget_);
            save_config_modal_widget_ = nullptr;
        }
        if (delete_modal_widget_) {
            ui_modal_hide(delete_modal_widget_);
            delete_modal_widget_ = nullptr;
        }
    }

    // Clear widget pointers (LVGL owns the objects)
    canvas_ = nullptr;
    calibrate_name_input_ = nullptr;
    rename_name_input_ = nullptr;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void BedMeshPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Current mesh stats subjects
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_mesh_available_, 0, "bed_mesh_available");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_profile_name_, profile_name_buf_, "",
                                        "bed_mesh_profile_name");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_dimensions_, dimensions_buf_, "No mesh data",
                                        "bed_mesh_dimensions");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_max_label_, max_label_buf_, "Max",
                                        "bed_mesh_max_label");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_max_value_, max_value_buf_, "--",
                                        "bed_mesh_max_value");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_min_label_, min_label_buf_, "Min",
                                        "bed_mesh_min_label");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_min_value_, min_value_buf_, "--",
                                        "bed_mesh_min_value");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_variance_, variance_buf_, "", "bed_mesh_variance");

    // Profile count
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_mesh_profile_count_, 0, "bed_mesh_profile_count");

    // Profile list subjects (5 profiles)
    for (int i = 0; i < BED_MESH_MAX_PROFILES; i++) {
        std::string name_key = "bed_mesh_profile_" + std::to_string(i) + "_name";
        std::string range_key = "bed_mesh_profile_" + std::to_string(i) + "_range";
        std::string active_key = "bed_mesh_profile_" + std::to_string(i) + "_active";

        auto idx = static_cast<size_t>(i);
        char* name_buf = profile_name_bufs_[idx].data();
        char* range_buf = profile_range_bufs_[idx].data();

        // Initialize with 5-arg form: (subject, buf, prev_buf, size, initial_value)
        lv_subject_init_string(&profile_name_subjects_[idx], name_buf, nullptr,
                               profile_name_bufs_[idx].size(), "");
        lv_xml_register_subject(nullptr, name_key.c_str(), &profile_name_subjects_[idx]);

        lv_subject_init_string(&profile_range_subjects_[idx], range_buf, nullptr,
                               profile_range_bufs_[idx].size(), "");
        lv_xml_register_subject(nullptr, range_key.c_str(), &profile_range_subjects_[idx]);

        lv_subject_init_int(&profile_active_subjects_[idx], 0);
        lv_xml_register_subject(nullptr, active_key.c_str(), &profile_active_subjects_[idx]);
    }

    // Modal state subjects (NOT visibility - internal state only)
    UI_SUBJECT_INIT_AND_REGISTER_INT(bed_mesh_calibrating_, 0, "bed_mesh_calibrating");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_mesh_rename_old_name_, rename_old_name_buf_, "",
                                        "bed_mesh_rename_old_name");
    // Note: All modals now use ui_modal_show() pattern instead of visibility subjects

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized and registered", get_name());
}

// ============================================================================
// Setup
// ============================================================================

void BedMeshPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::info("[{}] Setting up event handlers...", get_name());

    // Register all XML event callbacks
    register_event_callbacks();

    // Use standard overlay panel setup
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    lv_obj_t* overlay_content = lv_obj_find_by_name(panel_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return;
    }

    // Find canvas widget
    canvas_ = lv_obj_find_by_name(overlay_content, "bed_mesh_canvas");
    if (!canvas_) {
        spdlog::error("[{}] Canvas widget 'bed_mesh_canvas' not found in XML", get_name());
        return;
    }
    spdlog::debug("[{}] Found canvas widget - rotation controlled by touch drag", get_name());

    // Setup Moonraker subscription for mesh updates
    setup_moonraker_subscription();

    // Load initial mesh data from MoonrakerAPI
    if (api_ && api_->has_bed_mesh()) {
        const BedMeshProfile* mesh = api_->get_active_bed_mesh();
        if (mesh) {
            spdlog::info("[{}] Active mesh: profile='{}', size={}x{}", get_name(), mesh->name,
                         mesh->x_count, mesh->y_count);
            on_mesh_update_internal(*mesh);
        }
        // Update profile list
        update_profile_list_subjects();
    } else {
        spdlog::info("[{}] No mesh data available from Moonraker", get_name());
    }

    // Apply saved render mode preference from settings
    int saved_mode = SettingsManager::instance().get_bed_mesh_render_mode();
    auto render_mode = static_cast<bed_mesh_render_mode_t>(saved_mode);
    ui_bed_mesh_set_render_mode(canvas_, render_mode);
    spdlog::debug("[{}] Render mode set from settings: {} ({})", get_name(), saved_mode,
                  saved_mode == 0 ? "Auto" : (saved_mode == 1 ? "3D" : "2D"));

    // Evaluate render mode based on FPS history from previous sessions
    // This decides whether to use 3D or 2D fallback mode for AUTO mode
    ui_bed_mesh_evaluate_render_mode(canvas_);

    // Register cleanup handler
    lv_obj_add_event_cb(panel_, on_panel_delete, LV_EVENT_DELETE, this);

    spdlog::info("[{}] Setup complete!", get_name());
}

// ============================================================================
// Event Callback Registration
// ============================================================================

void BedMeshPanel::register_event_callbacks() {
    // Header calibrate button
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_calibrate_clicked",
                             on_calibrate_header_clicked_cb);

    // Profile row callbacks (5 profiles)
    lv_xml_register_event_cb(nullptr, "on_profile_0_clicked", on_profile_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_1_clicked", on_profile_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_2_clicked", on_profile_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_3_clicked", on_profile_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_4_clicked", on_profile_clicked_cb);

    lv_xml_register_event_cb(nullptr, "on_profile_0_rename", on_profile_rename_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_1_rename", on_profile_rename_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_2_rename", on_profile_rename_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_3_rename", on_profile_rename_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_4_rename", on_profile_rename_cb);

    lv_xml_register_event_cb(nullptr, "on_profile_0_delete", on_profile_delete_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_1_delete", on_profile_delete_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_2_delete", on_profile_delete_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_3_delete", on_profile_delete_cb);
    lv_xml_register_event_cb(nullptr, "on_profile_4_delete", on_profile_delete_cb);

    // Calibrate modal
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_calibrate_cancel", on_calibrate_cancel_cb);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_calibrate_start", on_calibrate_start_cb);

    // Rename modal
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_rename_cancel", on_rename_cancel_cb);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_rename_confirm", on_rename_confirm_cb);

    // Delete modal
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_delete_cancel", on_delete_cancel_cb);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_delete_confirm", on_delete_confirm_cb);

    // Save config modal
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_save_config_no", on_save_config_no_cb);
    lv_xml_register_event_cb(nullptr, "on_bed_mesh_save_config_yes", on_save_config_yes_cb);

    spdlog::debug("[{}] XML event callbacks registered", get_name());
}

// ============================================================================
// Profile List Update
// ============================================================================

void BedMeshPanel::update_profile_list_subjects() {
    if (!api_) {
        lv_subject_set_int(&bed_mesh_profile_count_, 0);
        return;
    }

    const auto profiles = api_->get_bed_mesh_profiles();
    const BedMeshProfile* active_mesh = api_->get_active_bed_mesh();
    std::string active_name = active_mesh ? active_mesh->name : "";

    int count = std::min(static_cast<int>(profiles.size()), BED_MESH_MAX_PROFILES);
    lv_subject_set_int(&bed_mesh_profile_count_, count);

    for (int i = 0; i < BED_MESH_MAX_PROFILES; i++) {
        size_t idx = static_cast<size_t>(i);
        if (i < count) {
            profile_names_[idx] = profiles[idx];

            // Set name
            lv_subject_copy_string(&profile_name_subjects_[idx], profiles[idx].c_str());

            // Calculate and set range
            float range = calculate_profile_range(profiles[idx]);
            std::snprintf(profile_range_bufs_[idx].data(), 32, "%.3f", range);
            lv_subject_copy_string(&profile_range_subjects_[idx], profile_range_bufs_[idx].data());

            // Set active state
            int is_active = (profiles[idx] == active_name) ? 1 : 0;
            lv_subject_set_int(&profile_active_subjects_[idx], is_active);
        } else {
            profile_names_[idx].clear();
            lv_subject_copy_string(&profile_name_subjects_[idx], "");
            lv_subject_copy_string(&profile_range_subjects_[idx], "");
            lv_subject_set_int(&profile_active_subjects_[idx], 0);
        }
    }

    spdlog::debug("[{}] Profile list updated: {} profiles, active='{}'", get_name(), count,
                  active_name);
}

float BedMeshPanel::calculate_profile_range(const std::string& profile_name) {
    if (!api_)
        return 0.0f;

    // Get mesh data for this profile
    // Note: MoonrakerAPI stores profiles in bed_mesh.profiles map
    // For now, we'll use the active mesh if it matches, otherwise return 0
    const BedMeshProfile* mesh = api_->get_active_bed_mesh();
    if (!mesh || mesh->name != profile_name || mesh->probed_matrix.empty()) {
        return 0.0f;
    }

    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& row : mesh->probed_matrix) {
        for (float z : row) {
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
        }
    }

    return max_z - min_z;
}

// ============================================================================
// Mesh Data Update
// ============================================================================

void BedMeshPanel::set_mesh_data(const std::vector<std::vector<float>>& mesh_data) {
    if (!canvas_) {
        spdlog::error("[{}] Cannot set mesh data - canvas not initialized", get_name());
        return;
    }

    if (mesh_data.empty() || mesh_data[0].empty()) {
        spdlog::error("[{}] Invalid mesh data - empty rows or columns", get_name());
        return;
    }

    int rows = static_cast<int>(mesh_data.size());
    int cols = static_cast<int>(mesh_data[0].size());

    std::vector<const float*> row_pointers(static_cast<size_t>(rows));
    for (int i = 0; i < rows; i++) {
        row_pointers[static_cast<size_t>(i)] = mesh_data[static_cast<size_t>(i)].data();
    }

    if (!ui_bed_mesh_set_data(canvas_, row_pointers.data(), rows, cols)) {
        spdlog::error("[{}] Failed to set mesh data in widget", get_name());
        return;
    }

    update_info_subjects(mesh_data, cols, rows);
}

void BedMeshPanel::redraw() {
    if (!canvas_) {
        spdlog::warn("[{}] Cannot redraw - canvas not initialized", get_name());
        return;
    }
    ui_bed_mesh_redraw(canvas_);
}

void BedMeshPanel::setup_moonraker_subscription() {
    if (!api_) {
        spdlog::warn("[{}] Cannot subscribe to Moonraker - API is null", get_name());
        return;
    }

    MoonrakerAPI* api = api_;
    auto alive = alive_; // Capture shared_ptr by value for destruction detection [L012]

    api_->get_client().register_notify_update([this, api, alive](nlohmann::json notification) {
        // Check destruction flag FIRST - panel may have been deleted
        if (!alive->load()) {
            return;
        }

        // Check if this notification contains bed_mesh data BEFORE deferring to main thread
        // This avoids unnecessary context switches for unrelated notifications
        if (!notification.contains("params") || !notification["params"].is_array() ||
            notification["params"].empty()) {
            return;
        }
        const nlohmann::json& params = notification["params"][0];
        if (!params.contains("bed_mesh") || !params["bed_mesh"].is_object()) {
            return;
        }

        // CRITICAL: Defer LVGL modifications to main thread via ui_async_call [L012]
        // WebSocket callbacks run on libhv thread - direct lv_subject_* calls cause crashes
        struct Ctx {
            BedMeshPanel* panel;
            MoonrakerAPI* api;
            std::shared_ptr<std::atomic<bool>> alive;
        };
        auto* ctx = new Ctx{this, api, alive};
        ui_async_call(
            [](void* user_data) {
                auto* c = static_cast<Ctx*>(user_data);
                // Check again on main thread - panel could be destroyed between queue and exec
                if (!c->alive->load()) {
                    delete c;
                    return;
                }
                const BedMeshProfile* mesh = c->api->get_active_bed_mesh();
                if (mesh) {
                    c->panel->on_mesh_update_internal(*mesh);
                }
                c->panel->update_profile_list_subjects();
                delete c;
            },
            ctx);
    });
    spdlog::debug("[{}] Registered Moonraker callback for mesh updates", get_name());
}

void BedMeshPanel::on_mesh_update_internal(const BedMeshProfile& mesh) {
    spdlog::debug("[{}] on_mesh_update_internal called, probed_matrix.size={}", get_name(),
                  mesh.probed_matrix.size());

    if (mesh.probed_matrix.empty()) {
        lv_subject_set_int(&bed_mesh_available_, 0);
        lv_subject_copy_string(&bed_mesh_dimensions_, "No mesh data");
        lv_subject_copy_string(&bed_mesh_max_label_, "Max");
        lv_subject_copy_string(&bed_mesh_max_value_, "--");
        lv_subject_copy_string(&bed_mesh_min_label_, "Min");
        lv_subject_copy_string(&bed_mesh_min_value_, "--");
        lv_subject_copy_string(&bed_mesh_variance_, "");
        spdlog::warn("[{}] No mesh data available", get_name());
        return;
    }

    lv_subject_set_int(&bed_mesh_available_, 1);
    lv_subject_copy_string(&bed_mesh_profile_name_, mesh.name.c_str());

    std::snprintf(dimensions_buf_, sizeof(dimensions_buf_), "%dx%d", mesh.x_count, mesh.y_count);
    lv_subject_copy_string(&bed_mesh_dimensions_, dimensions_buf_);

    // Calculate Z range with coordinates
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    int min_row = 0, min_col = 0;
    int max_row = 0, max_col = 0;

    for (size_t row = 0; row < mesh.probed_matrix.size(); row++) {
        for (size_t col = 0; col < mesh.probed_matrix[row].size(); col++) {
            float z = mesh.probed_matrix[row][col];
            if (z < min_z) {
                min_z = z;
                min_row = static_cast<int>(row);
                min_col = static_cast<int>(col);
            }
            if (z > max_z) {
                max_z = z;
                max_row = static_cast<int>(row);
                max_col = static_cast<int>(col);
            }
        }
    }

    // Convert mesh indices to coordinates
    float min_x = (min_col - (mesh.x_count - 1) / 2.0f) * 50.0f;
    float min_y = ((mesh.y_count - 1 - min_row) - (mesh.y_count - 1) / 2.0f) * 50.0f;
    float max_x = (max_col - (mesh.x_count - 1) / 2.0f) * 50.0f;
    float max_y = ((mesh.y_count - 1 - max_row) - (mesh.y_count - 1) / 2.0f) * 50.0f;

    // Update max label and value
    std::snprintf(max_label_buf_, sizeof(max_label_buf_), "Max [%.1f, %.1f]", max_x, max_y);
    lv_subject_copy_string(&bed_mesh_max_label_, max_label_buf_);
    std::snprintf(max_value_buf_, sizeof(max_value_buf_), "%.3f mm", max_z);
    lv_subject_copy_string(&bed_mesh_max_value_, max_value_buf_);

    // Update min label and value
    std::snprintf(min_label_buf_, sizeof(min_label_buf_), "Min [%.1f, %.1f]", min_x, min_y);
    lv_subject_copy_string(&bed_mesh_min_label_, min_label_buf_);
    std::snprintf(min_value_buf_, sizeof(min_value_buf_), "%.3f mm", min_z);
    lv_subject_copy_string(&bed_mesh_min_value_, min_value_buf_);

    // Update variance (range)
    float variance = max_z - min_z;
    std::snprintf(variance_buf_, sizeof(variance_buf_), "%.3f mm", variance);
    lv_subject_copy_string(&bed_mesh_variance_, variance_buf_);

    // Update renderer
    set_mesh_data(mesh.probed_matrix);

    // Set coordinate bounds
    if (canvas_ && (mesh.mesh_max[0] > mesh.mesh_min[0]) && (mesh.mesh_max[1] > mesh.mesh_min[1])) {
        ui_bed_mesh_set_bounds(canvas_, mesh.mesh_min[0], mesh.mesh_max[0], mesh.mesh_min[1],
                               mesh.mesh_max[1], mesh.mesh_min[0], mesh.mesh_max[0],
                               mesh.mesh_min[1], mesh.mesh_max[1]);
    }

    spdlog::info("[{}] Mesh updated: {} ({}x{}, Z: {:.3f} to {:.3f})", get_name(), mesh.name,
                 mesh.x_count, mesh.y_count, min_z, max_z);
}

void BedMeshPanel::update_info_subjects(const std::vector<std::vector<float>>& mesh_data, int cols,
                                        int rows) {
    std::snprintf(dimensions_buf_, sizeof(dimensions_buf_), "%dx%d points", cols, rows);
    lv_subject_copy_string(&bed_mesh_dimensions_, dimensions_buf_);

    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (const auto& row : mesh_data) {
        for (float val : row) {
            min_z = std::min(min_z, val);
            max_z = std::max(max_z, val);
        }
    }

    float variance = max_z - min_z;
    std::snprintf(variance_buf_, sizeof(variance_buf_), "%.3f mm", variance);
    lv_subject_copy_string(&bed_mesh_variance_, variance_buf_);
}

// ============================================================================
// Profile Operations
// ============================================================================

void BedMeshPanel::load_profile(int index) {
    if (index < 0 || index >= BED_MESH_MAX_PROFILES)
        return;
    if (profile_names_[static_cast<size_t>(index)].empty())
        return;

    const std::string& name = profile_names_[static_cast<size_t>(index)];
    spdlog::info("[{}] Loading profile: {}", get_name(), name);

    if (api_) {
        std::string cmd = "BED_MESH_PROFILE LOAD=" + name;
        api_->execute_gcode(
            cmd, [this, name]() { spdlog::debug("[{}] Profile loaded: {}", get_name(), name); },
            [this](const MoonrakerError& err) {
                spdlog::error("[{}] Failed to load profile: {}", get_name(), err.message);
                NOTIFY_ERROR("Failed to load profile");
            });
    }
}

void BedMeshPanel::delete_profile(int index) {
    if (index < 0 || index >= BED_MESH_MAX_PROFILES)
        return;
    if (profile_names_[static_cast<size_t>(index)].empty())
        return;

    const std::string& name = profile_names_[static_cast<size_t>(index)];
    show_delete_confirm_modal(name);
}

void BedMeshPanel::rename_profile(int index) {
    if (index < 0 || index >= BED_MESH_MAX_PROFILES)
        return;
    if (profile_names_[static_cast<size_t>(index)].empty())
        return;

    const std::string& name = profile_names_[static_cast<size_t>(index)];
    show_rename_modal(name);
}

void BedMeshPanel::start_calibration() {
    show_calibrate_modal();
}

// ============================================================================
// Modal Management
// ============================================================================

void BedMeshPanel::show_calibrate_modal() {
    lv_subject_set_int(&bed_mesh_calibrating_, 0);

    calibrate_modal_widget_ = ui_modal_show("bed_mesh_calibrate_modal");
    spdlog::debug("[{}] Showing calibrate modal", get_name());
}

void BedMeshPanel::show_rename_modal(const std::string& profile_name) {
    pending_rename_old_ = profile_name;
    lv_subject_copy_string(&bed_mesh_rename_old_name_, profile_name.c_str());

    rename_modal_widget_ = ui_modal_show("bed_mesh_rename_modal");
    spdlog::debug("[{}] Showing rename modal for: {}", get_name(), profile_name);
}

void BedMeshPanel::show_delete_confirm_modal(const std::string& profile_name) {
    pending_delete_profile_ = profile_name;

    // Create message with profile name
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf), "Delete profile '%s'? This action cannot be undone.",
             profile_name.c_str());

    delete_modal_widget_ =
        ui_modal_show_confirmation("Delete Profile?", msg_buf, ModalSeverity::Warning, "Delete",
                                   on_delete_confirm_cb, on_delete_cancel_cb,
                                   nullptr); // Uses global panel reference

    if (!delete_modal_widget_) {
        spdlog::error("[{}] Failed to create delete confirmation modal", get_name());
        return;
    }

    spdlog::debug("[{}] Showing delete confirm modal for: {}", get_name(), profile_name);
}

void BedMeshPanel::show_save_config_modal() {
    save_config_modal_widget_ = ui_modal_show("bed_mesh_save_config_modal");
    spdlog::debug("[{}] Showing save config modal", get_name());
}

void BedMeshPanel::hide_all_modals() {
    // Reset calibrating state
    lv_subject_set_int(&bed_mesh_calibrating_, 0);

    // Hide all modals (all use ui_modal_hide pattern now)
    if (calibrate_modal_widget_) {
        ui_modal_hide(calibrate_modal_widget_);
        calibrate_modal_widget_ = nullptr;
    }
    if (rename_modal_widget_) {
        ui_modal_hide(rename_modal_widget_);
        rename_modal_widget_ = nullptr;
    }
    if (save_config_modal_widget_) {
        ui_modal_hide(save_config_modal_widget_);
        save_config_modal_widget_ = nullptr;
    }
    if (delete_modal_widget_) {
        ui_modal_hide(delete_modal_widget_);
        delete_modal_widget_ = nullptr;
    }
}

void BedMeshPanel::confirm_delete_profile() {
    std::string name = pending_delete_profile_;
    hide_all_modals();
    execute_delete_profile(name);
}

void BedMeshPanel::decline_save_config() {
    hide_all_modals();
    pending_operation_ = PendingOperation::None;
}

void BedMeshPanel::confirm_save_config() {
    hide_all_modals();
    execute_save_config();
    pending_operation_ = PendingOperation::None;
}

void BedMeshPanel::start_calibration_with_name(const std::string& profile_name) {
    hide_all_modals();
    execute_calibration(profile_name);
}

void BedMeshPanel::confirm_rename(const std::string& new_name) {
    std::string old_name = pending_rename_old_;
    hide_all_modals();
    execute_rename_profile(old_name, new_name);
}

// ============================================================================
// Profile Operation Implementations
// ============================================================================

void BedMeshPanel::execute_delete_profile(const std::string& name) {
    if (!api_)
        return;

    spdlog::info("[{}] Deleting profile: {}", get_name(), name);

    std::string cmd = "BED_MESH_PROFILE REMOVE=" + name;
    api_->execute_gcode(
        cmd,
        [this, name]() {
            spdlog::info("[{}] Profile deleted: {}", get_name(), name);
            NOTIFY_SUCCESS("Profile deleted");
            pending_operation_ = PendingOperation::Delete;
            show_save_config_modal();
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to delete profile: {}", get_name(), err.message);
            NOTIFY_ERROR("Failed to delete profile");
        });
}

void BedMeshPanel::execute_rename_profile(const std::string& old_name,
                                          const std::string& new_name) {
    if (!api_)
        return;

    spdlog::info("[{}] Renaming profile: {} -> {}", get_name(), old_name, new_name);

    // Step 1: Load the profile
    std::string load_cmd = "BED_MESH_PROFILE LOAD=" + old_name;
    api_->execute_gcode(
        load_cmd,
        [this, old_name, new_name]() {
            // Step 2: Save with new name
            std::string save_cmd = "BED_MESH_PROFILE SAVE=" + new_name;
            api_->execute_gcode(
                save_cmd,
                [this, old_name, new_name]() {
                    // Step 3: Remove old name
                    std::string remove_cmd = "BED_MESH_PROFILE REMOVE=" + old_name;
                    api_->execute_gcode(
                        remove_cmd,
                        [this, old_name, new_name]() {
                            spdlog::info("[{}] Profile renamed: {} -> {}", get_name(), old_name,
                                         new_name);
                            NOTIFY_SUCCESS("Profile renamed");
                            pending_operation_ = PendingOperation::Rename;
                            show_save_config_modal();
                        },
                        [this](const MoonrakerError& err) {
                            spdlog::error("[{}] Failed to remove old profile: {}", get_name(),
                                          err.message);
                            NOTIFY_ERROR("Rename failed at remove step");
                        });
                },
                [this](const MoonrakerError& err) {
                    spdlog::error("[{}] Failed to save new profile: {}", get_name(), err.message);
                    NOTIFY_ERROR("Rename failed at save step");
                });
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to load profile for rename: {}", get_name(), err.message);
            NOTIFY_ERROR("Rename failed at load step");
        });
}

void BedMeshPanel::execute_calibration(const std::string& profile_name) {
    if (!api_)
        return;

    spdlog::info("[{}] Starting calibration for profile: {}", get_name(), profile_name);
    lv_subject_set_int(&bed_mesh_calibrating_, 1);

    std::string cmd = "BED_MESH_CALIBRATE PROFILE=" + profile_name;
    api_->execute_gcode(
        cmd,
        [this, profile_name]() {
            spdlog::info("[{}] Calibration started for: {}", get_name(), profile_name);
            NOTIFY_INFO("Calibration started");
            // Modal will close when mesh update notification arrives
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to start calibration: {}", get_name(), err.message);
            NOTIFY_ERROR("Failed to start calibration");
            lv_subject_set_int(&bed_mesh_calibrating_, 0);
        });
}

void BedMeshPanel::execute_save_config() {
    if (!api_)
        return;

    spdlog::info("[{}] Saving config (will restart Klipper)", get_name());

    api_->execute_gcode(
        "SAVE_CONFIG",
        [this]() {
            spdlog::info("[{}] SAVE_CONFIG sent - Klipper will restart", get_name());
            NOTIFY_INFO("Configuration saved - restarting");
        },
        [this](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to save config: {}", get_name(), err.message);
            NOTIFY_ERROR("Failed to save configuration");
        });
}

// ============================================================================
// Static Event Callbacks
// ============================================================================

void BedMeshPanel::on_panel_delete(lv_event_t* e) {
    auto* self = static_cast<BedMeshPanel*>(lv_event_get_user_data(e));
    if (!self)
        return;

    spdlog::debug("[{}] Panel delete event - cleaning up resources", self->get_name());
    self->canvas_ = nullptr;
    self->calibrate_name_input_ = nullptr;
    self->rename_name_input_ = nullptr;
}

// Helper to extract profile index from callback name
static int get_profile_index_from_event(lv_event_t* e) {
    // The callback name contains the index (e.g., "on_profile_2_clicked")
    // We use user_data to pass the panel, not the index
    // Instead, look at the target object's name
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!target)
        return -1;

    // Find parent row and extract index from its name
    lv_obj_t* parent = lv_obj_get_parent(target);
    while (parent) {
        const char* name = lv_obj_get_name(parent);
        if (name && std::strncmp(name, "profile_row_", 12) == 0) {
            return name[12] - '0'; // Extract digit
        }
        parent = lv_obj_get_parent(parent);
    }
    return -1;
}

static void on_profile_clicked_cb(lv_event_t* e) {
    int index = get_profile_index_from_event(e);
    if (index >= 0) {
        get_global_bed_mesh_panel().load_profile(index);
    }
}

static void on_profile_rename_cb(lv_event_t* e) {
    int index = get_profile_index_from_event(e);
    if (index >= 0) {
        get_global_bed_mesh_panel().rename_profile(index);
    }
}

static void on_profile_delete_cb(lv_event_t* e) {
    int index = get_profile_index_from_event(e);
    if (index >= 0) {
        get_global_bed_mesh_panel().delete_profile(index);
    }
}

static void on_calibrate_header_clicked_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().start_calibration();
}

static void on_calibrate_cancel_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().hide_all_modals();
}

static void on_calibrate_start_cb(lv_event_t* /*e*/) {
    // Find the textarea
    lv_obj_t* input = lv_obj_find_by_name(lv_layer_top(), "calibrate_profile_name_input");
    if (!input) {
        // Try from parent screen
        input = lv_obj_find_by_name(lv_screen_active(), "calibrate_profile_name_input");
    }

    std::string profile_name = "default";
    if (input) {
        const char* text = lv_textarea_get_text(input);
        if (text && std::strlen(text) > 0) {
            profile_name = text;
        }
    }

    get_global_bed_mesh_panel().start_calibration_with_name(profile_name);
}

static void on_rename_cancel_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().hide_all_modals();
}

static void on_rename_confirm_cb(lv_event_t* /*e*/) {
    // Get the new name from the input field
    lv_obj_t* input = lv_obj_find_by_name(lv_layer_top(), "rename_new_name_input");
    if (!input) {
        input = lv_obj_find_by_name(lv_screen_active(), "rename_new_name_input");
    }

    if (input) {
        const char* text = lv_textarea_get_text(input);
        if (text && std::strlen(text) > 0) {
            get_global_bed_mesh_panel().confirm_rename(std::string(text));
        }
    }
}

static void on_delete_cancel_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().hide_all_modals();
}

static void on_delete_confirm_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().confirm_delete_profile();
}

static void on_save_config_no_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().decline_save_config();
}

static void on_save_config_yes_cb(lv_event_t* /*e*/) {
    get_global_bed_mesh_panel().confirm_save_config();
}

// ============================================================================
// Global Instance
// ============================================================================

BedMeshPanel& get_global_bed_mesh_panel() {
    if (!g_bed_mesh_panel) {
        g_bed_mesh_panel = std::make_unique<BedMeshPanel>(get_printer_state(), get_moonraker_api());
    }
    return *g_bed_mesh_panel;
}
