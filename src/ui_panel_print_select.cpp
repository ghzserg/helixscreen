// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_select.h"

#include "ui_async_callback.h"
#include "ui_error_reporting.h"
#include "ui_fonts.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_print_status.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_utils.h"

#include "app_globals.h"
#include "config.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "usb_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for class-based API
PrintStatusPanel& get_global_print_status_panel();

// Note: strip_gcode_extension() moved to ui_utils.h for DRY reuse across panels

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<PrintSelectPanel> g_print_select_panel;

PrintSelectPanel* get_print_select_panel(PrinterState& printer_state, MoonrakerAPI* api) {
    if (!g_print_select_panel) {
        g_print_select_panel = std::make_unique<PrintSelectPanel>(printer_state, api);
    }
    return g_print_select_panel.get();
}

PrintSelectPanel& get_global_print_select_panel() {
    if (!g_print_select_panel) {
        spdlog::error(
            "[PrintSelectPanel] get_global_print_select_panel() called before panel created");
    }
    return *g_print_select_panel;
}

// ============================================================================
// Static XML Event Callbacks (registered via lv_xml_register_event_cb)
// ============================================================================

static void on_print_select_view_toggle(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().toggle_view();
}

static void on_print_select_source_printer(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().on_source_printer_clicked();
}

static void on_print_select_source_usb(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().on_source_usb_clicked();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

PrintSelectPanel::PrintSelectPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::debug("[{}] Constructed", get_name());
}

PrintSelectPanel::~PrintSelectPanel() {
    // CRITICAL: During static destruction (app exit), LVGL may already be gone.
    // We check if LVGL is still initialized before calling any LVGL functions.
    // lv_is_initialized() returns false after lv_deinit() is called.
    if (lv_is_initialized()) {
        // Remove scroll event callbacks to prevent use-after-free
        // These were registered with 'this' pointer as user_data
        if (card_view_container_) {
            lv_obj_remove_event_cb(card_view_container_, on_scroll_static);
        }
        if (list_rows_container_) {
            lv_obj_remove_event_cb(list_rows_container_, on_scroll_static);
        }

        // Delete pending timer
        if (refresh_timer_) {
            lv_timer_delete(refresh_timer_);
            refresh_timer_ = nullptr;
        }
    }

    // Clear pool vectors - they hold raw pointers to LVGL widgets which
    // are owned by LVGL's widget tree and cleaned up by lv_deinit().
    // We just clear our references to avoid dangling pointers.
    card_pool_.clear();
    card_pool_indices_.clear();
    list_pool_.clear();
    list_pool_indices_.clear();

    // Reset our pointers - the LVGL widget tree handles widget cleanup.
    card_view_container_ = nullptr;
    list_view_container_ = nullptr;
    list_rows_container_ = nullptr;
    empty_state_container_ = nullptr;
    view_toggle_btn_ = nullptr;
    view_toggle_icon_ = nullptr;
    detail_view_widget_ = nullptr;
    confirmation_dialog_widget_ = nullptr;
    print_status_panel_widget_ = nullptr;
    source_printer_btn_ = nullptr;
    source_usb_btn_ = nullptr;
    card_leading_spacer_ = nullptr;
    card_trailing_spacer_ = nullptr;
    list_leading_spacer_ = nullptr;
    list_trailing_spacer_ = nullptr;

    // NOTE: Do NOT log here - spdlog may be destroyed first
}

// ============================================================================
// PanelBase Implementation
// ============================================================================

void PrintSelectPanel::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize selected file subjects
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filename_subject_, selected_filename_buffer_, "",
                                        "selected_filename");

    // Thumbnail uses POINTER subject (required by lv_image_bind_src)
    strncpy(selected_thumbnail_buffer_, DEFAULT_PLACEHOLDER_THUMB,
            sizeof(selected_thumbnail_buffer_) - 1);
    UI_SUBJECT_INIT_AND_REGISTER_POINTER(selected_thumbnail_subject_, selected_thumbnail_buffer_,
                                         "selected_thumbnail");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_print_time_subject_, selected_print_time_buffer_,
                                        "", "selected_print_time");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filament_weight_subject_,
                                        selected_filament_weight_buffer_, "",
                                        "selected_filament_weight");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_layer_count_subject_, selected_layer_count_buffer_,
                                        "", "selected_layer_count");

    // Initialize detail view visibility subject (0 = hidden, 1 = visible)
    UI_SUBJECT_INIT_AND_REGISTER_INT(detail_view_visible_subject_, 0, "detail_view_visible");

    // Initialize view mode subject (0 = CARD, 1 = LIST) - XML bindings control container visibility
    UI_SUBJECT_INIT_AND_REGISTER_INT(view_mode_subject_, 0, "print_select_view_mode");

    // Initialize can print subject (1 = can print, 0 = print in progress)
    // XML binding disables print button when value is 0
    bool can_print = printer_state_.can_start_new_print();
    UI_SUBJECT_INIT_AND_REGISTER_INT(can_print_subject_, can_print ? 1 : 0,
                                     "print_select_can_print");

    // Register XML event callbacks (must be done BEFORE XML is created)
    lv_xml_register_event_cb(nullptr, "on_print_select_view_toggle", on_print_select_view_toggle);
    lv_xml_register_event_cb(nullptr, "on_print_select_source_printer",
                             on_print_select_source_printer);
    lv_xml_register_event_cb(nullptr, "on_print_select_source_usb", on_print_select_source_usb);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void PrintSelectPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] Cannot setup: panel is null", get_name());
        return;
    }

    // Find widget references
    card_view_container_ = lv_obj_find_by_name(panel_, "card_view_container");
    list_view_container_ = lv_obj_find_by_name(panel_, "list_view_container");
    list_rows_container_ = lv_obj_find_by_name(panel_, "list_rows_container");
    empty_state_container_ = lv_obj_find_by_name(panel_, "empty_state_container");
    view_toggle_btn_ = lv_obj_find_by_name(panel_, "view_toggle_btn");
    view_toggle_icon_ = lv_obj_find_by_name(panel_, "view_toggle_icon");

    if (!card_view_container_ || !list_view_container_ || !list_rows_container_ ||
        !empty_state_container_ || !view_toggle_btn_ || !view_toggle_icon_) {
        spdlog::error("[{}] Failed to find required widgets", get_name());
        return;
    }

    // Register scroll event handlers for progressive loading
    lv_obj_add_event_cb(card_view_container_, on_scroll_static, LV_EVENT_SCROLL, this);
    lv_obj_add_event_cb(list_rows_container_, on_scroll_static, LV_EVENT_SCROLL, this);

    // Note: view_toggle_btn click handler is now in XML via <event_cb>

    // Setup source selector buttons (Printer/USB)
    setup_source_buttons();

    // Wire up column header click handlers
    // We need to pack both 'this' and column index into user_data
    // Use a simple encoding: store column index in the low bits of a pointer-sized int
    struct HeaderBinding {
        const char* name;
        PrintSelectSortColumn column;
    };
    static const HeaderBinding headers[] = {
        {"header_filename", PrintSelectSortColumn::FILENAME},
        {"header_size", PrintSelectSortColumn::SIZE},
        {"header_modified", PrintSelectSortColumn::MODIFIED},
        {"header_print_time", PrintSelectSortColumn::PRINT_TIME}};

    for (const auto& binding : headers) {
        lv_obj_t* header = lv_obj_find_by_name(panel_, binding.name);
        if (header) {
            // Encode column in user_data: we'll use the address of the static array entry
            // The callback will recover the column from the difference
            lv_obj_add_event_cb(header, on_header_clicked_static, LV_EVENT_CLICKED, this);
            // Store column index in the object's user_data for recovery
            lv_obj_set_user_data(header,
                                 reinterpret_cast<void*>(static_cast<intptr_t>(binding.column)));
        }
    }

    // Create detail view (confirmation dialog created on-demand)
    create_detail_view();

    // Register resize callback
    // Note: ui_resize_handler_register expects a C callback, so we use a static trampoline
    // We store 'this' in a static variable since the resize system doesn't support user_data
    // This is safe because there's only one PrintSelectPanel instance
    static PrintSelectPanel* resize_self = nullptr;
    resize_self = this;
    ui_resize_handler_register([]() {
        if (resize_self) {
            resize_self->handle_resize();
        }
    });

    // Mark panel as fully initialized (enables resize callbacks)
    panel_initialized_ = true;

    // Refresh from Moonraker when API becomes available (via set_api)
    // Don't populate anything here - wait for API connection
    if (api_) {
        refresh_files();
    } else {
        spdlog::debug("[{}] MoonrakerAPI not available yet, waiting for set_api()", get_name());
        update_empty_state();
    }

    // Register observer on active_panel subject to trigger on_activate() when panel becomes visible
    // This is needed because ui_nav doesn't call lifecycle hooks on C++ panel classes
    // ObserverGuard handles cleanup automatically in destructor
    lv_subject_t* active_panel_subject = lv_xml_get_subject(NULL, "active_panel");
    if (active_panel_subject) {
        active_panel_observer_ = ObserverGuard(
            active_panel_subject,
            [](lv_observer_t* observer, lv_subject_t* subject) {
                auto* self = static_cast<PrintSelectPanel*>(lv_observer_get_user_data(observer));
                int32_t panel_id = lv_subject_get_int(subject);
                if (panel_id == UI_PANEL_PRINT_SELECT && self) {
                    self->on_activate();
                }
            },
            this);
        spdlog::debug("[{}] Registered observer on active_panel subject for lazy file loading",
                      get_name());
    }

    // Register observer on connection state to refresh files when printer connects
    // This handles the race condition where panel activates before WebSocket connection
    // ObserverGuard handles cleanup automatically in destructor
    lv_subject_t* connection_subject = printer_state_.get_printer_connection_state_subject();
    if (connection_subject) {
        connection_observer_ = ObserverGuard(
            connection_subject,
            [](lv_observer_t* observer, lv_subject_t* subject) {
                auto* self = static_cast<PrintSelectPanel*>(lv_observer_get_user_data(observer));
                int32_t state = lv_subject_get_int(subject);
                // PrinterStatus::CONNECTED = 2
                if (state == 2 && self) {
                    // Refresh files if empty
                    if (self->file_list_.empty() && self->current_source_ == FileSource::PRINTER) {
                        spdlog::info("[{}] Connection established, refreshing file list",
                                     self->get_name());
                        self->refresh_files();
                    }

                    // Check for helix_print plugin on each connection/reconnection
                    if (self->api_) {
                        spdlog::debug(
                            "[{}] Connection established, checking for helix_print plugin",
                            self->get_name());
                        self->api_->check_helix_plugin(
                            [](bool available) {
                                if (available) {
                                    spdlog::info("[PrintSelectPanel] helix_print plugin available");
                                } else {
                                    spdlog::debug(
                                        "[PrintSelectPanel] helix_print plugin not available");
                                }
                            },
                            [](const MoonrakerError&) {
                                // Silently ignore errors - plugin not available
                            });
                    }
                }
            },
            this);
        spdlog::debug("[{}] Registered observer on connection state for auto-refresh", get_name());
    }

    // Register observer on print job state to enable/disable print button
    // Prevents starting a new print while one is already in progress
    lv_subject_t* print_state_subject = printer_state_.get_print_state_subject();
    if (print_state_subject) {
        print_state_observer_ = ObserverGuard(
            print_state_subject,
            [](lv_observer_t* observer, lv_subject_t* /*subject*/) {
                auto* self = static_cast<PrintSelectPanel*>(lv_observer_get_user_data(observer));
                if (self) {
                    self->update_print_button_state();
                }
            },
            this);
        spdlog::debug("[{}] Registered observer on print job state for print button", get_name());
    }

    spdlog::debug("[{}] Setup complete", get_name());
}

// ============================================================================
// Public API
// ============================================================================

void PrintSelectPanel::toggle_view() {
    if (current_view_mode_ == PrintSelectViewMode::CARD) {
        // Switch to list view
        current_view_mode_ = PrintSelectViewMode::LIST;

        // Update reactive subject - XML bindings handle container visibility
        lv_subject_set_int(&view_mode_subject_, 1);

        // Update icon to show grid_view (indicates you can switch back to card view)
        const void* grid_icon = lv_xml_get_image(NULL, "mat_grid_view_img");
        if (grid_icon) {
            lv_image_set_src(view_toggle_icon_, grid_icon);
        }
        spdlog::debug("[{}] Switched to list view", get_name());

        // Populate list view (initializes pool if needed)
        populate_list_view();
    } else {
        // Switch to card view
        current_view_mode_ = PrintSelectViewMode::CARD;

        // Update reactive subject - XML bindings handle container visibility
        lv_subject_set_int(&view_mode_subject_, 0);

        // Update icon to show list (indicates you can switch to list view)
        const void* list_icon = lv_xml_get_image(NULL, "mat_list_img");
        if (list_icon) {
            lv_image_set_src(view_toggle_icon_, list_icon);
        }
        spdlog::debug("[{}] Switched to card view", get_name());

        // Repopulate card view
        populate_card_view();
    }

    update_empty_state();
}

void PrintSelectPanel::sort_by(PrintSelectSortColumn column) {
    // Toggle direction if same column, otherwise default to ascending
    if (column == current_sort_column_) {
        current_sort_direction_ = (current_sort_direction_ == PrintSelectSortDirection::ASCENDING)
                                      ? PrintSelectSortDirection::DESCENDING
                                      : PrintSelectSortDirection::ASCENDING;
    } else {
        current_sort_column_ = column;
        current_sort_direction_ = PrintSelectSortDirection::ASCENDING;
    }

    apply_sort();
    update_sort_indicators();

    // Repopulate current view
    if (current_view_mode_ == PrintSelectViewMode::CARD) {
        populate_card_view();
    } else {
        populate_list_view();
    }

    spdlog::debug("[{}] Sorted by column {}, direction {}", get_name(), static_cast<int>(column),
                  static_cast<int>(current_sort_direction_));
}

void PrintSelectPanel::refresh_files() {
    if (!api_) {
        spdlog::warn("[{}] Cannot refresh files: MoonrakerAPI not initialized", get_name());
        return;
    }

    // Check if WebSocket is actually connected before attempting to send requests
    // This prevents the race condition where set_api() is called before connection is established
    ConnectionState state = api_->get_client().get_connection_state();
    if (state != ConnectionState::CONNECTED) {
        spdlog::debug("[{}] Cannot refresh files: not connected (state={})", get_name(),
                      static_cast<int>(state));
        return;
    }

    spdlog::debug("[{}] Refreshing file list from Moonraker (path: '{}')...", get_name(),
                  current_path_.empty() ? "/" : current_path_);

    // Capture 'this' for async callbacks
    auto* self = this;

    // Request file list from current directory (non-recursive)
    api_->list_files(
        "gcodes", current_path_, false,
        // Success callback
        [self](const std::vector<FileInfo>& files) {
            spdlog::debug("[{}] Received {} items from Moonraker", self->get_name(), files.size());

            // Clear existing file list and metadata tracking
            self->file_list_.clear();
            self->metadata_fetched_.clear();

            // Add ".." parent directory entry if not at root
            if (!self->current_path_.empty()) {
                PrintFileData parent_dir;
                parent_dir.filename = "..";
                parent_dir.is_dir = true;
                parent_dir.thumbnail_path = self->FOLDER_UP_ICON;
                parent_dir.size_str = "Go up";
                parent_dir.print_time_str = "";
                parent_dir.filament_str = "";
                parent_dir.modified_str = "";
                self->file_list_.push_back(parent_dir);
            }

            // Convert FileInfo to PrintFileData (include directories)
            for (const auto& file : files) {
                PrintFileData data;
                data.filename = file.filename;
                data.is_dir = file.is_dir;
                data.file_size_bytes = file.size;
                data.modified_timestamp = static_cast<time_t>(file.modified);

                if (file.is_dir) {
                    // Directory - use folder icon
                    data.thumbnail_path = self->FOLDER_ICON;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = "Folder";
                    data.print_time_str = "";
                    data.filament_str = "";
                } else {
                    // Only process .gcode files
                    if (file.filename.find(".gcode") == std::string::npos &&
                        file.filename.find(".g") == std::string::npos) {
                        continue;
                    }

                    data.thumbnail_path = self->DEFAULT_PLACEHOLDER_THUMB;
                    data.print_time_minutes = 0;
                    data.filament_grams = 0.0f;
                    data.size_str = format_file_size(data.file_size_bytes);
                    data.print_time_str = format_print_time(data.print_time_minutes);
                    data.filament_str = format_filament_weight(data.filament_grams);
                }

                data.modified_str = format_modified_date(data.modified_timestamp);
                self->file_list_.push_back(data);
            }

            // Count files vs directories (can be done on any thread)
            size_t dir_count = 0, file_count = 0;
            for (const auto& item : self->file_list_) {
                if (item.is_dir)
                    dir_count++;
                else
                    file_count++;
            }
            spdlog::info("[{}] File list updated: {} directories, {} G-code files",
                         self->get_name(), dir_count, file_count);

            // Dispatch LVGL operations to main thread (LVGL is not thread-safe)
            lv_async_call(
                [](void* user_data) {
                    auto* panel = static_cast<PrintSelectPanel*>(user_data);

                    // Show files immediately with placeholder metadata
                    panel->apply_sort();
                    panel->update_sort_indicators();
                    panel->populate_card_view();
                    panel->populate_list_view();
                    panel->update_empty_state();

                    // Check for pending file selection (--select-file flag or API call)
                    std::string pending;
                    if (!panel->pending_file_selection_.empty()) {
                        pending = panel->pending_file_selection_;
                        panel->pending_file_selection_.clear(); // Clear to avoid repeat
                    } else if (get_runtime_config().select_file != nullptr) {
                        // Check runtime config on first load (--select-file CLI flag)
                        static bool select_file_checked = false;
                        if (!select_file_checked) {
                            pending = get_runtime_config().select_file;
                            select_file_checked = true;
                        }
                    }
                    if (!pending.empty()) {
                        if (!panel->select_file_by_name(pending)) {
                            spdlog::warn("[{}] Pending file selection '{}' not found in file list",
                                         panel->get_name(), pending);
                        }
                    }

                    // Now that views are populated, trigger metadata fetch for VISIBLE items only
                    // This ensures thumbnails arrive AFTER cards exist, and we don't flood the
                    // network with requests for off-screen files
                    size_t visible_start = 0;
                    size_t visible_end = 0;

                    if (panel->current_view_mode_ == PrintSelectViewMode::CARD) {
                        // Card view: compute from visible rows
                        if (panel->visible_start_row_ >= 0 && panel->visible_end_row_ >= 0) {
                            visible_start = static_cast<size_t>(panel->visible_start_row_) *
                                            static_cast<size_t>(panel->cards_per_row_);
                            visible_end = static_cast<size_t>(panel->visible_end_row_) *
                                          static_cast<size_t>(panel->cards_per_row_);
                        }
                    } else {
                        // List view: use visible list indices
                        if (panel->visible_list_start_ >= 0 && panel->visible_list_end_ >= 0) {
                            visible_start = static_cast<size_t>(panel->visible_list_start_);
                            visible_end = static_cast<size_t>(panel->visible_list_end_);
                        }
                    }

                    // Fallback: if no visible range computed, fetch first ~20 files
                    if (visible_end == 0 && !panel->file_list_.empty()) {
                        visible_end = std::min(panel->file_list_.size(), size_t{20});
                    }

                    panel->fetch_metadata_range(visible_start, visible_end);
                },
                self);
        },
        // Error callback
        [self](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to refresh file list");
            LOG_ERROR_INTERNAL("[{}] File list refresh error: {} ({})", self->get_name(),
                               error.message, error.get_type_string());
        });
}

void PrintSelectPanel::fetch_metadata_range(size_t start, size_t end) {
    if (!api_) {
        return;
    }

    // Clamp range to file list bounds
    start = std::min(start, file_list_.size());
    end = std::min(end, file_list_.size());

    if (start >= end) {
        return;
    }

    // Ensure tracking vector is properly sized
    if (metadata_fetched_.size() != file_list_.size()) {
        metadata_fetched_.resize(file_list_.size(), false);
    }

    auto* self = this;
    size_t fetch_count = 0;

    // Fetch metadata for files in range only (not directories, not already fetched)
    for (size_t i = start; i < end; i++) {
        if (file_list_[i].is_dir)
            continue; // Skip directories

        if (metadata_fetched_[i])
            continue; // Already fetched or in flight

        // Mark as fetched immediately to prevent duplicate requests
        metadata_fetched_[i] = true;
        fetch_count++;

        const std::string filename = file_list_[i].filename;

        api_->get_file_metadata(
            filename,
            // Metadata success callback (runs on background thread)
            [self, i, filename](const FileMetadata& metadata) {
                // Extract all values on background thread (safe - metadata is const ref)
                int print_time_minutes = static_cast<int>(metadata.estimated_time / 60.0);
                float filament_grams = static_cast<float>(metadata.filament_weight_total);
                std::string filament_type = metadata.filament_type;
                std::string thumb_path = metadata.get_largest_thumbnail();
                uint32_t layer_count = metadata.layer_count;

                // Format strings on background thread (uses standalone helper functions)
                std::string print_time_str = format_print_time(print_time_minutes);
                std::string filament_str = format_filament_weight(filament_grams);
                std::string layer_count_str = layer_count > 0 ? std::to_string(layer_count) : "--";

                // Check if thumbnail is a local file (background thread - filesystem OK)
                bool thumb_is_local = !thumb_path.empty() && std::filesystem::exists(thumb_path);

                // Prepare cache path for remote thumbnails
                std::string cache_file;
                if (!thumb_path.empty() && !thumb_is_local) {
                    std::hash<std::string> hasher;
                    size_t hash = hasher(thumb_path);
                    cache_file = "/tmp/helix_thumbs/" + std::to_string(hash) + ".png";
                    std::filesystem::create_directories("/tmp/helix_thumbs");
                }

                // CRITICAL: Dispatch file_list_ modifications to main thread to avoid race
                // conditions with populate_card_view/populate_list_view reading file_list_
                struct MetadataUpdate {
                    PrintSelectPanel* panel;
                    size_t index;
                    std::string filename;
                    int print_time_minutes;
                    float filament_grams;
                    std::string filament_type;
                    std::string print_time_str;
                    std::string filament_str;
                    uint32_t layer_count;
                    std::string layer_count_str;
                    std::string thumb_path;
                    std::string cache_file;
                    bool thumb_is_local;
                };

                ui_async_call_safe<MetadataUpdate>(
                    std::make_unique<MetadataUpdate>(
                        MetadataUpdate{self, i, filename, print_time_minutes, filament_grams,
                                       filament_type, print_time_str, filament_str, layer_count,
                                       layer_count_str, thumb_path, cache_file, thumb_is_local}),
                    [](MetadataUpdate* d) {
                        auto* self = d->panel;

                        // Bounds check (file_list could change during async operation)
                        if (d->index >= self->file_list_.size() ||
                            self->file_list_[d->index].filename != d->filename) {
                            spdlog::warn("[{}] File list changed during metadata fetch for {}",
                                         self->get_name(), d->filename);
                            return;
                        }

                        // Update metadata fields (now on main thread - safe!)
                        self->file_list_[d->index].print_time_minutes = d->print_time_minutes;
                        self->file_list_[d->index].filament_grams = d->filament_grams;
                        self->file_list_[d->index].filament_type = d->filament_type;
                        self->file_list_[d->index].print_time_str = d->print_time_str;
                        self->file_list_[d->index].filament_str = d->filament_str;
                        self->file_list_[d->index].layer_count = d->layer_count;
                        self->file_list_[d->index].layer_count_str = d->layer_count_str;

                        spdlog::trace("[{}] Updated metadata for {}: {}min, {}g, {} layers",
                                      self->get_name(), d->filename, d->print_time_minutes,
                                      d->filament_grams, d->layer_count);

                        // Handle thumbnail
                        if (!d->thumb_path.empty() && self->api_) {
                            if (d->thumb_is_local) {
                                // Local file exists - use directly (mock mode)
                                self->file_list_[d->index].thumbnail_path = "A:" + d->thumb_path;
                                spdlog::trace("[{}] Using local thumbnail for {}: {}",
                                              self->get_name(), d->filename,
                                              self->file_list_[d->index].thumbnail_path);
                            } else {
                                // Remote path - download from Moonraker
                                spdlog::trace("[{}] Downloading thumbnail for {}: {} -> {}",
                                              self->get_name(), d->filename, d->thumb_path,
                                              d->cache_file);

                                size_t file_idx = d->index;
                                std::string filename_copy = d->filename;
                                self->api_->download_thumbnail(
                                    d->thumb_path, d->cache_file,
                                    // Success callback - also dispatch to main thread!
                                    [self, file_idx, filename_copy](const std::string& local_path) {
                                        struct ThumbUpdate {
                                            PrintSelectPanel* panel;
                                            size_t index;
                                            std::string filename;
                                            std::string local_path;
                                        };
                                        ui_async_call_safe<ThumbUpdate>(
                                            std::make_unique<ThumbUpdate>(ThumbUpdate{
                                                self, file_idx, filename_copy, local_path}),
                                            [](ThumbUpdate* t) {
                                                if (t->index < t->panel->file_list_.size() &&
                                                    t->panel->file_list_[t->index].filename ==
                                                        t->filename) {
                                                    t->panel->file_list_[t->index].thumbnail_path =
                                                        "A:" + t->local_path;
                                                    spdlog::debug(
                                                        "[{}] Thumbnail cached for {}: {}",
                                                        t->panel->get_name(), t->filename,
                                                        t->panel->file_list_[t->index]
                                                            .thumbnail_path);
                                                    t->panel->schedule_view_refresh();
                                                }
                                            });
                                    },
                                    // Error callback
                                    [self, filename_copy](const MoonrakerError& error) {
                                        spdlog::warn("[{}] Failed to download thumbnail for {}: {}",
                                                     self->get_name(), filename_copy,
                                                     error.message);
                                    });
                            }
                        }

                        // Schedule debounced view refresh
                        self->schedule_view_refresh();

                        // Update detail view if this file is currently selected
                        if (strcmp(self->selected_filename_buffer_, d->filename.c_str()) == 0) {
                            spdlog::debug("[{}] Updating detail view for selected file: {}",
                                          self->get_name(), d->filename);
                            self->set_selected_file(
                                d->filename.c_str(),
                                self->file_list_[d->index].thumbnail_path.c_str(),
                                d->print_time_str.c_str(), d->filament_str.c_str(),
                                d->layer_count_str.c_str());
                        }
                    });
            },
            // Metadata error callback
            [self, filename](const MoonrakerError& error) {
                spdlog::warn("[{}] Failed to get metadata for {}: {} ({})", self->get_name(),
                             filename, error.message, error.get_type_string());
            });
    }

    if (fetch_count > 0) {
        spdlog::debug("[{}] fetch_metadata_range({}, {}): started {} metadata requests", get_name(),
                      start, end, fetch_count);
    }
}

void PrintSelectPanel::set_api(MoonrakerAPI* api) {
    api_ = api;

    // Note: Don't auto-refresh here - WebSocket may not be connected yet.
    // refresh_files() has a connection check that will silently return if not connected.
    // Files will be loaded lazily via on_activate() when user navigates to this panel.
    // helix_print plugin check happens in connection observer (after connection established)
    if (api_ && panel_initialized_) {
        spdlog::debug("[{}] API set, files will load on first view", get_name());
        refresh_files(); // Will early-return if not connected
    }
}

void PrintSelectPanel::on_activate() {
    // Always refresh when panel becomes visible to pick up external changes
    // (e.g., files uploaded via web UI, deleted from another client)
    // This also handles the race condition where set_api() was called before WebSocket connection
    if (current_source_ == FileSource::PRINTER && api_) {
        spdlog::info("[{}] Panel activated, refreshing file list", get_name());
        refresh_files();
    } else if (current_source_ == FileSource::USB) {
        spdlog::info("[{}] Panel activated, refreshing USB file list", get_name());
        refresh_usb_files();
    }
}

void PrintSelectPanel::navigate_to_directory(const std::string& dirname) {
    // Build new path
    if (current_path_.empty()) {
        current_path_ = dirname;
    } else {
        current_path_ = current_path_ + "/" + dirname;
    }

    spdlog::info("[{}] Navigating to directory: {}", get_name(), current_path_);
    refresh_files();
}

void PrintSelectPanel::navigate_up() {
    // Don't navigate above root
    if (current_path_.empty()) {
        spdlog::debug("[{}] Already at root, cannot navigate up", get_name());
        return;
    }

    // Find last path separator and truncate
    size_t last_slash = current_path_.rfind('/');
    if (last_slash == std::string::npos) {
        // No slash found - we're one level deep, go to root
        current_path_.clear();
    } else {
        // Truncate at last slash
        current_path_ = current_path_.substr(0, last_slash);
    }

    spdlog::info("[{}] Navigating up to: {}", get_name(),
                 current_path_.empty() ? "/" : current_path_);
    refresh_files();
}

void PrintSelectPanel::set_selected_file(const char* filename, const char* thumbnail_src,
                                         const char* print_time, const char* filament_weight,
                                         const char* layer_count) {
    lv_subject_copy_string(&selected_filename_subject_, filename);

    // Thumbnail uses POINTER subject - copy to buffer then update pointer
    strncpy(selected_thumbnail_buffer_, thumbnail_src, sizeof(selected_thumbnail_buffer_) - 1);
    selected_thumbnail_buffer_[sizeof(selected_thumbnail_buffer_) - 1] = '\0';
    lv_subject_set_pointer(&selected_thumbnail_subject_, selected_thumbnail_buffer_);

    lv_subject_copy_string(&selected_print_time_subject_, print_time);
    lv_subject_copy_string(&selected_filament_weight_subject_, filament_weight);
    lv_subject_copy_string(&selected_layer_count_subject_, layer_count);

    spdlog::info("[{}] Selected file: {}", get_name(), filename);
}

void PrintSelectPanel::show_detail_view() {
    if (detail_view_widget_) {
        // Trigger async scan for embedded G-code operations (for conflict detection)
        std::string filename(selected_filename_buffer_);
        if (!filename.empty()) {
            scan_gcode_for_operations(filename);
        }

        // Set filament type dropdown to match file metadata
        lv_obj_t* dropdown = lv_obj_find_by_name(detail_view_widget_, "filament_dropdown");
        if (dropdown && !selected_filament_type_.empty()) {
            // Map filament type string to dropdown index
            // Options: PLA(0), PETG(1), ABS(2), TPU(3), Nylon(4), ASA(5), PC(6)
            uint32_t index = 0; // Default to PLA
            if (selected_filament_type_ == "PETG") {
                index = 1;
            } else if (selected_filament_type_ == "ABS") {
                index = 2;
            } else if (selected_filament_type_ == "TPU") {
                index = 3;
            } else if (selected_filament_type_ == "Nylon" || selected_filament_type_ == "NYLON" ||
                       selected_filament_type_ == "PA") {
                index = 4;
            } else if (selected_filament_type_ == "ASA") {
                index = 5;
            } else if (selected_filament_type_ == "PC") {
                index = 6;
            }
            // PLA is index 0 (default) for "PLA" or any unrecognized type
            lv_dropdown_set_selected(dropdown, index);
            spdlog::debug("[{}] Set filament dropdown to {} (index {})", get_name(),
                          selected_filament_type_, index);
        }

        // Use nav system for consistent backdrop and z-order management
        ui_nav_push_overlay(detail_view_widget_);
        lv_subject_set_int(&detail_view_visible_subject_, 1);
    }
}

void PrintSelectPanel::hide_detail_view() {
    if (detail_view_widget_) {
        // Use nav system to properly hide and manage backdrop
        ui_nav_go_back();
        lv_subject_set_int(&detail_view_visible_subject_, 0);
    }
}

void PrintSelectPanel::show_delete_confirmation() {
    // Configure modal
    ui_modal_config_t config = {
        .position = {.use_alignment = true, .alignment = LV_ALIGN_CENTER, .x = 0, .y = 0},
        .backdrop_opa = 180,
        .keyboard = nullptr,
        .persistent = false,
        .on_close = nullptr};

    // Create message with current filename
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf),
             "Are you sure you want to delete '%s'? This action cannot be undone.",
             selected_filename_buffer_);

    const char* attrs[] = {"title", "Delete File?", "message", msg_buf, NULL};

    ui_modal_configure(UI_MODAL_SEVERITY_WARNING, true, "Delete", "Cancel");
    confirmation_dialog_widget_ = ui_modal_show("modal_dialog", &config, attrs);

    if (!confirmation_dialog_widget_) {
        spdlog::error("[{}] Failed to create confirmation dialog", get_name());
        return;
    }

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(confirmation_dialog_widget_, "btn_secondary");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_cancel_delete_static, LV_EVENT_CLICKED, this);
    }

    // Wire up confirm button
    lv_obj_t* confirm_btn = lv_obj_find_by_name(confirmation_dialog_widget_, "btn_primary");
    if (confirm_btn) {
        lv_obj_add_event_cb(confirm_btn, on_confirm_delete_static, LV_EVENT_CLICKED, this);
    }

    spdlog::info("[{}] Delete confirmation dialog shown", get_name());
}

void PrintSelectPanel::set_print_status_panel(lv_obj_t* panel) {
    print_status_panel_widget_ = panel;
    spdlog::debug("[{}] Print status panel reference set", get_name());
}

// ============================================================================
// Internal Methods
// ============================================================================

CardDimensions PrintSelectPanel::calculate_card_dimensions() {
    if (!card_view_container_) {
        spdlog::error("[{}] Cannot calculate dimensions: container is null", get_name());
        return {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
    }

    lv_coord_t container_width = lv_obj_get_content_width(card_view_container_);
    // Read gap from container's XML-defined style (respects design tokens)
    // Note: style_pad_gap in XML sets both pad_row and pad_column; we read pad_column for width
    // calc
    int card_gap = lv_obj_get_style_pad_column(card_view_container_, LV_PART_MAIN);
    spdlog::trace("[{}] Container content width: {}px (MIN={}, MAX={}, GAP={})", get_name(),
                  container_width, CARD_MIN_WIDTH, CARD_MAX_WIDTH, card_gap);

    // Calculate available height from parent panel dimensions
    lv_obj_t* panel_root = lv_obj_get_parent(card_view_container_);
    if (!panel_root) {
        spdlog::error("[{}] Cannot find panel root", get_name());
        return {4, 2, CARD_MIN_WIDTH, CARD_DEFAULT_HEIGHT};
    }

    lv_coord_t panel_height = lv_obj_get_height(panel_root);
    lv_obj_t* top_bar = lv_obj_get_child(panel_root, 0);
    lv_coord_t top_bar_height = top_bar ? lv_obj_get_height(top_bar) : 60;
    lv_coord_t panel_gap = lv_obj_get_style_pad_row(panel_root, LV_PART_MAIN);
    lv_coord_t container_padding = lv_obj_get_style_pad_top(card_view_container_, LV_PART_MAIN) +
                                   lv_obj_get_style_pad_bottom(card_view_container_, LV_PART_MAIN);
    lv_coord_t available_height = panel_height - top_bar_height - container_padding - panel_gap;

    CardDimensions dims;

    // Determine optimal number of rows based on available height
    dims.num_rows = (available_height >= ROW_COUNT_3_MIN_HEIGHT) ? 3 : 2;

    // Calculate card height based on rows
    int total_row_gaps = (dims.num_rows - 1) * card_gap;
    dims.card_height = (available_height - total_row_gaps) / dims.num_rows;

    // Try different column counts
    for (int cols = 10; cols >= 1; cols--) {
        int total_gaps = (cols - 1) * card_gap;
        int card_width = (container_width - total_gaps) / cols;

        if (card_width >= CARD_MIN_WIDTH && card_width <= CARD_MAX_WIDTH) {
            dims.num_columns = cols;
            dims.card_width = card_width;

            spdlog::trace("[{}] Calculated card layout: {} rows x {} columns, card={}x{}",
                          get_name(), dims.num_rows, dims.num_columns, dims.card_width,
                          dims.card_height);
            return dims;
        }
    }

    // Fallback
    dims.num_columns = container_width / (CARD_MIN_WIDTH + card_gap);
    if (dims.num_columns < 1)
        dims.num_columns = 1;
    dims.card_width = CARD_MIN_WIDTH;

    spdlog::warn("[{}] No optimal card layout found, using fallback: {} columns", get_name(),
                 dims.num_columns);
    return dims;
}

void PrintSelectPanel::schedule_view_refresh() {
    // Use lv_async_call to ensure thread-safety (this may be called from WebSocket thread)
    lv_async_call(
        [](void* user_data) {
            auto* self = static_cast<PrintSelectPanel*>(user_data);

            // If a timer is already pending, reset it (debounce)
            if (self->refresh_timer_) {
                lv_timer_reset(self->refresh_timer_);
                return;
            }

            // Create a one-shot timer to refresh views after debounce period
            self->refresh_timer_ = lv_timer_create(
                [](lv_timer_t* timer) {
                    auto* panel = static_cast<PrintSelectPanel*>(lv_timer_get_user_data(timer));
                    panel->refresh_timer_ = nullptr; // Clear before callback (timer auto-deletes)

                    // Safety check: if containers are null, panel is likely being/been destroyed.
                    if (!panel->card_view_container_ && !panel->list_rows_container_) {
                        return;
                    }

                    spdlog::debug("[{}] Debounced metadata refresh - updating visible cards only",
                                  panel->get_name());

                    // Only refresh CONTENT of currently visible cards - don't reset
                    // spacers/positions This prevents flashing when metadata/thumbnails arrive
                    // asynchronously
                    panel->refresh_visible_content();
                },
                REFRESH_DEBOUNCE_MS, self);

            // Make it a one-shot timer
            lv_timer_set_repeat_count(self->refresh_timer_, 1);
        },
        this);
}

void PrintSelectPanel::refresh_visible_content() {
    // Refresh content of currently visible cards without resetting positions
    if (card_view_container_ && !card_pool_.empty() && visible_start_row_ >= 0) {
        CardDimensions dims = calculate_card_dimensions();

        // Re-configure each visible pool card with latest data
        for (size_t i = 0; i < card_pool_.size(); i++) {
            ssize_t file_idx = card_pool_indices_[i];
            if (file_idx >= 0 && static_cast<size_t>(file_idx) < file_list_.size()) {
                configure_card(card_pool_[i], static_cast<size_t>(file_idx), dims);
            }
        }
    }

    // Same for list view
    if (list_rows_container_ && !list_pool_.empty() && visible_list_start_ >= 0) {
        for (size_t i = 0; i < list_pool_.size(); i++) {
            ssize_t file_idx = list_pool_indices_[i];
            if (file_idx >= 0 && static_cast<size_t>(file_idx) < file_list_.size()) {
                configure_list_row(list_pool_[i], static_cast<size_t>(file_idx));
            }
        }
    }
}

void PrintSelectPanel::init_card_pool() {
    if (!card_view_container_ || !card_pool_.empty())
        return;

    spdlog::debug("[{}] init_card_pool() creating {} card widgets", get_name(), CARD_POOL_SIZE);

    // Calculate initial dimensions
    lv_obj_update_layout(card_view_container_);
    CardDimensions dims = calculate_card_dimensions();
    cards_per_row_ = dims.num_columns;

    // Reserve pool storage
    card_pool_.reserve(CARD_POOL_SIZE);
    card_pool_indices_.resize(CARD_POOL_SIZE, -1);

    // Create pool cards (initially hidden)
    for (int i = 0; i < CARD_POOL_SIZE; i++) {
        const char* attrs[] = {"thumbnail_src",
                               DEFAULT_PLACEHOLDER_THUMB,
                               "filename",
                               "",
                               "print_time",
                               "",
                               "filament_weight",
                               "",
                               NULL};

        lv_obj_t* card =
            static_cast<lv_obj_t*>(lv_xml_create(card_view_container_, CARD_COMPONENT_NAME, attrs));

        if (card) {
            lv_obj_set_width(card, dims.card_width);
            lv_obj_set_height(card, dims.card_height);
            lv_obj_set_style_flex_grow(card, 0, LV_PART_MAIN);
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);

            // Attach click handler ONCE at pool creation (not on every scroll!)
            // The handler uses lv_obj_get_user_data() to get the file index,
            // which is updated in configure_card() when the card is recycled.
            lv_obj_add_event_cb(card, on_file_clicked_static, LV_EVENT_CLICKED, this);

            card_pool_.push_back(card);
        }
    }

    spdlog::debug("[{}] Card pool initialized with {} cards", get_name(), card_pool_.size());
}

void PrintSelectPanel::configure_card(lv_obj_t* card, size_t index, const CardDimensions& dims) {
    if (!card || index >= file_list_.size())
        return;

    const auto& file = file_list_[index];

    // Update display name
    std::string display_name =
        file.is_dir ? file.filename + "/" : strip_gcode_extension(file.filename);

    // Update labels
    lv_obj_t* filename_label = lv_obj_find_by_name(card, "filename_label");
    if (filename_label) {
        lv_label_set_text(filename_label, display_name.c_str());
    }

    lv_obj_t* time_label = lv_obj_find_by_name(card, "time_label");
    if (time_label) {
        lv_label_set_text(time_label, file.print_time_str.c_str());
    }

    lv_obj_t* filament_label = lv_obj_find_by_name(card, "filament_label");
    if (filament_label) {
        lv_label_set_text(filament_label, file.filament_str.c_str());
    }

    // Update thumbnail
    lv_obj_t* thumb_img = lv_obj_find_by_name(card, "thumbnail");
    if (thumb_img && !file.thumbnail_path.empty()) {
        lv_image_set_src(thumb_img, file.thumbnail_path.c_str());

        // Directory styling - use amber/orange tint for folder icons
        if (file.is_dir) {
            lv_obj_set_style_image_recolor(thumb_img, ui_theme_get_color("warning_color"), 0);
            lv_obj_set_style_image_recolor_opa(thumb_img, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_image_recolor_opa(thumb_img, LV_OPA_TRANSP, 0);
        }
    }

    // Hide/show metadata row for directories
    lv_obj_t* metadata_row = lv_obj_find_by_name(card, "metadata_row");
    if (metadata_row) {
        if (file.is_dir) {
            lv_obj_add_flag(metadata_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(metadata_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Adjust overlay heights for directories
    if (file.is_dir) {
        lv_obj_t* metadata_clip = lv_obj_find_by_name(card, "metadata_clip");
        lv_obj_t* metadata_overlay = lv_obj_find_by_name(card, "metadata_overlay");
        if (metadata_clip && metadata_overlay) {
            lv_obj_set_height(metadata_clip, 40);
            lv_obj_set_height(metadata_overlay, 48);
        }
    }

    // Update card sizing
    lv_obj_set_width(card, dims.card_width);
    lv_obj_set_height(card, dims.card_height);

    // Store file index for click handler
    lv_obj_set_user_data(card, reinterpret_cast<void*>(index));

    // Show the card
    lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
}

void PrintSelectPanel::update_visible_cards() {
    if (!card_view_container_ || card_pool_.empty() || file_list_.empty())
        return;

    // Get scroll position and container dimensions
    int32_t scroll_y = lv_obj_get_scroll_y(card_view_container_);
    int32_t viewport_height = lv_obj_get_height(card_view_container_);

    CardDimensions dims = calculate_card_dimensions();
    cards_per_row_ = dims.num_columns;

    int card_gap = lv_obj_get_style_pad_row(card_view_container_, LV_PART_MAIN);
    int row_height = dims.card_height + card_gap;
    int total_rows = (static_cast<int>(file_list_.size()) + cards_per_row_ - 1) / cards_per_row_;

    // Calculate visible row range (with buffer)
    int first_visible_row = std::max(0, static_cast<int>(scroll_y / row_height) - CARD_BUFFER_ROWS);
    int last_visible_row =
        std::min(total_rows, static_cast<int>((scroll_y + viewport_height) / row_height) + 1 +
                                 CARD_BUFFER_ROWS);

    // Skip update if visible range hasn't changed
    if (first_visible_row == visible_start_row_ && last_visible_row == visible_end_row_) {
        return;
    }

    // Calculate file index range
    int first_visible_idx = first_visible_row * cards_per_row_;
    int last_visible_idx =
        std::min(static_cast<int>(file_list_.size()), last_visible_row * cards_per_row_);

    spdlog::trace("[{}] Scroll: {} viewport: {} rows: {}-{} indices: {}-{}", get_name(), scroll_y,
                  viewport_height, first_visible_row, last_visible_row, first_visible_idx,
                  last_visible_idx);

    // Update leading spacer height to push cards to correct scroll position
    int leading_height = first_visible_row * row_height;
    if (card_leading_spacer_) {
        lv_obj_set_height(card_leading_spacer_, leading_height);
        lv_obj_move_to_index(card_leading_spacer_, 0);
    }

    // Update trailing spacer to create scroll range for remaining rows
    int trailing_height = (total_rows - last_visible_row) * row_height;
    if (card_trailing_spacer_) {
        lv_obj_set_height(card_trailing_spacer_, std::max(0, trailing_height));
    }

    // Mark all pool cards as available
    std::fill(card_pool_indices_.begin(), card_pool_indices_.end(), static_cast<ssize_t>(-1));

    // Assign pool cards to visible indices
    // Note: Click handler was attached once in init_card_pool(), not here.
    // The handler reads file index from user_data, which configure_card() updates.
    size_t pool_idx = 0;
    for (int file_idx = first_visible_idx;
         file_idx < last_visible_idx && pool_idx < card_pool_.size(); file_idx++, pool_idx++) {
        lv_obj_t* card = card_pool_[pool_idx];
        configure_card(card, static_cast<size_t>(file_idx), dims);
        card_pool_indices_[pool_idx] = file_idx;

        // Move card after spacer in container order
        lv_obj_move_to_index(card, static_cast<int>(pool_idx) + 1);
    }

    // Hide unused pool cards
    for (; pool_idx < card_pool_.size(); pool_idx++) {
        lv_obj_add_flag(card_pool_[pool_idx], LV_OBJ_FLAG_HIDDEN);
        card_pool_indices_[pool_idx] = -1;
    }

    visible_start_row_ = first_visible_row;
    visible_end_row_ = last_visible_row;
}

void PrintSelectPanel::handle_scroll(lv_obj_t* container) {
    if (container == card_view_container_) {
        update_visible_cards();
        // Lazy-load metadata for newly visible cards
        if (visible_start_row_ >= 0 && visible_end_row_ >= 0) {
            size_t start =
                static_cast<size_t>(visible_start_row_) * static_cast<size_t>(cards_per_row_);
            size_t end =
                static_cast<size_t>(visible_end_row_) * static_cast<size_t>(cards_per_row_);
            fetch_metadata_range(start, end);
        }
    } else if (container == list_rows_container_) {
        update_visible_list_rows();
        // Lazy-load metadata for newly visible list items
        if (visible_list_start_ >= 0 && visible_list_end_ >= 0) {
            fetch_metadata_range(static_cast<size_t>(visible_list_start_),
                                 static_cast<size_t>(visible_list_end_));
        }
    }
}

void PrintSelectPanel::populate_card_view() {
    if (!card_view_container_)
        return;

    spdlog::debug("[{}] populate_card_view() with {} files (virtualized)", get_name(),
                  file_list_.size());

    // Initialize pool on first call
    if (card_pool_.empty()) {
        init_card_pool();
    }

    // Calculate optimal card dimensions (reads gap from container's XML style)
    CardDimensions dims = calculate_card_dimensions();
    cards_per_row_ = dims.num_columns;

    // Calculate total rows for debug logging
    int total_rows = (static_cast<int>(file_list_.size()) + cards_per_row_ - 1) / cards_per_row_;

    // Create leading spacer if needed - this spacer fills the space before visible cards
    if (!card_leading_spacer_) {
        card_leading_spacer_ = lv_obj_create(card_view_container_);
        lv_obj_remove_style_all(card_leading_spacer_);
        lv_obj_remove_flag(card_leading_spacer_, LV_OBJ_FLAG_CLICKABLE);
        // Spacer participates in flex layout - takes full row width to force wrap
        lv_obj_set_width(card_leading_spacer_, lv_pct(100));
        lv_obj_set_height(card_leading_spacer_, 0);
    }

    // Create trailing spacer if needed - enables scrolling to end of file list
    if (!card_trailing_spacer_) {
        card_trailing_spacer_ = lv_obj_create(card_view_container_);
        lv_obj_remove_style_all(card_trailing_spacer_);
        lv_obj_remove_flag(card_trailing_spacer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(card_trailing_spacer_, lv_pct(100));
        lv_obj_set_height(card_trailing_spacer_, 0);
    }

    // Reset visible range tracking to force full update
    visible_start_row_ = -1;
    visible_end_row_ = -1;

    // Reset scroll position when file list changes
    lv_obj_scroll_to_y(card_view_container_, 0, LV_ANIM_OFF);

    // Update which cards are visible
    update_visible_cards();

    spdlog::debug("[{}] Card view virtualized: {} files, {} rows, pool size {}", get_name(),
                  file_list_.size(), total_rows, card_pool_.size());
}

void PrintSelectPanel::init_list_pool() {
    if (!list_rows_container_ || !list_pool_.empty())
        return;

    spdlog::debug("[{}] init_list_pool() creating {} row widgets", get_name(), LIST_POOL_SIZE);

    // Reserve pool storage
    list_pool_.reserve(LIST_POOL_SIZE);
    list_pool_indices_.resize(LIST_POOL_SIZE, -1);

    // Create pool rows (initially hidden)
    for (int i = 0; i < LIST_POOL_SIZE; i++) {
        const char* attrs[] = {"filename", "",           "file_size", "",  "modified_date",
                               "",         "print_time", "",          NULL};

        lv_obj_t* row = static_cast<lv_obj_t*>(
            lv_xml_create(list_rows_container_, "print_file_list_row", attrs));

        if (row) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

            // Attach click handler ONCE at pool creation (not on every scroll!)
            // The handler uses lv_obj_get_user_data() to get the file index,
            // which is updated in configure_list_row() when the row is recycled.
            lv_obj_add_event_cb(row, on_file_clicked_static, LV_EVENT_CLICKED, this);

            list_pool_.push_back(row);
        }
    }

    spdlog::debug("[{}] List pool initialized with {} rows", get_name(), list_pool_.size());
}

void PrintSelectPanel::configure_list_row(lv_obj_t* row, size_t index) {
    if (!row || index >= file_list_.size())
        return;

    const auto& file = file_list_[index];

    // Update display name
    std::string display_name =
        file.is_dir ? file.filename + "/" : strip_gcode_extension(file.filename);

    // Update labels by finding them in the row (names match XML component)
    lv_obj_t* filename_label = lv_obj_find_by_name(row, "row_filename");
    if (filename_label) {
        lv_label_set_text(filename_label, display_name.c_str());
    }

    lv_obj_t* size_label = lv_obj_find_by_name(row, "row_size");
    if (size_label) {
        lv_label_set_text(size_label, file.size_str.c_str());
    }

    lv_obj_t* modified_label = lv_obj_find_by_name(row, "row_modified");
    if (modified_label) {
        lv_label_set_text(modified_label, file.modified_str.c_str());
    }

    lv_obj_t* time_label = lv_obj_find_by_name(row, "row_print_time");
    if (time_label) {
        lv_label_set_text(time_label, file.print_time_str.c_str());
    }

    // Store file index for click handler
    lv_obj_set_user_data(row, reinterpret_cast<void*>(index));

    // Show the row
    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
}

void PrintSelectPanel::update_visible_list_rows() {
    if (!list_rows_container_ || list_pool_.empty() || file_list_.empty())
        return;

    // Get scroll position and container dimensions
    int32_t scroll_y = lv_obj_get_scroll_y(list_rows_container_);
    int32_t viewport_height = lv_obj_get_height(list_rows_container_);

    int total_rows = static_cast<int>(file_list_.size());

    // Calculate row stride from actual widget height + container gap (respects XML styling)
    int row_height = list_pool_.empty() ? 44 : lv_obj_get_height(list_pool_[0]);
    int row_gap = lv_obj_get_style_pad_row(list_rows_container_, LV_PART_MAIN);
    int row_stride = row_height + row_gap;

    // Calculate visible row range (with buffer)
    int first_visible = std::max(0, static_cast<int>(scroll_y / row_stride) - 2);
    int last_visible =
        std::min(total_rows, static_cast<int>((scroll_y + viewport_height) / row_stride) + 3);

    // Skip update if visible range hasn't changed
    if (first_visible == visible_list_start_ && last_visible == visible_list_end_) {
        return;
    }

    spdlog::trace("[{}] List scroll: {} viewport: {} visible: {}-{}", get_name(), scroll_y,
                  viewport_height, first_visible, last_visible);

    // Update leading spacer height
    int leading_height = first_visible * row_stride;
    if (list_leading_spacer_) {
        lv_obj_set_height(list_leading_spacer_, leading_height);
        lv_obj_move_to_index(list_leading_spacer_, 0);
    }

    // Update trailing spacer height
    int trailing_height = (total_rows - last_visible) * row_stride;
    if (list_trailing_spacer_) {
        lv_obj_set_height(list_trailing_spacer_, std::max(0, trailing_height));
    }

    // Mark all pool rows as available
    std::fill(list_pool_indices_.begin(), list_pool_indices_.end(), static_cast<ssize_t>(-1));

    // Assign pool rows to visible indices
    // Note: Click handler was attached once in init_list_pool(), not here.
    // The handler reads file index from user_data, which configure_list_row() updates.
    size_t pool_idx = 0;
    for (int file_idx = first_visible; file_idx < last_visible && pool_idx < list_pool_.size();
         file_idx++, pool_idx++) {
        lv_obj_t* row = list_pool_[pool_idx];
        configure_list_row(row, static_cast<size_t>(file_idx));
        list_pool_indices_[pool_idx] = file_idx;

        // Position row after leading spacer
        lv_obj_move_to_index(row, static_cast<int>(pool_idx) + 1);
    }

    // Hide unused pool rows
    for (; pool_idx < list_pool_.size(); pool_idx++) {
        lv_obj_add_flag(list_pool_[pool_idx], LV_OBJ_FLAG_HIDDEN);
        list_pool_indices_[pool_idx] = -1;
    }

    visible_list_start_ = first_visible;
    visible_list_end_ = last_visible;
}

void PrintSelectPanel::populate_list_view() {
    if (!list_rows_container_)
        return;

    spdlog::debug("[{}] populate_list_view() with {} files (virtualized)", get_name(),
                  file_list_.size());

    // Initialize pool on first call
    if (list_pool_.empty()) {
        init_list_pool();
    }

    // Create leading spacer if needed
    if (!list_leading_spacer_) {
        list_leading_spacer_ = lv_obj_create(list_rows_container_);
        lv_obj_remove_style_all(list_leading_spacer_);
        lv_obj_remove_flag(list_leading_spacer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(list_leading_spacer_, lv_pct(100));
        lv_obj_set_height(list_leading_spacer_, 0);
    }

    // Create trailing spacer if needed
    if (!list_trailing_spacer_) {
        list_trailing_spacer_ = lv_obj_create(list_rows_container_);
        lv_obj_remove_style_all(list_trailing_spacer_);
        lv_obj_remove_flag(list_trailing_spacer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(list_trailing_spacer_, lv_pct(100));
        lv_obj_set_height(list_trailing_spacer_, 0);
    }

    // Reset visible range tracking to force full update
    visible_list_start_ = -1;
    visible_list_end_ = -1;

    // Reset scroll position when file list changes
    lv_obj_scroll_to_y(list_rows_container_, 0, LV_ANIM_OFF);

    // Update which rows are visible
    update_visible_list_rows();

    spdlog::debug("[{}] List view virtualized: {} files, pool size {}", get_name(),
                  file_list_.size(), list_pool_.size());
}

void PrintSelectPanel::apply_sort() {
    auto sort_column = current_sort_column_;
    auto sort_direction = current_sort_direction_;

    std::sort(file_list_.begin(), file_list_.end(),
              [sort_column, sort_direction](const PrintFileData& a, const PrintFileData& b) {
                  bool result = false;

                  switch (sort_column) {
                  case PrintSelectSortColumn::FILENAME:
                      result = a.filename < b.filename;
                      break;
                  case PrintSelectSortColumn::SIZE:
                      result = a.file_size_bytes < b.file_size_bytes;
                      break;
                  case PrintSelectSortColumn::MODIFIED:
                      result = a.modified_timestamp < b.modified_timestamp;
                      break;
                  case PrintSelectSortColumn::PRINT_TIME:
                      result = a.print_time_minutes < b.print_time_minutes;
                      break;
                  case PrintSelectSortColumn::FILAMENT:
                      result = a.filament_grams < b.filament_grams;
                      break;
                  }

                  if (sort_direction == PrintSelectSortDirection::DESCENDING) {
                      result = !result;
                  }

                  return result;
              });
}

void PrintSelectPanel::update_empty_state() {
    if (!empty_state_container_)
        return;

    bool is_empty = file_list_.empty();

    if (is_empty) {
        lv_obj_remove_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_state_container_, LV_OBJ_FLAG_HIDDEN);

        if (current_view_mode_ == PrintSelectViewMode::CARD) {
            lv_obj_remove_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(card_view_container_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(list_view_container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void PrintSelectPanel::update_print_button_state() {
    // Update the can_print subject based on current print state
    // XML binding automatically disables button when value is 0
    bool can_print = printer_state_.can_start_new_print();
    int new_value = can_print ? 1 : 0;

    // Only update if value changed (avoid unnecessary subject notifications)
    if (lv_subject_get_int(&can_print_subject_) != new_value) {
        lv_subject_set_int(&can_print_subject_, new_value);
        spdlog::debug("[{}] Print button {} (can_start_new_print={})", get_name(),
                      can_print ? "enabled" : "disabled", can_print);
    }
}

void PrintSelectPanel::update_sort_indicators() {
    const char* header_names[] = {"header_filename", "header_size", "header_modified",
                                  "header_print_time"};
    PrintSelectSortColumn columns[] = {PrintSelectSortColumn::FILENAME, PrintSelectSortColumn::SIZE,
                                       PrintSelectSortColumn::MODIFIED,
                                       PrintSelectSortColumn::PRINT_TIME};

    for (int i = 0; i < 4; i++) {
        char icon_up_name[64];
        char icon_down_name[64];
        snprintf(icon_up_name, sizeof(icon_up_name), "%s_icon_up", header_names[i]);
        snprintf(icon_down_name, sizeof(icon_down_name), "%s_icon_down", header_names[i]);

        lv_obj_t* icon_up = lv_obj_find_by_name(panel_, icon_up_name);
        lv_obj_t* icon_down = lv_obj_find_by_name(panel_, icon_down_name);

        if (icon_up && icon_down) {
            if (columns[i] == current_sort_column_) {
                if (current_sort_direction_ == PrintSelectSortDirection::ASCENDING) {
                    lv_obj_remove_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                lv_obj_add_flag(icon_up, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(icon_down, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void PrintSelectPanel::create_detail_view() {
    if (!parent_screen_) {
        spdlog::error("[{}] Cannot create detail view: parent_screen is null", get_name());
        return;
    }

    if (detail_view_widget_) {
        spdlog::error("[{}] Detail view already exists", get_name());
        return;
    }

    detail_view_widget_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_file_detail", nullptr));

    if (!detail_view_widget_) {
        LOG_ERROR_INTERNAL("[{}] Failed to create detail view from XML", get_name());
        NOTIFY_ERROR("Failed to load file details");
        return;
    }

    // Set width to fill space after nav bar
    ui_set_overlay_width(detail_view_widget_, parent_screen_);

    // Set responsive padding for content area
    lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget_, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_));
        lv_obj_set_style_pad_all(content_container, padding, 0);
    }

    lv_obj_add_flag(detail_view_widget_, LV_OBJ_FLAG_HIDDEN);

    // Wire up back button
    lv_obj_t* back_button = lv_obj_find_by_name(detail_view_widget_, "back_button");
    if (back_button) {
        lv_obj_add_event_cb(back_button, on_back_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Wire up delete button
    lv_obj_t* delete_button = lv_obj_find_by_name(detail_view_widget_, "delete_button");
    if (delete_button) {
        lv_obj_add_event_cb(delete_button, on_delete_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Wire up print button and store reference for enable/disable
    print_button_ = lv_obj_find_by_name(detail_view_widget_, "print_button");
    if (print_button_) {
        lv_obj_add_event_cb(print_button_, on_print_button_clicked_static, LV_EVENT_CLICKED, this);
    }

    // Click backdrop to close
    lv_obj_add_event_cb(detail_view_widget_, on_detail_backdrop_clicked_static, LV_EVENT_CLICKED,
                        this);

    // Look up pre-print option checkboxes
    bed_leveling_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "bed_leveling_checkbox");
    qgl_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "qgl_checkbox");
    z_tilt_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "z_tilt_checkbox");
    nozzle_clean_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "nozzle_clean_checkbox");
    timelapse_checkbox_ = lv_obj_find_by_name(detail_view_widget_, "timelapse_checkbox");

    spdlog::debug("[{}] Detail view created", get_name());
}

void PrintSelectPanel::hide_delete_confirmation() {
    if (confirmation_dialog_widget_) {
        ui_modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }
}

void PrintSelectPanel::handle_resize() {
    if (!panel_initialized_)
        return;

    spdlog::info("[{}] Handling resize event", get_name());

    if (current_view_mode_ == PrintSelectViewMode::CARD && card_view_container_) {
        populate_card_view();
    }

    if (detail_view_widget_ && parent_screen_) {
        lv_obj_t* content_container = lv_obj_find_by_name(detail_view_widget_, "content_container");
        if (content_container) {
            lv_coord_t padding = ui_get_header_content_padding(lv_obj_get_height(parent_screen_));
            lv_obj_set_style_pad_all(content_container, padding, 0);
        }
    }
}

void PrintSelectPanel::attach_card_click_handler(lv_obj_t* card, size_t file_index) {
    // Store file index in widget user_data
    lv_obj_set_user_data(card, reinterpret_cast<void*>(file_index));
    lv_obj_add_event_cb(card, on_file_clicked_static, LV_EVENT_CLICKED, this);
}

void PrintSelectPanel::attach_row_click_handler(lv_obj_t* row, size_t file_index) {
    lv_obj_set_user_data(row, reinterpret_cast<void*>(file_index));
    lv_obj_add_event_cb(row, on_file_clicked_static, LV_EVENT_CLICKED, this);
}

void PrintSelectPanel::handle_file_click(size_t file_index) {
    if (file_index >= file_list_.size()) {
        spdlog::error("[{}] Invalid file index: {}", get_name(), file_index);
        return;
    }

    const auto& file = file_list_[file_index];

    if (file.is_dir) {
        if (file.filename == "..") {
            // Parent directory - navigate up
            navigate_up();
        } else {
            // Directory clicked - navigate into it
            navigate_to_directory(file.filename);
        }
    } else {
        // File clicked - show detail view
        set_selected_file(file.filename.c_str(), file.thumbnail_path.c_str(),
                          file.print_time_str.c_str(), file.filament_str.c_str(),
                          file.layer_count_str.c_str());
        selected_filament_type_ = file.filament_type;
        show_detail_view();
    }
}

void PrintSelectPanel::start_print() {
    // Stage 9: Concurrent Print Prevention
    // Check if a print is already active before allowing a new one to start
    if (!printer_state_.can_start_new_print()) {
        PrintJobState current_state = printer_state_.get_print_job_state();
        const char* state_str = print_job_state_to_string(current_state);
        NOTIFY_ERROR("Cannot start print: printer is {}", state_str);
        spdlog::warn("[{}] Attempted to start print while printer is in {} state", get_name(),
                     state_str);
        return;
    }

    std::string filename_to_print(selected_filename_buffer_);
    auto* self = this;

    // Helper to check if checkbox is visible and checked
    auto is_option_enabled = [](lv_obj_t* checkbox) -> bool {
        if (!checkbox)
            return false;
        // Only consider it enabled if visible (not hidden) and checked
        bool is_visible = !lv_obj_has_flag(checkbox, LV_OBJ_FLAG_HIDDEN);
        bool is_checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
        return is_visible && is_checked;
    };

    // Read pre-print option states
    bool do_bed_leveling = is_option_enabled(bed_leveling_checkbox_);
    bool do_qgl = is_option_enabled(qgl_checkbox_);
    bool do_z_tilt = is_option_enabled(z_tilt_checkbox_);
    bool do_nozzle_clean = is_option_enabled(nozzle_clean_checkbox_);
    bool do_timelapse = is_option_enabled(timelapse_checkbox_);

    bool has_pre_print_ops = do_bed_leveling || do_qgl || do_z_tilt || do_nozzle_clean;

    spdlog::info(
        "[{}] Starting print: {} (pre-print: mesh={}, qgl={}, z_tilt={}, clean={}, timelapse={})",
        get_name(), filename_to_print, do_bed_leveling, do_qgl, do_z_tilt, do_nozzle_clean,
        do_timelapse);

    // Enable timelapse recording if requested (Moonraker-Timelapse plugin)
    if (do_timelapse && api_) {
        api_->set_timelapse_enabled(
            true, []() { spdlog::info("[PrintSelect] Timelapse enabled for this print"); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintSelect] Failed to enable timelapse: {}", err.message);
            });
    }

    if (api_) {
        // Check if user disabled operations that are embedded in the G-code file.
        // If so, we need to modify the file before printing to comment out those operations.
        std::vector<helix::gcode::OperationType> ops_to_disable = collect_ops_to_disable();

        if (!ops_to_disable.empty()) {
            spdlog::info("[{}] User disabled {} embedded operations - modifying G-code", get_name(),
                         ops_to_disable.size());
            modify_and_print(filename_to_print, ops_to_disable);
            return; // modify_and_print handles everything including navigation
        }
        if (has_pre_print_ops) {
            // Create command sequencer for pre-print operations
            pre_print_sequencer_ = std::make_unique<helix::gcode::CommandSequencer>(
                api_->get_client(), *api_, printer_state_);

            // Always home first if doing any pre-print operations
            pre_print_sequencer_->add_operation(helix::gcode::OperationType::HOMING, {}, "Homing");

            // Add selected operations in logical order
            if (do_qgl) {
                pre_print_sequencer_->add_operation(helix::gcode::OperationType::QGL, {},
                                                    "Quad Gantry Level");
            }
            if (do_z_tilt) {
                pre_print_sequencer_->add_operation(helix::gcode::OperationType::Z_TILT, {},
                                                    "Z-Tilt Adjust");
            }
            if (do_bed_leveling) {
                pre_print_sequencer_->add_operation(helix::gcode::OperationType::BED_LEVELING, {},
                                                    "Bed Mesh Calibration");
            }
            if (do_nozzle_clean) {
                pre_print_sequencer_->add_operation(helix::gcode::OperationType::NOZZLE_CLEAN, {},
                                                    "Clean Nozzle");
            }

            // Add the actual print start as the final operation
            helix::gcode::OperationParams print_params;
            print_params.filename = filename_to_print;
            pre_print_sequencer_->add_operation(helix::gcode::OperationType::START_PRINT,
                                                print_params, "Starting Print");

            // Show the print status panel immediately in "Preparing" state
            if (print_status_panel_widget_) {
                hide_detail_view();
                ui_nav_push_overlay(print_status_panel_widget_);

                // Initialize the preparing state
                auto& status_panel = get_global_print_status_panel();
                status_panel.set_preparing("Starting...", 0,
                                           static_cast<int>(pre_print_sequencer_->queue_size()));
            }

            // Start the sequence
            pre_print_sequencer_->start(
                // Progress callback - update the Preparing UI
                [self](const std::string& op_name, int step, int total, float progress) {
                    spdlog::info("[{}] Pre-print progress: {} ({}/{}, {:.0f}%)", self->get_name(),
                                 op_name, step, total, progress * 100.0f);

                    // Update PrintStatusPanel's preparing state
                    auto& status_panel = get_global_print_status_panel();
                    status_panel.set_preparing(op_name, step, total);
                    status_panel.set_preparing_progress(progress);
                },
                // Completion callback
                [self](bool success, const std::string& error) {
                    auto& status_panel = get_global_print_status_panel();

                    if (success) {
                        spdlog::info("[{}] Pre-print sequence complete, print started",
                                     self->get_name());
                        // Transition from Preparing → Printing state
                        status_panel.end_preparing(true);
                    } else {
                        NOTIFY_ERROR("Pre-print failed: {}", error);
                        LOG_ERROR_INTERNAL("[{}] Pre-print sequence failed: {}", self->get_name(),
                                           error);
                        // Transition from Preparing → Idle state
                        status_panel.end_preparing(false);
                    }
                    // Clean up sequencer
                    self->pre_print_sequencer_.reset();
                });
        } else {
            // No pre-print operations - start print directly
            api_->start_print(
                filename_to_print,
                // Success callback
                [self]() {
                    spdlog::info("[{}] Print started successfully", self->get_name());

                    if (self->print_status_panel_widget_) {
                        self->hide_detail_view();
                        ui_nav_push_overlay(self->print_status_panel_widget_);
                    } else {
                        spdlog::error("[{}] Print status panel not set", self->get_name());
                    }
                },
                // Error callback
                [self, filename_to_print](const MoonrakerError& error) {
                    NOTIFY_ERROR("Failed to start print: {}", error.message);
                    LOG_ERROR_INTERNAL("[{}] Print start failed for {}: {} ({})", self->get_name(),
                                       filename_to_print, error.message, error.get_type_string());
                });
        }
    } else {
        // Cannot start print without API connection
        spdlog::error("[{}] Cannot start print - not connected to printer", get_name());
        NOTIFY_ERROR("Cannot start print: not connected to printer");
    }
}

void PrintSelectPanel::delete_file() {
    std::string filename_to_delete(selected_filename_buffer_);
    auto* self = this;

    if (api_) {
        spdlog::info("[{}] Deleting file: {}", get_name(), filename_to_delete);

        api_->delete_file(
            filename_to_delete,
            // Success callback
            [self]() {
                spdlog::info("[{}] File deleted successfully", self->get_name());
                self->hide_delete_confirmation();
                self->hide_detail_view();
                self->refresh_files();
            },
            // Error callback
            [self](const MoonrakerError& error) {
                NOTIFY_ERROR("Failed to delete file");
                LOG_ERROR_INTERNAL("[{}] File delete error: {} ({})", self->get_name(),
                                   error.message, error.get_type_string());
                self->hide_delete_confirmation();
            });
    } else {
        NOTIFY_WARNING("Cannot delete file: printer not connected");
        hide_delete_confirmation();
    }
}

// ============================================================================
// Static Callbacks (trampolines)
// ============================================================================

void PrintSelectPanel::on_scroll_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        self->handle_scroll(target);
    }
}

void PrintSelectPanel::on_view_toggle_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->toggle_view();
    }
}

void PrintSelectPanel::on_header_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        // Recover column from widget's user_data
        auto column = static_cast<PrintSelectSortColumn>(
            reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
        self->sort_by(column);
    }
}

void PrintSelectPanel::on_file_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        size_t file_index = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
        self->handle_file_click(file_index);
    }
}

void PrintSelectPanel::on_back_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_detail_view();
    }
}

void PrintSelectPanel::on_delete_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->show_delete_confirmation();
    }
}

void PrintSelectPanel::on_print_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->start_print();
    }
}

void PrintSelectPanel::on_detail_backdrop_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* current_target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    // Only close if clicking the backdrop itself, not child widgets
    if (self && target == current_target) {
        self->hide_detail_view();
    }
}

void PrintSelectPanel::on_confirm_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->delete_file();
    }
}

void PrintSelectPanel::on_cancel_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
    }
}

bool PrintSelectPanel::select_file_by_name(const std::string& filename) {
    // Search for the file in the current file list
    for (size_t i = 0; i < file_list_.size(); ++i) {
        const auto& file = file_list_[i];
        if (!file.is_dir && file.filename == filename) {
            // Found it - simulate a file click
            spdlog::info("[{}] Programmatically selecting file: {}", get_name(), filename);
            handle_file_click(i);
            return true;
        }
    }

    spdlog::warn("[{}] File not found for selection: {}", get_name(), filename);
    return false;
}

void PrintSelectPanel::set_pending_file_selection(const std::string& filename) {
    pending_file_selection_ = filename;
    spdlog::info("[{}] Set pending file selection: '{}'", get_name(), filename);
}

// ============================================================================
// G-code Operations Integration (Stage 7)
// ============================================================================

void PrintSelectPanel::scan_gcode_for_operations(const std::string& filename) {
    // Skip if already cached for this file
    if (cached_scan_filename_ == filename && cached_scan_result_.has_value()) {
        spdlog::debug("[{}] Using cached scan result for {}", get_name(), filename);
        return;
    }

    if (!api_) {
        spdlog::warn("[{}] Cannot scan G-code - no API connection", get_name());
        return;
    }

    // Build path for download
    std::string file_path = current_path_.empty() ? filename : current_path_ + "/" + filename;

    spdlog::info("[{}] Scanning G-code for embedded operations: {}", get_name(), file_path);

    auto* self = this;
    api_->download_file(
        "gcodes", file_path,
        // Success: parse content and cache result
        [self, filename](const std::string& content) {
            helix::gcode::GCodeOpsDetector detector;
            self->cached_scan_result_ = detector.scan_content(content);
            self->cached_scan_filename_ = filename;

            if (self->cached_scan_result_->operations.empty()) {
                spdlog::debug("[{}] No embedded operations found in {}", self->get_name(),
                              filename);
            } else {
                spdlog::info("[{}] Found {} embedded operations in {}:", self->get_name(),
                             self->cached_scan_result_->operations.size(), filename);
                for (const auto& op : self->cached_scan_result_->operations) {
                    spdlog::info("[{}]   - {} at line {} ({})", self->get_name(), op.display_name(),
                                 op.line_number, op.raw_line.substr(0, 50));
                }
            }
        },
        // Error: just log, don't block the UI
        [self, filename](const MoonrakerError& error) {
            spdlog::warn("[{}] Failed to scan G-code {}: {}", self->get_name(), filename,
                         error.message);
            self->cached_scan_result_.reset();
            self->cached_scan_filename_.clear();
        });
}

std::vector<helix::gcode::OperationType> PrintSelectPanel::collect_ops_to_disable() const {
    std::vector<helix::gcode::OperationType> ops_to_disable;

    if (!cached_scan_result_.has_value()) {
        return ops_to_disable; // No scan result, nothing to disable
    }

    // Helper to check if checkbox is visible and UNCHECKED
    auto is_option_disabled = [](lv_obj_t* checkbox) -> bool {
        if (!checkbox)
            return false;
        bool is_visible = !lv_obj_has_flag(checkbox, LV_OBJ_FLAG_HIDDEN);
        bool is_checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
        return is_visible && !is_checked; // Visible but NOT checked = disabled
    };

    // Check each operation type: if file has it embedded AND user disabled it
    if (is_option_disabled(bed_leveling_checkbox_) &&
        cached_scan_result_->has_operation(helix::gcode::OperationType::BED_LEVELING)) {
        ops_to_disable.push_back(helix::gcode::OperationType::BED_LEVELING);
        spdlog::debug("[{}] User disabled bed leveling, file has it embedded", get_name());
    }

    if (is_option_disabled(qgl_checkbox_) &&
        cached_scan_result_->has_operation(helix::gcode::OperationType::QGL)) {
        ops_to_disable.push_back(helix::gcode::OperationType::QGL);
        spdlog::debug("[{}] User disabled QGL, file has it embedded", get_name());
    }

    if (is_option_disabled(z_tilt_checkbox_) &&
        cached_scan_result_->has_operation(helix::gcode::OperationType::Z_TILT)) {
        ops_to_disable.push_back(helix::gcode::OperationType::Z_TILT);
        spdlog::debug("[{}] User disabled Z-tilt, file has it embedded", get_name());
    }

    if (is_option_disabled(nozzle_clean_checkbox_) &&
        cached_scan_result_->has_operation(helix::gcode::OperationType::NOZZLE_CLEAN)) {
        ops_to_disable.push_back(helix::gcode::OperationType::NOZZLE_CLEAN);
        spdlog::debug("[{}] User disabled nozzle clean, file has it embedded", get_name());
    }

    return ops_to_disable;
}

void PrintSelectPanel::modify_and_print(
    const std::string& original_filename,
    const std::vector<helix::gcode::OperationType>& ops_to_disable) {
    if (!api_) {
        NOTIFY_ERROR("Cannot start print - not connected to printer");
        return;
    }

    if (!cached_scan_result_.has_value()) {
        spdlog::error("[{}] modify_and_print called without scan result", get_name());
        NOTIFY_ERROR("Internal error: no scan result");
        return;
    }

    // Build path for download
    std::string file_path =
        current_path_.empty() ? original_filename : current_path_ + "/" + original_filename;

    spdlog::info("[{}] Modifying G-code to disable {} operations", get_name(),
                 ops_to_disable.size());

    auto* self = this;

    // Step 1: Download the original file
    api_->download_file(
        "gcodes", file_path,
        // Success: modify and either use plugin or legacy flow
        [self, original_filename, file_path, ops_to_disable](const std::string& content) {
            // Step 2: Apply modifications
            helix::gcode::GCodeFileModifier modifier;
            modifier.disable_operations(*self->cached_scan_result_, ops_to_disable);

            std::string modified_content = modifier.apply_to_content(content);
            if (modified_content.empty()) {
                NOTIFY_ERROR("Failed to modify G-code file");
                return;
            }

            // Build modification identifiers for plugin
            std::vector<std::string> mod_names;
            for (const auto& op : ops_to_disable) {
                mod_names.push_back(helix::gcode::GCodeOpsDetector::operation_type_name(op) +
                                    "_disabled");
            }

            // Check if helix_print plugin is available
            if (self->api_->has_helix_plugin()) {
                // NEW PATH: Use helix_print plugin (single API call)
                spdlog::info("[{}] Using helix_print plugin for modified print", self->get_name());

                self->api_->start_modified_print(
                    file_path, modified_content, mod_names,
                    // Success callback
                    [self, original_filename](const MoonrakerAPI::ModifiedPrintResult& result) {
                        spdlog::info("[{}] Print started via helix_print plugin: {} -> {}",
                                     self->get_name(), result.original_filename,
                                     result.print_filename);

                        // Set thumbnail source to original filename
                        get_global_print_status_panel().set_thumbnail_source(original_filename);

                        if (self->print_status_panel_widget_) {
                            self->hide_detail_view();
                            ui_nav_push_overlay(self->print_status_panel_widget_);
                        }
                    },
                    // Error callback
                    [self, original_filename](const MoonrakerError& error) {
                        NOTIFY_ERROR("Failed to start modified print: {}", error.message);
                        LOG_ERROR_INTERNAL("[{}] helix_print plugin error for {}: {}",
                                           self->get_name(), original_filename, error.message);
                    });
            } else {
                // LEGACY PATH: Upload to .helix_temp then start print
                spdlog::info("[{}] Using legacy flow (helix_print plugin not available)",
                             self->get_name());

                // Generate unique temp filename
                std::string temp_filename = ".helix_temp/modified_" +
                                            std::to_string(std::time(nullptr)) + "_" +
                                            original_filename;

                spdlog::info("[{}] Uploading modified G-code to {}", self->get_name(),
                             temp_filename);

                self->api_->upload_file_with_name(
                    "gcodes", temp_filename, temp_filename, modified_content,
                    // Success: start print with modified file
                    [self, temp_filename, original_filename]() {
                        spdlog::info("[{}] Modified file uploaded, starting print",
                                     self->get_name());

                        // Set thumbnail source to original filename before starting print
                        // This ensures PrintStatusPanel loads the correct thumbnail
                        get_global_print_status_panel().set_thumbnail_source(original_filename);

                        // Start print with the modified file
                        self->api_->start_print(
                            temp_filename,
                            [self, original_filename]() {
                                spdlog::info(
                                    "[{}] Print started with modified G-code (original: {})",
                                    self->get_name(), original_filename);
                                if (self->print_status_panel_widget_) {
                                    self->hide_detail_view();
                                    ui_nav_push_overlay(self->print_status_panel_widget_);
                                }
                            },
                            [self, temp_filename](const MoonrakerError& error) {
                                NOTIFY_ERROR("Failed to start print: {}", error.message);
                                LOG_ERROR_INTERNAL("[{}] Print start failed for {}: {}",
                                                   self->get_name(), temp_filename, error.message);
                            });
                    },
                    // Error uploading
                    [self](const MoonrakerError& error) {
                        NOTIFY_ERROR("Failed to upload modified G-code: {}", error.message);
                        LOG_ERROR_INTERNAL("[{}] Upload failed: {}", self->get_name(),
                                           error.message);
                    });
            }
        },
        // Error downloading
        [self, original_filename](const MoonrakerError& error) {
            NOTIFY_ERROR("Failed to download G-code for modification: {}", error.message);
            LOG_ERROR_INTERNAL("[{}] Download failed for {}: {}", self->get_name(),
                               original_filename, error.message);
        });
}

// ============================================================================
// USB Source Methods
// ============================================================================

void PrintSelectPanel::setup_source_buttons() {
    // Find source selector buttons by name
    source_printer_btn_ = lv_obj_find_by_name(panel_, "source_printer_btn");
    source_usb_btn_ = lv_obj_find_by_name(panel_, "source_usb_btn");

    if (!source_printer_btn_ || !source_usb_btn_) {
        spdlog::warn("[{}] Source selector buttons not found", get_name());
        return;
    }

    // Note: click handlers are now in XML via <event_cb>

    // Hide USB tab by default - will be shown when USB drive is inserted
    lv_obj_add_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);

    // Set initial state - Printer is selected by default
    update_source_buttons();

    spdlog::debug("[{}] Source selector buttons configured (USB tab hidden until drive inserted)",
                  get_name());
}

void PrintSelectPanel::update_source_buttons() {
    if (!source_printer_btn_ || !source_usb_btn_) {
        return;
    }

    // Apply LV_STATE_CHECKED to the active source button
    // Make inactive button transparent for segmented control appearance
    if (current_source_ == FileSource::PRINTER) {
        lv_obj_add_state(source_printer_btn_, LV_STATE_CHECKED);
        lv_obj_remove_state(source_usb_btn_, LV_STATE_CHECKED);
        // Active tab: normal opacity, Inactive tab: transparent
        lv_obj_set_style_bg_opa(source_printer_btn_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(source_usb_btn_, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_remove_state(source_printer_btn_, LV_STATE_CHECKED);
        lv_obj_add_state(source_usb_btn_, LV_STATE_CHECKED);
        // Active tab: normal opacity, Inactive tab: transparent
        lv_obj_set_style_bg_opa(source_printer_btn_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(source_usb_btn_, LV_OPA_COVER, LV_PART_MAIN);
    }
}

void PrintSelectPanel::on_source_printer_clicked() {
    if (current_source_ == FileSource::PRINTER) {
        return; // Already on Printer source
    }

    spdlog::debug("[{}] Switching to Printer source", get_name());
    current_source_ = FileSource::PRINTER;
    update_source_buttons();

    // Refresh Moonraker files
    refresh_files();
}

void PrintSelectPanel::on_source_usb_clicked() {
    if (current_source_ == FileSource::USB) {
        return; // Already on USB source
    }

    spdlog::debug("[{}] Switching to USB source", get_name());
    current_source_ = FileSource::USB;
    update_source_buttons();

    // Refresh USB files
    refresh_usb_files();
}

void PrintSelectPanel::refresh_usb_files() {
    usb_files_.clear();
    file_list_.clear();

    if (!usb_manager_) {
        spdlog::warn("[{}] UsbManager not available", get_name());
        update_empty_state();
        return;
    }

    // Get connected USB drives
    auto drives = usb_manager_->get_drives();
    if (drives.empty()) {
        spdlog::debug("[{}] No USB drives detected", get_name());
        update_empty_state();
        return;
    }

    // Scan first drive for G-code files
    // TODO: If multiple drives, show a drive selector
    usb_files_ = usb_manager_->scan_for_gcode(drives[0].mount_path);

    spdlog::info("[{}] Found {} G-code files on USB drive '{}'", get_name(), usb_files_.size(),
                 drives[0].label);

    // Convert USB files to PrintFileData for display
    for (const auto& usb_file : usb_files_) {
        PrintFileData file_data;
        file_data.filename = usb_file.filename;
        file_data.file_size_bytes = usb_file.size_bytes;
        file_data.modified_timestamp = static_cast<time_t>(usb_file.modified_time);
        file_data.print_time_minutes = 0; // USB files don't have Moonraker metadata
        file_data.filament_grams = 0.0f;
        file_data.thumbnail_path = DEFAULT_PLACEHOLDER_THUMB;
        file_data.is_dir = false;

        // Format strings for display
        if (file_data.file_size_bytes < 1024) {
            file_data.size_str = std::to_string(file_data.file_size_bytes) + " B";
        } else if (file_data.file_size_bytes < 1024 * 1024) {
            file_data.size_str = std::to_string(file_data.file_size_bytes / 1024) + " KB";
        } else {
            file_data.size_str = std::to_string(file_data.file_size_bytes / (1024 * 1024)) + " MB";
        }

        // Format modified date
        std::tm* tm_info = std::localtime(&file_data.modified_timestamp);
        if (tm_info) {
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%b %d, %H:%M", tm_info);
            file_data.modified_str = buffer;
        } else {
            file_data.modified_str = "Unknown";
        }

        file_data.print_time_str = "--";
        file_data.filament_str = "--";

        file_list_.push_back(std::move(file_data));
    }

    // Apply sort and update view
    apply_sort();

    if (current_view_mode_ == PrintSelectViewMode::CARD) {
        populate_card_view();
    } else {
        populate_list_view();
    }

    update_empty_state();
}

void PrintSelectPanel::populate_usb_card_view() {
    // USB files use the same card view as Moonraker files
    populate_card_view();
}

void PrintSelectPanel::populate_usb_list_view() {
    // USB files use the same list view as Moonraker files
    populate_list_view();
}

void PrintSelectPanel::set_usb_manager(UsbManager* manager) {
    usb_manager_ = manager;

    // If USB source is currently active, refresh the file list
    if (current_source_ == FileSource::USB && usb_manager_) {
        refresh_usb_files();
    }

    spdlog::debug("[{}] UsbManager set", get_name());
}

void PrintSelectPanel::on_usb_drive_inserted() {
    if (!source_usb_btn_) {
        return;
    }

    spdlog::info("[{}] USB drive inserted - showing USB tab", get_name());
    lv_obj_remove_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);
}

void PrintSelectPanel::on_usb_drive_removed() {
    spdlog::info("[{}] USB drive removed - hiding USB tab", get_name());

    // Hide the USB tab
    if (source_usb_btn_) {
        lv_obj_add_flag(source_usb_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    // If USB source is currently active, switch to Printer source
    if (current_source_ == FileSource::USB) {
        spdlog::debug("[{}] Was viewing USB source - switching to Printer", get_name());

        // Clear USB files and the display file list
        usb_files_.clear();
        file_list_.clear();

        // Clear card/list views
        if (card_view_container_) {
            lv_obj_clean(card_view_container_);
        }
        if (list_rows_container_) {
            lv_obj_clean(list_rows_container_);
        }

        // Switch to Printer source
        current_source_ = FileSource::PRINTER;
        update_source_buttons();

        // Refresh Moonraker files
        refresh_files();
    }
}

void PrintSelectPanel::on_source_button_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    if (target == self->source_printer_btn_) {
        self->on_source_printer_clicked();
    } else if (target == self->source_usb_btn_) {
        self->on_source_usb_clicked();
    }
}
