// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_ams.h"

#include "ui_ams_slot.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_filament_path_canvas.h"
#include "ui_fonts.h"
#include "ui_hsv_picker.h"
#include "ui_icon.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_spool_canvas.h"
#include "ui_theme.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "color_utils.h"
#include "config.h"
#include "filament_database.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_map>

// Global instance pointer for XML callback access (atomic for safety during destruction)
static std::atomic<AmsPanel*> g_ams_panel_instance{nullptr};

/**
 * @brief Map AMS system/type name to logo image path
 *
 * Maps both generic firmware names (Happy Hare, AFC) and specific hardware
 * names (ERCF, Box Turtle, etc.) to their logo assets.
 *
 * @param name System or type name (case-insensitive matching)
 * @return Logo path or empty string if no matching logo
 */
static const char* get_ams_logo_path(const std::string& name) {
    // Normalize to lowercase for matching
    std::string lower_name = name;
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

/**
 * @brief Check if configured printer is a Voron
 *
 * Reads the printer type from helixconfig.json and checks if it contains "Voron".
 * Used to select Stealthburner toolhead rendering in the filament path canvas.
 *
 * @return true if printer type contains "Voron" (case-insensitive)
 */
static bool is_voron_printer() {
    Config* config = Config::get_instance();
    if (!config) {
        return false;
    }

    std::string printer_type = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
    if (printer_type.empty()) {
        return false;
    }

    // Case-insensitive search for "voron"
    std::string lower_type = printer_type;
    for (auto& c : lower_type) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return lower_type.find("voron") != std::string::npos;
}

// Lazy registration flag - widgets and XML registered on first use
static bool s_ams_widgets_registered = false;

// Forward declarations for XML event callbacks (defined later in file)
static void on_unload_clicked_xml(lv_event_t* e);
static void on_reset_clicked_xml(lv_event_t* e);
static void on_bypass_clicked_xml(lv_event_t* e);
static void on_bypass_toggled_xml(lv_event_t* e);
// Dryer card callbacks now handled by helix::ui::AmsDryerCard class
// Context menu and spoolman picker callbacks are now in extracted classes

// Edit modal and color picker callbacks now handled by helix::ui::AmsEditModal class

/**
 * @brief Register AMS widgets and XML component (lazy, called once on first use)
 *
 * Registers:
 * - spool_canvas: 3D filament spool visualization widget
 * - ams_slot: Individual slot widget with spool and status
 * - filament_path_canvas: Filament routing visualization
 * - ams_panel.xml: Main panel component
 * - ams_context_menu.xml: Slot context menu component
 */
static void ensure_ams_widgets_registered() {
    if (s_ams_widgets_registered) {
        return;
    }

    spdlog::info("[AMS Panel] Lazy-registering AMS widgets and XML components");

    // Register custom widgets (order matters - dependencies first)
    ui_spool_canvas_register();
    ui_ams_slot_register();
    ui_filament_path_canvas_register();

    // Register XML event callbacks BEFORE registering XML components
    // (callbacks must exist when XML parser encounters <event_cb> elements)
    lv_xml_register_event_cb(nullptr, "ams_unload_clicked_cb", on_unload_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_reset_clicked_cb", on_reset_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_bypass_clicked_cb", on_bypass_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_bypass_toggled_cb", on_bypass_toggled_xml);

    // Dryer card callbacks registered by helix::ui::AmsDryerCard class
    // Context menu callbacks registered by helix::ui::AmsContextMenu class
    // Spoolman picker callbacks registered by helix::ui::AmsSpoolmanPicker class
    // Edit modal and color picker callbacks registered by helix::ui::AmsEditModal class

    // Register XML components (dryer card must be registered before ams_panel since it's used
    // there)
    lv_xml_register_component_from_file("A:ui_xml/ams_dryer_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/dryer_presets_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_context_menu.xml");
    lv_xml_register_component_from_file("A:ui_xml/spoolman_spool_item.xml");
    lv_xml_register_component_from_file("A:ui_xml/spoolman_picker_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_edit_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/color_picker.xml");

    s_ams_widgets_registered = true;
    spdlog::debug("[AMS Panel] Widget and XML registration complete");
}

// ============================================================================
// XML Event Callback Wrappers (for <event_cb> elements in XML)
// ============================================================================

static void on_unload_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_unload();
    }
}

static void on_reset_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_reset();
    }
}

static void on_bypass_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_bypass_toggle();
    }
}

static void on_bypass_toggled_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_bypass_toggle();
    }
}

// Dryer card callbacks now handled by helix::ui::AmsDryerCard class
// Context menu and spoolman picker callbacks now handled by extracted classes:
// - helix::ui::AmsContextMenu (ui_ams_context_menu.cpp)
// - helix::ui::AmsSpoolmanPicker (ui_ams_spoolman_picker.cpp)
// Edit modal callbacks handled by helix::ui::AmsEditModal class

// ============================================================================
// Construction
// ============================================================================

AmsPanel::AmsPanel(PrinterState& printer_state, MoonrakerAPI* api) : PanelBase(printer_state, api) {
    spdlog::debug("[AmsPanel] Constructed");
}

// ============================================================================
// PanelBase Interface
// ============================================================================

void AmsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // AmsState handles all subject registration centrally
    // We just ensure it's initialized before panel creation
    AmsState::instance().init_subjects(true);

    // NOTE: Backend creation is handled by:
    // - main.cpp (mock mode at startup)
    // - AmsState::init_backend_from_capabilities() (real printer connection)
    // Panel should NOT create backends - it just observes the existing one.

    // Register observers for state changes
    slots_version_observer_ = ObserverGuard(AmsState::instance().get_slots_version_subject(),
                                            on_slots_version_changed, this);

    action_observer_ =
        ObserverGuard(AmsState::instance().get_ams_action_subject(), on_action_changed, this);

    current_slot_observer_ = ObserverGuard(AmsState::instance().get_current_slot_subject(),
                                           on_current_slot_changed, this);

    // Slot count observer for dynamic slot creation
    slot_count_observer_ =
        ObserverGuard(AmsState::instance().get_slot_count_subject(), on_slot_count_changed, this);

    // Path state observers for filament path visualization
    path_segment_observer_ = ObserverGuard(AmsState::instance().get_path_filament_segment_subject(),
                                           on_path_state_changed, this);
    path_topology_observer_ = ObserverGuard(AmsState::instance().get_path_topology_subject(),
                                            on_path_state_changed, this);

    // UI module subjects are now encapsulated in their respective classes:
    // - helix::ui::AmsEditModal
    // - helix::ui::AmsSpoolmanPicker
    // - helix::ui::AmsColorPicker

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized via AmsState + observers registered", get_name());
}

void AmsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up...", get_name());

    // Use standard overlay panel setup (header bar, responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup UI components
    setup_system_header();
    setup_slots();
    setup_action_buttons();
    setup_status_display();
    setup_path_canvas();

    // Setup dryer card (extracted module)
    if (!dryer_card_) {
        dryer_card_ = std::make_unique<helix::ui::AmsDryerCard>();
    }
    dryer_card_->setup(panel_);

    // Initial UI sync from backend state
    refresh_slots();

    spdlog::debug("[{}] Setup complete!", get_name());
}

void AmsPanel::on_activate() {
    spdlog::debug("[{}] Activated - syncing from backend", get_name());

    // Sync state when panel becomes visible
    AmsState::instance().sync_from_backend();
    refresh_slots();

    // Sync Spoolman active spool with currently loaded slot
    sync_spoolman_active_spool();
}

void AmsPanel::sync_spoolman_active_spool() {
    if (!api_) {
        return;
    }

    int current_slot = lv_subject_get_int(AmsState::instance().get_current_slot_subject());
    if (current_slot < 0) {
        return; // No active slot
    }

    auto* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    SlotInfo slot_info = backend->get_slot_info(current_slot);
    if (slot_info.spoolman_id > 0) {
        spdlog::debug("[{}] Syncing Spoolman: slot {} → spool ID {}", get_name(), current_slot,
                      slot_info.spoolman_id);
        api_->set_active_spool(
            slot_info.spoolman_id, []() {},
            [](const MoonrakerError& err) {
                spdlog::warn("[AmsPanel] Failed to sync active spool: {}", err.message);
            });
    }
}

void AmsPanel::on_deactivate() {
    spdlog::debug("[{}] Deactivated", get_name());
    // Note: UI destruction is handled by NavigationManager close callback
    // registered in get_global_ams_panel()
}

void AmsPanel::clear_panel_reference() {
    // Reset extracted UI modules (they handle their own RAII cleanup)
    dryer_card_.reset();
    spoolman_picker_.reset();
    context_menu_.reset();
    edit_modal_.reset();

    // Clear observer guards BEFORE clearing widget pointers (they reference widgets)
    slots_version_observer_.reset();
    action_observer_.reset();
    current_slot_observer_.reset();
    slot_count_observer_.reset();
    path_segment_observer_.reset();
    path_topology_observer_.reset();

    // Now clear all widget references
    panel_ = nullptr;
    parent_screen_ = nullptr;
    slot_grid_ = nullptr;
    labels_layer_ = nullptr;
    path_canvas_ = nullptr;
    current_slot_count_ = 0;

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        slot_widgets_[i] = nullptr;
        label_widgets_[i] = nullptr;
    }

    // Reset subjects_initialized_ so observers are recreated on next access
    subjects_initialized_ = false;

    // Clear global instance pointer to prevent callbacks from using stale pointer
    g_ams_panel_instance.store(nullptr);

    spdlog::debug("[AMS Panel] Cleared all widget references");
}

// ============================================================================
// Setup Helpers
// ============================================================================

void AmsPanel::setup_system_header() {
    // Find the system logo image in the header
    lv_obj_t* system_logo = lv_obj_find_by_name(panel_, "system_logo");
    if (!system_logo) {
        spdlog::warn("[{}] system_logo not found in XML", get_name());
        return;
    }

    // Get AMS system info from backend
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        spdlog::debug("[{}] No backend, hiding logo", get_name());
        lv_obj_add_flag(system_logo, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Get system name for logo lookup
    const auto& info = backend->get_system_info();
    const char* logo_path = get_ams_logo_path(info.type_name);

    if (logo_path) {
        spdlog::info("[{}] Setting logo: '{}' -> {}", get_name(), info.type_name, logo_path);
        lv_image_set_src(system_logo, logo_path);
        lv_obj_remove_flag(system_logo, LV_OBJ_FLAG_HIDDEN);
        // Log image dimensions after setting source
        lv_coord_t w = lv_obj_get_width(system_logo);
        lv_coord_t h = lv_obj_get_height(system_logo);
        spdlog::info("[{}] Logo widget size: {}x{}, hidden={}", get_name(), w, h,
                     lv_obj_has_flag(system_logo, LV_OBJ_FLAG_HIDDEN));
    } else {
        // Hide logo for unknown systems
        lv_obj_add_flag(system_logo, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[{}] No logo for system '{}'", get_name(), info.type_name);
    }
}

void AmsPanel::setup_slots() {
    slot_grid_ = lv_obj_find_by_name(panel_, "slot_grid");
    if (!slot_grid_) {
        spdlog::warn("[{}] slot_grid not found in XML", get_name());
        return;
    }

    // Find labels layer for z-order (labels render on top of all slots)
    labels_layer_ = lv_obj_find_by_name(panel_, "labels_layer");
    if (!labels_layer_) {
        spdlog::warn(
            "[{}] labels_layer not found in XML - labels may be obscured by overlapping slots",
            get_name());
    }

    // Get initial slot count and create slots
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    spdlog::debug("[{}] setup_slots: slot_count={} from subject", get_name(), slot_count);
    create_slots(slot_count);
}

void AmsPanel::create_slots(int count) {
    if (!slot_grid_) {
        return;
    }

    // Clamp to reasonable range
    if (count < 0) {
        count = 0;
    }
    if (count > MAX_VISIBLE_SLOTS) {
        spdlog::warn("[{}] Clamping slot_count {} to max {}", get_name(), count, MAX_VISIBLE_SLOTS);
        count = MAX_VISIBLE_SLOTS;
    }

    // Skip if unchanged
    if (count == current_slot_count_) {
        return;
    }

    spdlog::debug("[{}] Creating {} slots (was {})", get_name(), count, current_slot_count_);

    // Delete existing slots
    for (int i = 0; i < current_slot_count_; ++i) {
        if (slot_widgets_[i]) {
            lv_obj_delete(slot_widgets_[i]);
            slot_widgets_[i] = nullptr;
        }
        // Note: label_widgets_ are no longer used - labels are inside slot widgets
        label_widgets_[i] = nullptr;
    }

    // Create new slots via XML system (widget handles its own sizing/appearance)
    for (int i = 0; i < count; ++i) {
        lv_obj_t* slot = static_cast<lv_obj_t*>(lv_xml_create(slot_grid_, "ams_slot", nullptr));
        if (!slot) {
            spdlog::error("[{}] Failed to create ams_slot for index {}", get_name(), i);
            continue;
        }

        // Configure slot index (triggers reactive binding setup)
        ui_ams_slot_set_index(slot, i);

        // Set layout info for staggered label positioning
        // Each slot positions its own label based on its index and total count
        ui_ams_slot_set_layout_info(slot, i, count);

        // Store reference and setup click handler
        slot_widgets_[i] = slot;
        lv_obj_set_user_data(slot, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(slot, on_slot_clicked, LV_EVENT_CLICKED, this);
    }

    current_slot_count_ = count;

    // Get available width from slot_area (parent of slot_grid)
    lv_obj_t* slot_area = lv_obj_get_parent(slot_grid_);
    lv_obj_update_layout(slot_area); // Ensure layout is current
    int32_t available_width = lv_obj_get_content_width(slot_area);

    // Calculate dynamic slot width and overlap to fill available space
    // Formula: available_width = N * slot_width - (N-1) * overlap
    // With overlap = 50% of slot_width for 5+ gates:
    //   slot_width = available_width / (N * 0.5 + 0.5) for N >= 5
    //   slot_width = available_width / N for N <= 4 (no overlap)
    int32_t slot_width = 0;
    int32_t overlap = 0;

    if (count > 4) {
        // Use 50% overlap ratio for many gates
        // slot_width = available_width / (N * (1 - overlap_ratio) + overlap_ratio)
        // With overlap_ratio = 0.5: slot_width = available_width / (0.5*N + 0.5)
        float overlap_ratio = 0.5f;
        slot_width = static_cast<int32_t>(available_width /
                                          (count * (1.0f - overlap_ratio) + overlap_ratio));
        overlap = static_cast<int32_t>(slot_width * overlap_ratio);

        // Apply negative column padding for overlap effect
        lv_obj_set_style_pad_column(slot_grid_, -overlap, LV_PART_MAIN);
        spdlog::debug(
            "[{}] Dynamic sizing: available={}px, slot_width={}px, overlap={}px for {} gates",
            get_name(), available_width, slot_width, overlap, count);
    } else {
        // No overlap for 4 or fewer gates - evenly distributed
        slot_width = available_width / count;
        lv_obj_set_style_pad_column(slot_grid_, 0, LV_PART_MAIN);
        spdlog::debug("[{}] Even distribution: slot_width={}px for {} gates", get_name(),
                      slot_width, count);
    }

    // Apply calculated width to each slot
    for (int i = 0; i < count; ++i) {
        if (slot_widgets_[i]) {
            lv_obj_set_width(slot_widgets_[i], slot_width);
        }
    }

    // Move labels to overlay layer so they render on top of overlapping slots
    // This must happen after layout_info is set and widths are applied
    if (labels_layer_ && count > 4) {
        // First clear any previous labels from the layer
        lv_obj_clean(labels_layer_);

        // Calculate slot spacing (same formula as path canvas)
        int32_t slot_spacing = slot_width - overlap;

        for (int i = 0; i < count; ++i) {
            if (slot_widgets_[i]) {
                // Slot center X in labels_layer coords (no card_padding offset - we're inside the
                // card)
                int32_t slot_center_x = slot_width / 2 + i * slot_spacing;
                ui_ams_slot_move_label_to_layer(slot_widgets_[i], labels_layer_, slot_center_x);
            }
        }
        spdlog::debug("[{}] Moved {} labels to overlay layer", get_name(), count);
    }

    // Update path canvas with slot_width and overlap so lane positions match
    if (path_canvas_) {
        ui_filament_path_canvas_set_slot_overlap(path_canvas_, overlap);
        ui_filament_path_canvas_set_slot_width(path_canvas_, slot_width);
    }

    spdlog::info("[{}] Created {} slot widgets with dynamic width={}px", get_name(), count,
                 slot_width);

    // Update the visual tray to 1/3 of slot height
    update_tray_size();
}

void AmsPanel::update_tray_size() {
    if (!panel_ || !slot_grid_) {
        return;
    }

    // Find the tray element
    lv_obj_t* tray = lv_obj_find_by_name(panel_, "slot_tray");
    if (!tray) {
        spdlog::debug("[{}] slot_tray not found - skipping tray sizing", get_name());
        return;
    }

    // Force layout update so slot_grid has its final size
    lv_obj_update_layout(slot_grid_);

    // Get slot grid height (includes material label + spool + padding)
    int32_t grid_height = lv_obj_get_height(slot_grid_);
    if (grid_height <= 0) {
        spdlog::debug("[{}] slot_grid height {} - skipping tray sizing", get_name(), grid_height);
        return;
    }

    // Tray is 1/3 of the slot area height
    int32_t tray_height = grid_height / 3;

    // Set tray size and ensure it stays at bottom
    lv_obj_set_height(tray, tray_height);
    lv_obj_align(tray, LV_ALIGN_BOTTOM_MID, 0, 0);

    spdlog::debug("[{}] Tray sized to {}px (1/3 of {}px grid)", get_name(), tray_height,
                  grid_height);
}

void AmsPanel::on_slot_count_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->panel_) {
        return;
    }

    int new_count = lv_subject_get_int(subject);
    spdlog::debug("[AmsPanel] Slot count changed to {}", new_count);
    self->create_slots(new_count);
}

void AmsPanel::setup_action_buttons() {
    // Store panel pointer for static callbacks to access
    // (Callbacks are registered earlier in ensure_ams_widgets_registered())
    g_ams_panel_instance.store(this);

    spdlog::debug("[{}] Action buttons ready (callbacks registered during widget init)",
                  get_name());
}

void AmsPanel::setup_status_display() {
    // Status display is handled reactively via bind_text in XML
    // Just verify the elements exist
    lv_obj_t* status_label = lv_obj_find_by_name(panel_, "status_label");
    if (status_label) {
        spdlog::debug("[{}] Status label found - bound to ams_action_detail", get_name());
    }
}

void AmsPanel::setup_path_canvas() {
    path_canvas_ = lv_obj_find_by_name(panel_, "path_canvas");
    if (!path_canvas_) {
        spdlog::warn("[{}] path_canvas not found in XML", get_name());
        return;
    }

    // Use Stealthburner toolhead for Voron printers
    if (is_voron_printer()) {
        ui_filament_path_canvas_set_faceted_toolhead(path_canvas_, true);
        spdlog::info("[{}] Using Stealthburner toolhead for Voron printer", get_name());
    }

    // Set slot click callback to trigger filament load
    ui_filament_path_canvas_set_slot_callback(path_canvas_, on_path_slot_clicked, this);

    // Set slot_width and overlap to match current slot configuration
    // This syncs with the dynamic sizing calculated in create_slots()
    if (slot_grid_) {
        lv_obj_t* slot_area = lv_obj_get_parent(slot_grid_);
        lv_obj_update_layout(slot_area);
        int32_t available_width = lv_obj_get_content_width(slot_area);
        int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());

        int32_t slot_width = 0;
        int32_t overlap = 0;

        if (slot_count > 4) {
            float overlap_ratio = 0.5f;
            slot_width = static_cast<int32_t>(
                available_width / (slot_count * (1.0f - overlap_ratio) + overlap_ratio));
            overlap = static_cast<int32_t>(slot_width * overlap_ratio);
        } else if (slot_count > 0) {
            slot_width = available_width / slot_count;
        }

        ui_filament_path_canvas_set_slot_width(path_canvas_, slot_width);
        ui_filament_path_canvas_set_slot_overlap(path_canvas_, overlap);
    }

    // Initial configuration from backend
    update_path_canvas_from_backend();

    spdlog::debug("[{}] Path canvas setup complete", get_name());
}

void AmsPanel::update_path_canvas_from_backend() {
    if (!path_canvas_) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    // Get system info for slot count and topology
    AmsSystemInfo info = backend->get_system_info();

    // Set slot count from backend
    ui_filament_path_canvas_set_slot_count(path_canvas_, info.total_slots);

    // Set topology from backend
    PathTopology topology = backend->get_topology();
    ui_filament_path_canvas_set_topology(path_canvas_, static_cast<int>(topology));

    // Set active slot
    ui_filament_path_canvas_set_active_slot(path_canvas_, info.current_slot);

    // Set filament segment position
    PathSegment segment = backend->get_filament_segment();
    ui_filament_path_canvas_set_filament_segment(path_canvas_, static_cast<int>(segment));

    // Set error segment if any
    PathSegment error_seg = backend->infer_error_segment();
    ui_filament_path_canvas_set_error_segment(path_canvas_, static_cast<int>(error_seg));

    // Set filament color from current slot's filament
    if (info.current_slot >= 0) {
        SlotInfo slot_info = backend->get_slot_info(info.current_slot);
        ui_filament_path_canvas_set_filament_color(path_canvas_, slot_info.color_rgb);
    }

    // Set per-slot filament states for all slots with filament
    // This allows non-active slots to show their filament color/position
    ui_filament_path_canvas_clear_slot_filaments(path_canvas_);
    for (int i = 0; i < info.total_slots; ++i) {
        PathSegment slot_seg = backend->get_slot_filament_segment(i);
        if (slot_seg != PathSegment::NONE) {
            SlotInfo slot_info = backend->get_slot_info(i);
            ui_filament_path_canvas_set_slot_filament(path_canvas_, i, static_cast<int>(slot_seg),
                                                      slot_info.color_rgb);
        }
    }

    spdlog::trace("[{}] Path canvas updated: slots={}, topology={}, active={}, segment={}",
                  get_name(), info.total_slots, static_cast<int>(topology), info.current_slot,
                  static_cast<int>(segment));
}

// ============================================================================
// Public API
// ============================================================================

void AmsPanel::refresh_slots() {
    if (!panel_ || !subjects_initialized_) {
        return;
    }

    update_slot_colors();

    // Update current slot highlight
    int current_slot = lv_subject_get_int(AmsState::instance().get_current_slot_subject());
    update_current_slot_highlight(current_slot);
}

// ============================================================================
// UI Update Handlers
// ============================================================================

void AmsPanel::update_slot_colors() {
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    AmsBackend* backend = AmsState::instance().get_backend();

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        if (!slot_widgets_[i]) {
            continue;
        }

        if (i >= slot_count) {
            // Hide slots beyond configured count
            lv_obj_add_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);

        // Get slot color from AmsState subject
        lv_subject_t* color_subject = AmsState::instance().get_slot_color_subject(i);
        if (color_subject) {
            uint32_t rgb = static_cast<uint32_t>(lv_subject_get_int(color_subject));
            lv_color_t color = lv_color_hex(rgb);

            // Find color swatch within slot
            lv_obj_t* swatch = lv_obj_find_by_name(slot_widgets_[i], "color_swatch");
            if (swatch) {
                lv_obj_set_style_bg_color(swatch, color, 0);
            }
        }

        // Update material label and fill level from backend slot info
        if (backend) {
            SlotInfo slot_info = backend->get_slot_info(i);

            // Update slot-internal material label
            // Truncate long material names when many slots to prevent overlap
            lv_obj_t* material_label = lv_obj_find_by_name(slot_widgets_[i], "material_label");
            if (material_label) {
                if (!slot_info.material.empty()) {
                    std::string material = slot_info.material;
                    // Truncate to 4 chars when overlapping (5+ slots)
                    if (slot_count > 4 && material.length() > 4) {
                        material = material.substr(0, 4);
                    }
                    lv_label_set_text(material_label, material.c_str());
                } else {
                    lv_label_set_text(material_label, "---");
                }
            }

            // Set fill level from Spoolman weight data
            if (slot_info.total_weight_g > 0.0f) {
                float fill_level = slot_info.remaining_weight_g / slot_info.total_weight_g;
                ui_ams_slot_set_fill_level(slot_widgets_[i], fill_level);
            }
        }

        // Update status indicator
        update_slot_status(i);
    }
}

void AmsPanel::update_slot_status(int slot_index) {
    if (slot_index < 0 || slot_index >= MAX_VISIBLE_SLOTS || !slot_widgets_[slot_index]) {
        return;
    }

    lv_subject_t* status_subject = AmsState::instance().get_slot_status_subject(slot_index);
    if (!status_subject) {
        return;
    }

    auto status = static_cast<SlotStatus>(lv_subject_get_int(status_subject));

    // Find status indicator icon within slot
    lv_obj_t* status_icon = lv_obj_find_by_name(slot_widgets_[slot_index], "status_icon");
    if (!status_icon) {
        return;
    }

    // Update icon based on status
    switch (status) {
    case SlotStatus::EMPTY:
        // Show empty indicator
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_30, 0);
        break;

    case SlotStatus::AVAILABLE:
    case SlotStatus::FROM_BUFFER:
        // Show filament available
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::LOADED:
        // Show loaded (highlighted)
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::BLOCKED:
        // Show error state
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::UNKNOWN:
    default:
        lv_obj_add_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void AmsPanel::update_action_display(AmsAction action) {
    // Action display is handled via bind_text to ams_action_detail
    // This method can add visual feedback (progress indicators, etc.)

    lv_obj_t* progress = lv_obj_find_by_name(panel_, "action_progress");
    if (!progress) {
        return;
    }

    bool show_progress = (action == AmsAction::LOADING || action == AmsAction::UNLOADING ||
                          action == AmsAction::SELECTING || action == AmsAction::RESETTING);

    if (show_progress) {
        lv_obj_remove_flag(progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(progress, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsPanel::update_current_slot_highlight(int slot_index) {
    // Remove highlight from all slots (set border opacity to 0)
    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        if (slot_widgets_[i]) {
            lv_obj_remove_state(slot_widgets_[i], LV_STATE_CHECKED);
            lv_obj_set_style_border_opa(slot_widgets_[i], LV_OPA_0, 0);
        }
    }

    // Add highlight to current slot (show border)
    if (slot_index >= 0 && slot_index < MAX_VISIBLE_SLOTS && slot_widgets_[slot_index]) {
        lv_obj_add_state(slot_widgets_[slot_index], LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(slot_widgets_[slot_index], LV_OPA_100, 0);
    }

    // Update the "Currently Loaded" card in the right column
    update_current_loaded_display(slot_index);
}

void AmsPanel::update_current_loaded_display(int slot_index) {
    if (!panel_) {
        return;
    }

    // Sync subjects for reactive UI binding
    // This updates ams_current_material_text, ams_current_slot_text, ams_current_weight_text,
    // ams_current_has_weight, and ams_current_color subjects which are bound to XML elements
    AmsState::instance().sync_current_loaded_from_backend();

    // Find the swatch element - color binding is not supported in XML, so we set it via C++
    lv_obj_t* current_swatch = lv_obj_find_by_name(panel_, "current_swatch");
    if (current_swatch) {
        // Get color from subject (set by sync_current_loaded_from_backend)
        uint32_t color_rgb = static_cast<uint32_t>(
            lv_subject_get_int(AmsState::instance().get_current_color_subject()));
        lv_color_t color = lv_color_hex(color_rgb);
        lv_obj_set_style_bg_color(current_swatch, color, 0);
        lv_obj_set_style_border_color(current_swatch, color, 0);
    }

    // Update bypass-related state for path canvas visualization
    AmsBackend* backend = AmsState::instance().get_backend();
    bool bypass_active = (slot_index == -2 && backend && backend->is_bypass_active());

    // Update path canvas bypass visualization
    if (path_canvas_) {
        ui_filament_path_canvas_set_bypass_active(path_canvas_, bypass_active);
    }
}

// ============================================================================
// Event Callbacks
// ============================================================================

void AmsPanel::on_path_slot_clicked(int slot_index, void* user_data) {
    auto* self = static_cast<AmsPanel*>(user_data);
    if (!self) {
        return;
    }

    spdlog::info("[AmsPanel] Path slot {} clicked - triggering load", slot_index);

    // Trigger filament load for the clicked slot
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if backend is busy
    AmsSystemInfo info = backend->get_system_info();
    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
        NOTIFY_WARNING("AMS is busy: {}", ams_action_to_string(info.action));
        return;
    }

    AmsError error = backend->load_filament(slot_index);
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Load failed: {}", error.user_msg);
    }
}

void AmsPanel::on_slot_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_slot_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Use current_target (widget callback was registered on) not target (originally clicked
        // child)
        lv_obj_t* slot = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto slot_index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(slot)));
        self->handle_slot_tap(slot_index);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_unload_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_unload_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_unload();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_reset_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_reset_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_reset();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Observer Callbacks
// ============================================================================

void AmsPanel::on_slots_version_changed(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    spdlog::debug("[AmsPanel] Gates version changed - refreshing slots");
    self->refresh_slots();
}

void AmsPanel::on_action_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    auto action = static_cast<AmsAction>(lv_subject_get_int(subject));
    spdlog::debug("[AmsPanel] Action changed: {}", ams_action_to_string(action));
    self->update_action_display(action);
}

void AmsPanel::on_current_slot_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    int slot = lv_subject_get_int(subject);
    spdlog::debug("[AmsPanel] Current slot changed: {}", slot);
    self->update_current_slot_highlight(slot);

    // Also update path canvas when current slot changes
    self->update_path_canvas_from_backend();

    // Auto-set active Spoolman spool when slot becomes active
    if (slot >= 0 && self->api_) {
        auto* backend = AmsState::instance().get_backend();
        if (backend) {
            SlotInfo slot_info = backend->get_slot_info(slot);
            if (slot_info.spoolman_id > 0) {
                spdlog::info("[AmsPanel] Slot {} has Spoolman ID {}, setting as active spool", slot,
                             slot_info.spoolman_id);
                self->api_->set_active_spool(
                    slot_info.spoolman_id,
                    []() { spdlog::debug("[AmsPanel] Active spool set successfully"); },
                    [](const MoonrakerError& err) {
                        spdlog::warn("[AmsPanel] Failed to set active spool: {}", err.message);
                    });
            }
        }
    }
}

void AmsPanel::on_path_state_changed(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    spdlog::debug("[AmsPanel] Path state changed - updating path canvas");
    self->update_path_canvas_from_backend();
}

// ============================================================================
// Action Handlers
// ============================================================================

void AmsPanel::handle_slot_tap(int slot_index) {
    spdlog::info("[{}] Slot {} tapped", get_name(), slot_index);

    // Validate slot index against configured slot count
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (slot_index < 0 || slot_index >= slot_count) {
        spdlog::warn("[{}] Invalid slot index {} (slot_count={})", get_name(), slot_index,
                     slot_count);
        return;
    }

    // Show context menu near the tapped slot
    if (slot_index >= 0 && slot_index < MAX_VISIBLE_SLOTS && slot_widgets_[slot_index]) {
        show_context_menu(slot_index, slot_widgets_[slot_index]);
    }
}

void AmsPanel::handle_unload() {
    spdlog::info("[{}] Unload requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->unload_filament();
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Unload failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_reset() {
    spdlog::info("[{}] Reset requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->reset();
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Reset failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_bypass_toggle() {
    spdlog::info("[{}] Bypass toggle requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if hardware sensor controls bypass (button should be disabled, but check anyway)
    AmsSystemInfo info = backend->get_system_info();
    if (info.has_hardware_bypass_sensor) {
        NOTIFY_WARNING("Bypass controlled by sensor");
        spdlog::warn("[{}] Bypass toggle blocked - hardware sensor controls bypass", get_name());
        return;
    }

    // Check current bypass state and toggle
    bool currently_bypassed = backend->is_bypass_active();
    AmsError error;

    if (currently_bypassed) {
        error = backend->disable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO("Bypass disabled");
        }
    } else {
        error = backend->enable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO("Bypass enabled");
        }
    }

    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Bypass toggle failed: {}", error.user_msg);
    }
    // Switch state updates automatically via bypass_active subject binding
}

// ============================================================================
// Context Menu Management (delegates to helix::ui::AmsContextMenu)
// ============================================================================

void AmsPanel::show_context_menu(int slot_index, lv_obj_t* near_widget) {
    if (!parent_screen_ || !near_widget) {
        return;
    }

    // Create context menu on first use
    if (!context_menu_) {
        context_menu_ = std::make_unique<helix::ui::AmsContextMenu>();
    }

    // Set callback to handle menu actions
    context_menu_->set_action_callback(
        [this](helix::ui::AmsContextMenu::MenuAction action, int slot) {
            AmsBackend* backend = AmsState::instance().get_backend();

            switch (action) {
            case helix::ui::AmsContextMenu::MenuAction::LOAD:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                // Check if backend is busy
                {
                    AmsSystemInfo info = backend->get_system_info();
                    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
                        NOTIFY_WARNING("AMS is busy: {}", ams_action_to_string(info.action));
                        return;
                    }
                }
                {
                    AmsError error = backend->load_filament(slot);
                    if (error.result != AmsResult::SUCCESS) {
                        NOTIFY_ERROR("Load failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::UNLOAD:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                {
                    AmsError error = backend->unload_filament();
                    if (error.result != AmsResult::SUCCESS) {
                        NOTIFY_ERROR("Unload failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::EDIT:
                show_edit_modal(slot);
                break;

            case helix::ui::AmsContextMenu::MenuAction::SPOOLMAN:
                show_spoolman_picker(slot);
                break;

            case helix::ui::AmsContextMenu::MenuAction::CANCELLED:
            default:
                break;
            }
        });

    // Show the menu near the slot widget
    context_menu_->show_near_widget(parent_screen_, slot_index, near_widget);
}

// ============================================================================
// Spoolman Picker Management (delegates to helix::ui::AmsSpoolmanPicker)
// ============================================================================

void AmsPanel::show_spoolman_picker(int slot_index) {
    if (!parent_screen_) {
        spdlog::warn("[{}] Cannot show picker - no parent screen", get_name());
        return;
    }

    // Create picker on first use
    if (!spoolman_picker_) {
        spoolman_picker_ = std::make_unique<helix::ui::AmsSpoolmanPicker>();
    }

    // Get current spoolman_id for this slot
    int current_spool_id = 0;
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        SlotInfo slot_info = backend->get_slot_info(slot_index);
        current_spool_id = slot_info.spoolman_id;
    }

    // Set callback to handle picker results
    spoolman_picker_->set_completion_callback(
        [this](const helix::ui::AmsSpoolmanPicker::PickerResult& result) {
            AmsBackend* backend = AmsState::instance().get_backend();
            if (!backend) {
                return;
            }

            switch (result.action) {
            case helix::ui::AmsSpoolmanPicker::PickerAction::ASSIGN: {
                // Enrich slot with Spoolman data
                SlotInfo slot_info = backend->get_slot_info(result.slot_index);
                slot_info.spoolman_id = result.spool_id;

                const SpoolInfo& spool = result.spool_info;
                slot_info.color_name = spool.color_name;
                slot_info.material = spool.material;
                slot_info.brand = spool.vendor;
                slot_info.spool_name = spool.vendor + " " + spool.material;
                slot_info.remaining_weight_g = static_cast<float>(spool.remaining_weight_g);
                slot_info.total_weight_g = static_cast<float>(spool.initial_weight_g);
                slot_info.nozzle_temp_min = spool.nozzle_temp_min;
                slot_info.nozzle_temp_max = spool.nozzle_temp_max;
                slot_info.bed_temp = spool.bed_temp_recommended;

                // Parse color hex to RGB
                if (!spool.color_hex.empty()) {
                    std::string hex = spool.color_hex;
                    if (hex[0] == '#') {
                        hex = hex.substr(1);
                    }
                    try {
                        slot_info.color_rgb = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
                    } catch (...) {
                        spdlog::warn("[AmsPanel] Failed to parse color hex: {}", spool.color_hex);
                    }
                }

                backend->set_slot_info(result.slot_index, slot_info);
                AmsState::instance().sync_from_backend();
                refresh_slots();
                NOTIFY_INFO("Spool assigned to Slot {}", result.slot_index + 1);
                break;
            }

            case helix::ui::AmsSpoolmanPicker::PickerAction::UNLINK: {
                SlotInfo slot_info = backend->get_slot_info(result.slot_index);
                slot_info.spoolman_id = 0;
                slot_info.spool_name.clear();
                slot_info.remaining_weight_g = -1;
                slot_info.total_weight_g = -1;
                backend->set_slot_info(result.slot_index, slot_info);
                AmsState::instance().sync_from_backend();
                refresh_slots();
                NOTIFY_INFO("Slot {} assignment cleared", result.slot_index + 1);
                break;
            }

            case helix::ui::AmsSpoolmanPicker::PickerAction::CANCELLED:
            default:
                break;
            }
        });

    // Show the picker
    spoolman_picker_->show_for_slot(parent_screen_, slot_index, current_spool_id, api_);
}

// ============================================================================
// Edit Modal (delegated to helix::ui::AmsEditModal)
// ============================================================================

void AmsPanel::show_edit_modal(int slot_index) {
    if (!parent_screen_) {
        spdlog::warn("[{}] Cannot show edit modal - no parent screen", get_name());
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Create modal on first use (lazy initialization)
    if (!edit_modal_) {
        edit_modal_ = std::make_unique<helix::ui::AmsEditModal>();
    }

    // Get current slot info
    SlotInfo initial_info = backend->get_slot_info(slot_index);

    // Set completion callback to handle save result
    edit_modal_->set_completion_callback([this](const helix::ui::AmsEditModal::EditResult& result) {
        if (result.saved && result.slot_index >= 0) {
            // Apply the edited slot info to the backend
            AmsBackend* backend = AmsState::instance().get_backend();
            if (backend) {
                backend->set_slot_info(result.slot_index, result.slot_info);

                // Update the slot display
                AmsState::instance().sync_from_backend();
                refresh_slots();

                NOTIFY_INFO("Slot {} updated", result.slot_index + 1);
            }
        }
    });

    // Show the modal
    edit_modal_->show_for_slot(parent_screen_, slot_index, initial_info, api_);
}

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<AmsPanel> g_ams_panel;
static lv_obj_t* s_ams_panel_obj = nullptr;

void destroy_ams_panel_ui() {
    if (s_ams_panel_obj) {
        spdlog::info("[AMS Panel] Destroying panel UI to free memory");

        // Unregister close callback BEFORE deleting to prevent double-invocation
        // (e.g., if destroy called manually while panel is in overlay stack)
        NavigationManager::instance().unregister_overlay_close_callback(s_ams_panel_obj);

        // Clear the panel_ reference in AmsPanel before deleting
        if (g_ams_panel) {
            g_ams_panel->clear_panel_reference();
        }

        lv_obj_delete(s_ams_panel_obj);
        s_ams_panel_obj = nullptr;

        // Note: Widget registrations remain (LVGL doesn't support unregistration)
        // Note: g_ams_panel C++ object stays for state preservation
    }
}

AmsPanel& get_global_ams_panel() {
    if (!g_ams_panel) {
        g_ams_panel = std::make_unique<AmsPanel>(get_printer_state(), nullptr);
    }

    // Lazy create the panel UI if not yet created
    if (!s_ams_panel_obj && g_ams_panel) {
        // Ensure widgets and XML are registered
        ensure_ams_widgets_registered();

        // Create the panel on the active screen
        lv_obj_t* screen = lv_scr_act();
        s_ams_panel_obj = static_cast<lv_obj_t*>(lv_xml_create(screen, "ams_panel", nullptr));

        if (s_ams_panel_obj) {
            // Initialize subjects if needed
            if (!g_ams_panel->are_subjects_initialized()) {
                g_ams_panel->init_subjects();
            }

            // Setup the panel
            g_ams_panel->setup(s_ams_panel_obj, screen);
            lv_obj_add_flag(s_ams_panel_obj, LV_OBJ_FLAG_HIDDEN); // Hidden by default

            // Register close callback to destroy UI when overlay is closed
            NavigationManager::instance().register_overlay_close_callback(
                s_ams_panel_obj, []() { destroy_ams_panel_ui(); });

            spdlog::info("[AMS Panel] Lazy-created panel UI with close callback");
        } else {
            spdlog::error("[AMS Panel] Failed to create panel from XML");
        }
    }

    return *g_ams_panel;
}
