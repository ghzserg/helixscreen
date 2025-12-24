// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_select.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_fonts.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_print_status.h"
#include "ui_subject_registry.h"
#include "ui_theme.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "ams_state.h"
#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "lvgl/src/xml/lv_xml.h"
#include "moonraker_api.h"
#include "print_start_analyzer.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "thumbnail_cache.h"
#include "usb_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// Header column sort callbacks
static void on_print_select_header_filename(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().sort_by(PrintSelectSortColumn::FILENAME);
}

static void on_print_select_header_size(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().sort_by(PrintSelectSortColumn::SIZE);
}

static void on_print_select_header_modified(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().sort_by(PrintSelectSortColumn::MODIFIED);
}

static void on_print_select_header_print_time(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().sort_by(PrintSelectSortColumn::PRINT_TIME);
}

// Detail view callbacks
static void on_print_select_print_button(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().start_print();
}

static void on_print_select_delete_button(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().show_delete_confirmation();
}

static void on_print_select_detail_backdrop(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* current_target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    // Only close if clicking the backdrop itself, not child widgets
    if (target == current_target) {
        get_global_print_select_panel().hide_detail_view();
    }
}

static void on_print_detail_back_clicked(lv_event_t* e) {
    (void)e;
    get_global_print_select_panel().hide_detail_view();
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
    if (lv_is_initialized()) {
        // Remove scroll event callbacks to prevent use-after-free
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

        // Clean up filament warning dialog if open
        if (filament_warning_dialog_) {
            ui_modal_hide(filament_warning_dialog_);
            filament_warning_dialog_ = nullptr;
        }

        // Clean up color mismatch warning dialog if open
        if (color_mismatch_dialog_) {
            ui_modal_hide(color_mismatch_dialog_);
            color_mismatch_dialog_ = nullptr;
        }
    }

    // Cleanup extracted view modules (handles observer removal internally)
    if (card_view_) {
        card_view_->cleanup();
    }
    if (list_view_) {
        list_view_->cleanup();
    }

    // Reset widget references - the LVGL widget tree handles widget cleanup.
    card_view_container_ = nullptr;
    list_view_container_ = nullptr;
    list_rows_container_ = nullptr;
    empty_state_container_ = nullptr;
    view_toggle_btn_ = nullptr;
    view_toggle_icon_ = nullptr;
    print_status_panel_widget_ = nullptr;

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
    strncpy(selected_thumbnail_buffer_, helix::ui::PrintSelectCardView::DEFAULT_THUMB,
            sizeof(selected_thumbnail_buffer_) - 1);
    UI_SUBJECT_INIT_AND_REGISTER_POINTER(selected_thumbnail_subject_, selected_thumbnail_buffer_,
                                         "selected_thumbnail");

    // Detail view thumbnail - uses cached PNG for better upscaling quality
    strncpy(selected_detail_thumbnail_buffer_, helix::ui::PrintSelectCardView::DEFAULT_THUMB,
            sizeof(selected_detail_thumbnail_buffer_) - 1);
    UI_SUBJECT_INIT_AND_REGISTER_POINTER(selected_detail_thumbnail_subject_,
                                         selected_detail_thumbnail_buffer_,
                                         "selected_detail_thumbnail");

    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_print_time_subject_, selected_print_time_buffer_,
                                        "", "selected_print_time");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_filament_weight_subject_,
                                        selected_filament_weight_buffer_, "",
                                        "selected_filament_weight");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_layer_count_subject_, selected_layer_count_buffer_,
                                        "", "selected_layer_count");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_file_ops_subject_, selected_file_ops_buffer_, "",
                                        "selected_file_ops");
    UI_SUBJECT_INIT_AND_REGISTER_INT(selected_file_ops_visible_subject_, 0,
                                     "selected_file_ops_visible");

    UI_SUBJECT_INIT_AND_REGISTER_STRING(selected_macro_ops_subject_, selected_macro_ops_buffer_, "",
                                        "selected_macro_ops");
    UI_SUBJECT_INIT_AND_REGISTER_INT(selected_macro_ops_visible_subject_, 0,
                                     "selected_macro_ops_visible");

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

    // Register list header sort callbacks
    lv_xml_register_event_cb(nullptr, "on_print_select_header_filename",
                             on_print_select_header_filename);
    lv_xml_register_event_cb(nullptr, "on_print_select_header_size", on_print_select_header_size);
    lv_xml_register_event_cb(nullptr, "on_print_select_header_modified",
                             on_print_select_header_modified);
    lv_xml_register_event_cb(nullptr, "on_print_select_header_print_time",
                             on_print_select_header_print_time);

    // Register detail view callbacks
    lv_xml_register_event_cb(nullptr, "on_print_select_print_button", on_print_select_print_button);
    lv_xml_register_event_cb(nullptr, "on_print_select_delete_button",
                             on_print_select_delete_button);
    lv_xml_register_event_cb(nullptr, "on_print_select_detail_backdrop",
                             on_print_select_detail_backdrop);
    lv_xml_register_event_cb(nullptr, "on_print_detail_back_clicked", on_print_detail_back_clicked);

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

    // Create and setup virtualized view modules
    auto* self = this;

    card_view_ = std::make_unique<helix::ui::PrintSelectCardView>();
    card_view_->setup(
        card_view_container_,
        // File click callback
        [self](size_t file_index) { self->handle_file_click(file_index); },
        // Metadata fetch callback
        [self](size_t start, size_t end) { self->fetch_metadata_range(start, end); });

    list_view_ = std::make_unique<helix::ui::PrintSelectListView>();
    list_view_->setup(
        list_rows_container_,
        // File click callback
        [self](size_t file_index) { self->handle_file_click(file_index); },
        // Metadata fetch callback
        [self](size_t start, size_t end) { self->fetch_metadata_range(start, end); });

    // Note: view_toggle_btn, source buttons, and header click handlers are now in XML via
    // <event_cb>

    // Initialize USB source manager
    usb_source_ = std::make_unique<helix::ui::PrintSelectUsbSource>();
    usb_source_->setup(panel);
    usb_source_->set_on_source_changed([self](FileSource source) {
        if (source == FileSource::PRINTER) {
            self->refresh_files();
        }
        // USB source refresh is handled by usb_source_ internally via on_files_ready callback
    });
    usb_source_->set_on_files_ready([self](std::vector<PrintFileData>&& files) {
        self->file_list_ = std::move(files);
        self->metadata_fetched_.clear();
        self->metadata_fetched_.resize(self->file_list_.size(), true); // USB files have no metadata

        self->apply_sort();
        if (self->current_view_mode_ == PrintSelectViewMode::CARD) {
            self->populate_card_view();
        } else {
            self->populate_list_view();
        }
        self->update_empty_state();
    });

    // Initialize file data provider for Moonraker files
    file_provider_ = std::make_unique<helix::ui::PrintSelectFileProvider>();
    file_provider_->set_api(api_);
    file_provider_->set_on_files_ready([self](std::vector<PrintFileData>&& files,
                                              std::vector<bool>&& fetched) {
        self->file_list_ = std::move(files);
        self->metadata_fetched_ = std::move(fetched);

        // Dispatch UI updates to main thread
        ui_async_call(
            [](void* user_data) {
                auto* panel = static_cast<PrintSelectPanel*>(user_data);

                panel->apply_sort();
                panel->update_sort_indicators();
                panel->populate_card_view();
                panel->populate_list_view();
                panel->update_empty_state();

                // Check for pending file selection
                std::string pending;
                if (!panel->pending_file_selection_.empty()) {
                    pending = panel->pending_file_selection_;
                    panel->pending_file_selection_.clear();
                } else if (get_runtime_config()->select_file != nullptr) {
                    static bool select_file_checked = false;
                    if (!select_file_checked) {
                        pending = get_runtime_config()->select_file;
                        select_file_checked = true;
                    }
                }
                if (!pending.empty()) {
                    if (!panel->select_file_by_name(pending)) {
                        spdlog::warn("[{}] Pending file selection '{}' not found in file list",
                                     panel->get_name(), pending);
                    }
                }

                // Fetch metadata for visible items
                int visible_start = 0, visible_end = 0;
                if (panel->current_view_mode_ == PrintSelectViewMode::CARD && panel->card_view_) {
                    panel->card_view_->get_visible_range(visible_start, visible_end);
                } else if (panel->list_view_) {
                    panel->list_view_->get_visible_range(visible_start, visible_end);
                }
                if (visible_end == 0 && !panel->file_list_.empty()) {
                    visible_end = static_cast<int>(std::min(panel->file_list_.size(), size_t{20}));
                }
                panel->fetch_metadata_range(static_cast<size_t>(visible_start),
                                            static_cast<size_t>(visible_end));
            },
            self);
    });
    file_provider_->set_on_metadata_updated([self](size_t index, const PrintFileData& updated) {
        // Update file in list
        if (index < self->file_list_.size() &&
            self->file_list_[index].filename == updated.filename) {
            // Merge updated fields
            if (updated.print_time_minutes > 0) {
                self->file_list_[index].print_time_minutes = updated.print_time_minutes;
                self->file_list_[index].print_time_str = updated.print_time_str;
            }
            if (updated.filament_grams > 0) {
                self->file_list_[index].filament_grams = updated.filament_grams;
                self->file_list_[index].filament_str = updated.filament_str;
            }
            if (!updated.filament_type.empty()) {
                self->file_list_[index].filament_type = updated.filament_type;
            }
            if (updated.layer_count > 0) {
                self->file_list_[index].layer_count = updated.layer_count;
                self->file_list_[index].layer_count_str = updated.layer_count_str;
            }
            if (!updated.thumbnail_path.empty() &&
                updated.thumbnail_path != helix::ui::PrintSelectCardView::DEFAULT_THUMB) {
                self->file_list_[index].thumbnail_path = updated.thumbnail_path;
            }

            // Schedule debounced view refresh
            self->schedule_view_refresh();

            // Update detail view if this file is selected
            if (strcmp(self->selected_filename_buffer_, updated.filename.c_str()) == 0) {
                self->set_selected_file(updated.filename.c_str(),
                                        self->file_list_[index].thumbnail_path.c_str(),
                                        self->file_list_[index].original_thumbnail_url.c_str(),
                                        self->file_list_[index].print_time_str.c_str(),
                                        self->file_list_[index].filament_str.c_str(),
                                        self->file_list_[index].layer_count_str.c_str());
            }
        }
    });
    file_provider_->set_on_error([self](const std::string& error) {
        NOTIFY_ERROR("Failed to refresh file list");
        LOG_ERROR_INTERNAL("[{}] File list refresh error: {}", self->get_name(), error);
    });

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
                    // Refresh files if empty (and on Printer source, not USB)
                    bool is_printer_source =
                        !self->usb_source_ || !self->usb_source_->is_usb_active();
                    if (self->file_list_.empty() && is_printer_source) {
                        spdlog::info("[{}] Connection established, refreshing file list",
                                     self->get_name());
                        self->refresh_files();
                    }

                    // Update installer's websocket URL for local/remote detection
                    if (self->api_) {
                        self->plugin_installer_.set_websocket_url(
                            self->api_->get_client().get_last_url());
                    }
                    // Note: Plugin detection now happens automatically in discovery flow
                    // (application.cpp). Install prompt is triggered by helix_plugin_observer_.
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

    // Also observe print_in_progress subject - this fires immediately when Print is tapped
    // (before Moonraker reports state change, which can take seconds)
    lv_subject_t* print_in_progress_subject = printer_state_.get_print_in_progress_subject();
    if (print_in_progress_subject) {
        print_in_progress_observer_ = ObserverGuard(
            print_in_progress_subject,
            [](lv_observer_t* observer, lv_subject_t* /*subject*/) {
                auto* self = static_cast<PrintSelectPanel*>(lv_observer_get_user_data(observer));
                if (self) {
                    self->update_print_button_state();
                }
            },
            this);
        spdlog::debug("[{}] Registered observer on print_in_progress for print button", get_name());
    }

    // Register observer on helix_plugin_installed to show install prompt when plugin not available
    // This fires after discovery completes and plugin status is known
    lv_subject_t* plugin_subject = printer_state_.get_helix_plugin_installed_subject();
    if (plugin_subject) {
        helix_plugin_observer_ = ObserverGuard(
            plugin_subject,
            [](lv_observer_t* observer, lv_subject_t* subject) {
                auto* self = static_cast<PrintSelectPanel*>(lv_observer_get_user_data(observer));
                if (!self)
                    return;

                bool plugin_available = lv_subject_get_int(subject) != 0;
                if (!plugin_available && self->plugin_installer_.should_prompt_install()) {
                    spdlog::info("[PrintSelectPanel] helix_print plugin not available, showing "
                                 "install prompt");
                    self->plugin_install_modal_.set_installer(&self->plugin_installer_);
                    self->plugin_install_modal_.show(lv_screen_active());
                }
            },
            this);
        spdlog::debug("[{}] Registered observer on helix_plugin_installed for install prompt",
                      get_name());
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

        // Animate list container entrance with crossfade
        animate_view_entrance(list_view_container_);

        // Animate list rows with staggered entrance (runs in parallel with container fade)
        if (list_view_) {
            list_view_->animate_entrance();
        }
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

        // Animate card container entrance with crossfade
        animate_view_entrance(card_view_container_);
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
    if (!file_provider_) {
        spdlog::warn("[{}] Cannot refresh files: file provider not initialized", get_name());
        return;
    }

    if (!file_provider_->is_ready()) {
        spdlog::debug("[{}] Cannot refresh files: not connected", get_name());
        return;
    }

    // Delegate to file provider - callbacks set in setup() will handle the results
    file_provider_->refresh_files(current_path_, file_list_, metadata_fetched_);
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
                uint32_t layer_count = metadata.layer_count;

                // Smart thumbnail selection: pick smallest that meets display requirements
                // This reduces download size while ensuring adequate resolution
                helix::ThumbnailTarget target = helix::ThumbnailProcessor::get_target_for_display();
                const ThumbnailInfo* best_thumb =
                    metadata.get_best_thumbnail(target.width, target.height);
                std::string thumb_path = best_thumb ? best_thumb->relative_path : "";

                // Format strings on background thread (uses standalone helper functions)
                std::string print_time_str = format_print_time(print_time_minutes);
                std::string filament_str = format_filament_weight(filament_grams);
                std::string layer_count_str = layer_count > 0 ? std::to_string(layer_count) : "--";

                // Check if thumbnail is a local file (background thread - filesystem OK)
                bool thumb_is_local = !thumb_path.empty() && std::filesystem::exists(thumb_path);

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
                    bool thumb_is_local;
                    helix::ThumbnailTarget thumb_target; // Target size for pre-scaling
                };

                ui_queue_update<MetadataUpdate>(
                    std::make_unique<MetadataUpdate>(
                        MetadataUpdate{self, i, filename, print_time_minutes, filament_grams,
                                       filament_type, print_time_str, filament_str, layer_count,
                                       layer_count_str, thumb_path, thumb_is_local, target}),
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

                        // Handle thumbnail with pre-scaling optimization
                        if (!d->thumb_path.empty() && self->api_) {
                            // Store original URL for detail view PNG lookup
                            self->file_list_[d->index].original_thumbnail_url = d->thumb_path;

                            if (d->thumb_is_local) {
                                // Local file exists - use directly (mock mode)
                                self->file_list_[d->index].thumbnail_path = "A:" + d->thumb_path;
                                spdlog::trace("[{}] Using local thumbnail for {}: {}",
                                              self->get_name(), d->filename,
                                              self->file_list_[d->index].thumbnail_path);
                            } else {
                                // Remote path - fetch with pre-scaling for optimal display
                                spdlog::trace("[{}] Fetching optimized thumbnail for {}: {}",
                                              self->get_name(), d->filename, d->thumb_path);

                                size_t file_idx = d->index;
                                std::string filename_copy = d->filename;
                                helix::ThumbnailTarget target = d->thumb_target;

                                get_thumbnail_cache().fetch_optimized(
                                    self->api_, d->thumb_path, target,
                                    // Success callback - receives pre-scaled .bin path
                                    [self, file_idx, filename_copy](const std::string& lvgl_path) {
                                        struct ThumbUpdate {
                                            PrintSelectPanel* panel;
                                            size_t index;
                                            std::string filename;
                                            std::string lvgl_path;
                                        };
                                        ui_queue_update<ThumbUpdate>(
                                            std::make_unique<ThumbUpdate>(ThumbUpdate{
                                                self, file_idx, filename_copy, lvgl_path}),
                                            [](ThumbUpdate* t) {
                                                if (t->index < t->panel->file_list_.size() &&
                                                    t->panel->file_list_[t->index].filename ==
                                                        t->filename) {
                                                    t->panel->file_list_[t->index].thumbnail_path =
                                                        t->lvgl_path;
                                                    spdlog::debug(
                                                        "[{}] Optimized thumbnail for {}: {}",
                                                        t->panel->get_name(), t->filename,
                                                        t->panel->file_list_[t->index]
                                                            .thumbnail_path);
                                                    t->panel->schedule_view_refresh();
                                                }
                                            });
                                    },
                                    // Error callback
                                    [self, filename_copy](const std::string& error) {
                                        spdlog::warn("[{}] Failed to fetch thumbnail for {}: {}",
                                                     self->get_name(), filename_copy, error);
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
                                self->file_list_[d->index].original_thumbnail_url.c_str(),
                                d->print_time_str.c_str(), d->filament_str.c_str(),
                                d->layer_count_str.c_str());
                        }
                    });
            },
            // Metadata error callback
            [self, filename](const MoonrakerError& error) {
                spdlog::debug("[{}] Failed to get metadata for {}: {} ({})", self->get_name(),
                              filename, error.message, error.get_type_string());
            },
            true // silent - don't trigger RPC_ERROR event/toast
        );
    }

    if (fetch_count > 0) {
        spdlog::debug("[{}] fetch_metadata_range({}, {}): started {} metadata requests", get_name(),
                      start, end, fetch_count);
    }
}

void PrintSelectPanel::set_api(MoonrakerAPI* api) {
    api_ = api;

    // Update file provider's API reference (it was created with nullptr in setup())
    if (file_provider_) {
        file_provider_->set_api(api_);
    }

    // Update detail view's dependencies (it was created with nullptr in setup())
    if (detail_view_) {
        detail_view_->set_dependencies(api_, &printer_state_);
    }

    // Check if Moonraker has symlink access to USB files (same-host setup)
    // If so, hide the USB tab since files are already accessible via Printer source
    if (api_ && usb_source_) {
        check_moonraker_usb_symlink();
    }

    // Note: Don't auto-refresh here - WebSocket may not be connected yet.
    // refresh_files() has a connection check that will silently return if not connected.
    // Files will be loaded lazily via on_activate() when user navigates to this panel.
    // helix_print plugin check happens in connection observer (after connection established)
    if (api_ && panel_initialized_) {
        spdlog::debug("[{}] API set, files will load on first view", get_name());
        refresh_files(); // Will early-return if not connected
    }
}

void PrintSelectPanel::check_moonraker_usb_symlink() {
    if (!api_ || !usb_source_) {
        return;
    }

    spdlog::debug("[{}] Checking if Moonraker has USB symlink access...", get_name());

    // Query Moonraker for files in the "usb" directory
    // If it exists and has files, Klipper's mod has created a symlink
    auto* self = this;
    api_->list_files(
        "gcodes", "usb", false,
        [self](const std::vector<FileInfo>& files) {
            // If there are any files or the directory exists, symlink is active
            // Note: An empty directory still counts - the symlink exists even if USB is empty
            if (!files.empty()) {
                spdlog::info("[{}] Moonraker has USB symlink access ({} files) - hiding USB tab",
                             self->get_name(), files.size());
                if (self->usb_source_) {
                    self->usb_source_->set_moonraker_has_usb_access(true);
                }
            } else {
                spdlog::debug("[{}] Moonraker USB path exists but empty - symlink likely active",
                              self->get_name());
                // Even an empty usb/ directory suggests symlink is set up
                if (self->usb_source_) {
                    self->usb_source_->set_moonraker_has_usb_access(true);
                }
            }
        },
        [self](const MoonrakerError& error) {
            // 404 or error means no symlink - USB tab should be available
            spdlog::debug("[{}] No Moonraker USB symlink detected ({})", self->get_name(),
                          error.message);
            // usb_source_ will show USB tab when drive is inserted
        });
}

void PrintSelectPanel::on_activate() {
    // On first activation: skip refresh if files already loaded (connection observer did it)
    // On subsequent activations: refresh to pick up external changes
    bool is_usb_active = usb_source_ && usb_source_->is_usb_active();

    if (!is_usb_active && api_) {
        // Printer (Moonraker) source
        if (first_activation_ && !file_list_.empty()) {
            first_activation_ = false;
            spdlog::debug("[{}] First activation, files already loaded - skipping refresh",
                          get_name());
            return;
        }
        first_activation_ = false;
        spdlog::info("[{}] Panel activated, refreshing file list", get_name());
        refresh_files();
    } else if (is_usb_active) {
        // USB source
        if (first_activation_ && !file_list_.empty()) {
            first_activation_ = false;
            spdlog::debug("[{}] First activation, files already loaded - skipping refresh",
                          get_name());
            return;
        }
        first_activation_ = false;
        spdlog::info("[{}] Panel activated, refreshing USB file list", get_name());
        if (usb_source_) {
            usb_source_->refresh_files();
        }
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
                                         const char* original_url, const char* print_time,
                                         const char* filament_weight, const char* layer_count) {
    lv_subject_copy_string(&selected_filename_subject_, filename);

    // Card thumbnail uses POINTER subject - copy to buffer then update pointer
    // This is the pre-scaled .bin for fast card rendering
    strncpy(selected_thumbnail_buffer_, thumbnail_src, sizeof(selected_thumbnail_buffer_) - 1);
    selected_thumbnail_buffer_[sizeof(selected_thumbnail_buffer_) - 1] = '\0';
    lv_subject_set_pointer(&selected_thumbnail_subject_, selected_thumbnail_buffer_);

    // Detail view thumbnail - use cached PNG for better upscaling quality
    // The PNG was downloaded by ThumbnailCache alongside the pre-scaled .bin
    if (original_url && original_url[0] != '\0') {
        // Look up the PNG path from the original Moonraker URL
        std::string png_path = get_thumbnail_cache().get_if_cached(original_url);
        if (!png_path.empty()) {
            strncpy(selected_detail_thumbnail_buffer_, png_path.c_str(),
                    sizeof(selected_detail_thumbnail_buffer_) - 1);
            selected_detail_thumbnail_buffer_[sizeof(selected_detail_thumbnail_buffer_) - 1] = '\0';
            spdlog::debug("[{}] Using cached PNG for detail view: {}", get_name(), png_path);
        } else {
            // Fallback to pre-scaled thumbnail if PNG not cached
            strncpy(selected_detail_thumbnail_buffer_, thumbnail_src,
                    sizeof(selected_detail_thumbnail_buffer_) - 1);
            selected_detail_thumbnail_buffer_[sizeof(selected_detail_thumbnail_buffer_) - 1] = '\0';
            spdlog::debug("[{}] PNG not cached, using pre-scaled for detail: {}", get_name(),
                          thumbnail_src);
        }
    } else {
        // No original URL - use same as card thumbnail
        strncpy(selected_detail_thumbnail_buffer_, thumbnail_src,
                sizeof(selected_detail_thumbnail_buffer_) - 1);
        selected_detail_thumbnail_buffer_[sizeof(selected_detail_thumbnail_buffer_) - 1] = '\0';
    }
    lv_subject_set_pointer(&selected_detail_thumbnail_subject_, selected_detail_thumbnail_buffer_);

    lv_subject_copy_string(&selected_print_time_subject_, print_time);
    lv_subject_copy_string(&selected_filament_weight_subject_, filament_weight);
    lv_subject_copy_string(&selected_layer_count_subject_, layer_count);

    spdlog::info("[{}] Selected file: {}", get_name(), filename);
}

void PrintSelectPanel::show_detail_view() {
    if (detail_view_) {
        std::string filename(selected_filename_buffer_);
        detail_view_->show(filename, current_path_, selected_filament_type_,
                           selected_filament_colors_, selected_file_size_bytes_);
    }
}

void PrintSelectPanel::hide_detail_view() {
    if (detail_view_) {
        detail_view_->hide();
    }
}

void PrintSelectPanel::show_delete_confirmation() {
    if (detail_view_) {
        std::string filename(selected_filename_buffer_);
        detail_view_->show_delete_confirmation(filename);
    }
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
    ui_async_call(
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
    // Delegates to extracted view modules
    if (card_view_ && card_view_->is_initialized()) {
        CardDimensions dims = calculate_card_dimensions();
        card_view_->refresh_content(file_list_, dims);
    }

    if (list_view_ && list_view_->is_initialized()) {
        list_view_->refresh_content(file_list_);
    }
}

void PrintSelectPanel::handle_scroll(lv_obj_t* container) {
    // Delegate to extracted view modules (they trigger metadata fetch via callback)
    if (container == card_view_container_ && card_view_) {
        CardDimensions dims = calculate_card_dimensions();
        card_view_->update_visible(file_list_, dims);
    } else if (container == list_rows_container_ && list_view_) {
        list_view_->update_visible(file_list_);
    }
}

void PrintSelectPanel::populate_card_view() {
    if (!card_view_ || !card_view_container_)
        return;

    spdlog::debug("[{}] populate_card_view() with {} files (virtualized)", get_name(),
                  file_list_.size());

    // Delegate to extracted card view module
    CardDimensions dims = calculate_card_dimensions();
    card_view_->populate(file_list_, dims);

    spdlog::debug("[{}] Card view populated with {} files", get_name(), file_list_.size());
}

void PrintSelectPanel::animate_view_entrance(lv_obj_t* container) {
    if (!container)
        return;

    // Skip animation if disabled - show container in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_opa(container, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[{}] Animations disabled - showing view instantly", get_name());
        return;
    }

    // Animation constants for view transition
    constexpr int32_t FADE_DURATION_MS = 150;

    // Start container transparent
    lv_obj_set_style_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);

    // Fade in animation
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, container);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, FADE_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    spdlog::debug("[{}] View entrance animation started", get_name());
}

void PrintSelectPanel::populate_list_view() {
    if (!list_view_ || !list_rows_container_)
        return;

    spdlog::debug("[{}] populate_list_view() with {} files (virtualized)", get_name(),
                  file_list_.size());

    // Delegate to extracted list view module
    list_view_->populate(file_list_);

    // Trigger entrance animation for newly visible rows
    list_view_->animate_entrance();

    spdlog::debug("[{}] List view populated with {} files", get_name(), file_list_.size());
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

    // Animation constants for sort indicator transition
    constexpr int32_t FADE_DURATION_MS = 200;

    // Check if animations are enabled
    bool animations_enabled = SettingsManager::instance().get_animations_enabled();

    // Helper lambda for animated show/hide with crossfade
    auto animate_icon_visibility = [animations_enabled](lv_obj_t* icon, bool show) {
        if (!icon)
            return;

        if (show) {
            // Show icon
            lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);

            if (!animations_enabled) {
                // Instant show
                lv_obj_set_style_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
                return;
            }

            // Show with fade in
            lv_obj_set_style_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);

            lv_anim_t fade_in;
            lv_anim_init(&fade_in);
            lv_anim_set_var(&fade_in, icon);
            lv_anim_set_values(&fade_in, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_duration(&fade_in, FADE_DURATION_MS);
            lv_anim_set_path_cb(&fade_in, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&fade_in, [](void* obj, int32_t value) {
                lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                                     LV_PART_MAIN);
            });
            lv_anim_start(&fade_in);
        } else {
            // Hide icon
            if (!animations_enabled) {
                // Instant hide
                lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
                return;
            }

            // Hide with fade out (hide flag set in completion callback)
            bool is_visible = !lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN);
            if (is_visible) {
                lv_anim_t fade_out;
                lv_anim_init(&fade_out);
                lv_anim_set_var(&fade_out, icon);
                lv_anim_set_values(&fade_out, LV_OPA_COVER, LV_OPA_TRANSP);
                lv_anim_set_duration(&fade_out, FADE_DURATION_MS);
                lv_anim_set_path_cb(&fade_out, lv_anim_path_ease_in);
                lv_anim_set_exec_cb(&fade_out, [](void* obj, int32_t value) {
                    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                                         LV_PART_MAIN);
                });
                lv_anim_set_completed_cb(&fade_out, [](lv_anim_t* anim) {
                    lv_obj_add_flag(static_cast<lv_obj_t*>(anim->var), LV_OBJ_FLAG_HIDDEN);
                });
                lv_anim_start(&fade_out);
            } else {
                // Already hidden, just ensure it stays hidden
                lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
    };

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
                    animate_icon_visibility(icon_up, true);
                    animate_icon_visibility(icon_down, false);
                } else {
                    animate_icon_visibility(icon_up, false);
                    animate_icon_visibility(icon_down, true);
                }
            } else {
                animate_icon_visibility(icon_up, false);
                animate_icon_visibility(icon_down, false);
            }
        }
    }
}

void PrintSelectPanel::create_detail_view() {
    detail_view_ = std::make_unique<helix::ui::PrintSelectDetailView>();

    if (!detail_view_->create(parent_screen_)) {
        spdlog::error("[{}] Failed to create detail view", get_name());
        detail_view_.reset();
        return;
    }

    // Set dependencies and callbacks
    detail_view_->set_dependencies(api_, &printer_state_);
    detail_view_->set_visible_subject(&detail_view_visible_subject_);
    detail_view_->set_on_delete_confirmed([this]() { delete_file(); });

    // Set callback to update detected operations display when scan completes
    if (auto* prep_mgr = detail_view_->get_prep_manager()) {
        prep_mgr->set_scan_complete_callback([this](const std::string& formatted_ops) {
            lv_subject_copy_string(&selected_file_ops_subject_, formatted_ops.c_str());
            // Update visibility: 1 = show (has content), 0 = hide (empty)
            lv_subject_set_int(&selected_file_ops_visible_subject_, formatted_ops.empty() ? 0 : 1);
            spdlog::debug("[{}] Updated file ops: '{}' (visible: {})", get_name(), formatted_ops,
                          !formatted_ops.empty());
        });

        // Set callback to update PRINT_START macro operations display
        prep_mgr->set_macro_analysis_callback([this](
                                                  const helix::PrintStartAnalysis& /*analysis*/) {
            // Note: detail_view_ guaranteed valid here - callback stored in prep_manager owned by
            // detail_view_
            if (auto* prep_mgr = detail_view_->get_prep_manager()) {
                std::string formatted = prep_mgr->format_macro_operations();
                lv_subject_copy_string(&selected_macro_ops_subject_, formatted.c_str());
                // Update visibility: 1 = show (has content), 0 = hide (empty)
                lv_subject_set_int(&selected_macro_ops_visible_subject_, formatted.empty() ? 0 : 1);
                spdlog::debug("[{}] Updated macro ops: '{}' (visible: {})", get_name(), formatted,
                              !formatted.empty());
            }
        });
    }

    spdlog::debug("[{}] Detail view module initialized", get_name());
}

void PrintSelectPanel::hide_delete_confirmation() {
    if (detail_view_) {
        detail_view_->hide_delete_confirmation();
    }
}

void PrintSelectPanel::handle_resize() {
    if (!panel_initialized_)
        return;

    spdlog::info("[{}] Handling resize event", get_name());

    if (current_view_mode_ == PrintSelectViewMode::CARD && card_view_container_) {
        populate_card_view();
    }

    if (detail_view_ && parent_screen_) {
        detail_view_->handle_resize(parent_screen_);
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
                          file.original_thumbnail_url.c_str(), file.print_time_str.c_str(),
                          file.filament_str.c_str(), file.layer_count_str.c_str());
        selected_filament_type_ = file.filament_type;
        selected_filament_colors_ = file.filament_colors;
        selected_file_size_bytes_ = file.file_size_bytes;
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

    // Check if runout sensor shows no filament (pre-print warning)
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::RUNOUT) &&
        !sensor_mgr.is_filament_detected(helix::FilamentSensorRole::RUNOUT)) {
        // No filament detected - show warning
        spdlog::info("[{}] Runout sensor shows no filament - showing pre-print warning",
                     get_name());
        show_filament_warning();
        return;
    }

    // Check if G-code requires colors not loaded in AMS
    auto missing_tools = check_ams_color_match();
    if (!missing_tools.empty()) {
        spdlog::info("[{}] G-code requires {} tool colors not found in AMS slots", get_name(),
                     missing_tools.size());
        show_color_mismatch_warning(missing_tools);
        return;
    }

    // All checks passed - proceed directly
    execute_print_start();
}

void PrintSelectPanel::execute_print_start() {
    auto* prep_manager = detail_view_ ? detail_view_->get_prep_manager() : nullptr;
    if (!prep_manager) {
        spdlog::error("[{}] Cannot start print - prep manager not initialized", get_name());
        NOTIFY_ERROR("Cannot start print: internal error");
        return;
    }

    std::string filename_to_print(selected_filename_buffer_);
    auto* self = this;

    // Read options to check for timelapse (handled separately from prep_manager)
    auto options = prep_manager->read_options_from_checkboxes();

    spdlog::info(
        "[{}] Starting print: {} (pre-print: mesh={}, qgl={}, z_tilt={}, clean={}, timelapse={})",
        get_name(), filename_to_print, options.bed_leveling, options.qgl, options.z_tilt,
        options.nozzle_clean, options.timelapse);

    // Enable timelapse recording if requested (Moonraker-Timelapse plugin)
    if (options.timelapse && api_) {
        api_->set_timelapse_enabled(
            true, []() { spdlog::info("[PrintSelect] Timelapse enabled for this print"); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintSelect] Failed to enable timelapse: {}", err.message);
            });
    }

    // Delegate to PrintPreparationManager
    prep_manager->start_print(
        filename_to_print, current_path_,
        // Navigation callback - navigate to print status panel
        [self]() {
            spdlog::info("[{}] Print started - navigating to print status panel", self->get_name());
            if (self->print_status_panel_widget_) {
                self->hide_detail_view();
                ui_nav_push_overlay(self->print_status_panel_widget_);
            } else {
                spdlog::error("[{}] Print status panel not set", self->get_name());
            }
        },
        // Preparing callback - update status panel preparing state
        [](const std::string& op_name, int step, int total) {
            auto& status_panel = get_global_print_status_panel();
            status_panel.set_preparing(op_name, step, total);
        },
        // Progress callback - update status panel progress
        [](float progress) {
            auto& status_panel = get_global_print_status_panel();
            status_panel.set_preparing_progress(progress);
        },
        // Completion callback
        [self](bool success, const std::string& error) {
            auto& status_panel = get_global_print_status_panel();

            if (success) {
                spdlog::info("[{}] Print started successfully", self->get_name());
                status_panel.end_preparing(true);
            } else if (!error.empty()) {
                NOTIFY_ERROR("Pre-print failed: {}", error);
                LOG_ERROR_INTERNAL("[{}] Pre-print sequence failed: {}", self->get_name(), error);
                status_panel.end_preparing(false);
            }
        });
}

void PrintSelectPanel::show_filament_warning() {
    // Close any existing dialog first
    if (filament_warning_dialog_) {
        ui_modal_hide(filament_warning_dialog_);
        filament_warning_dialog_ = nullptr;
    }

    const char* attrs[] = {"title", "No Filament Detected", "message",
                           "The runout sensor indicates no filament is loaded. "
                           "Start print anyway?",
                           nullptr};

    ui_modal_configure(ModalSeverity::Warning, true, "Start Print", "Cancel");
    filament_warning_dialog_ = ui_modal_show("modal_dialog", attrs);

    if (!filament_warning_dialog_) {
        spdlog::error("[{}] Failed to create filament warning dialog", get_name());
        return;
    }

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(filament_warning_dialog_, "btn_secondary");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_filament_warning_cancel_static, LV_EVENT_CLICKED, this);
    }

    // Wire up proceed button
    lv_obj_t* proceed_btn = lv_obj_find_by_name(filament_warning_dialog_, "btn_primary");
    if (proceed_btn) {
        lv_obj_add_event_cb(proceed_btn, on_filament_warning_proceed_static, LV_EVENT_CLICKED,
                            this);
    }

    spdlog::debug("[{}] Pre-print filament warning dialog shown", get_name());
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

void PrintSelectPanel::on_file_clicked_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self && target) {
        size_t file_index = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
        self->handle_file_click(file_index);
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
// USB Source Methods (delegate to usb_source_ module)
// ============================================================================

void PrintSelectPanel::on_source_printer_clicked() {
    if (!usb_source_) {
        spdlog::warn("[{}] USB source module not initialized", get_name());
        return;
    }
    usb_source_->select_printer_source();
}

void PrintSelectPanel::on_source_usb_clicked() {
    if (!usb_source_) {
        spdlog::warn("[{}] USB source module not initialized", get_name());
        return;
    }
    usb_source_->select_usb_source();
}

void PrintSelectPanel::set_usb_manager(UsbManager* manager) {
    if (usb_source_) {
        usb_source_->set_usb_manager(manager);
    }
    spdlog::debug("[{}] UsbManager set", get_name());
}

void PrintSelectPanel::on_usb_drive_inserted() {
    if (usb_source_) {
        usb_source_->on_drive_inserted();
    }
}

void PrintSelectPanel::on_usb_drive_removed() {
    if (usb_source_) {
        usb_source_->on_drive_removed();
    }
    // Note: The usb_source_ module handles switching to Printer source if needed,
    // and the on_source_changed callback triggers refresh_files()
}

void PrintSelectPanel::on_filament_warning_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintSelectPanel] on_filament_warning_proceed_static");
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->filament_warning_dialog_) {
            ui_modal_hide(self->filament_warning_dialog_);
            self->filament_warning_dialog_ = nullptr;
        }
        // Execute print
        self->execute_print_start();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintSelectPanel::on_filament_warning_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintSelectPanel] on_filament_warning_cancel_static");
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self && self->filament_warning_dialog_) {
        ui_modal_hide(self->filament_warning_dialog_);
        self->filament_warning_dialog_ = nullptr;
        spdlog::debug("[PrintSelectPanel] Print cancelled by user (no filament warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// AMS Color Mismatch Detection
// ============================================================================

std::vector<int> PrintSelectPanel::check_ams_color_match() {
    std::vector<int> missing_tools;

    // Skip check if no multi-color G-code (single color or no colors)
    if (selected_filament_colors_.size() <= 1) {
        return missing_tools;
    }

    // Skip check if AMS not available
    if (!AmsState::instance().is_available()) {
        spdlog::debug("[{}] AMS not available, skipping color match check", get_name());
        return missing_tools;
    }

    // Get slot count from AMS
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (slot_count <= 0) {
        spdlog::debug("[{}] No AMS slots available", get_name());
        return missing_tools;
    }

    // Collect all slot colors
    std::vector<uint32_t> slot_colors;
    for (int i = 0; i < slot_count && i < AmsState::MAX_SLOTS; ++i) {
        lv_subject_t* color_subject = AmsState::instance().get_slot_color_subject(i);
        if (color_subject) {
            uint32_t color = static_cast<uint32_t>(lv_subject_get_int(color_subject));
            slot_colors.push_back(color);
        }
    }

    // Color match tolerance (0-255 scale)
    // Value of 40 allows ~15% variance per RGB channel, accounting for
    // differences between slicer color palettes and Spoolman/AMS colors
    constexpr int COLOR_MATCH_TOLERANCE = 40;

    // Check each required tool color
    for (size_t tool_idx = 0; tool_idx < selected_filament_colors_.size(); ++tool_idx) {
        auto required_color = ui_parse_hex_color(selected_filament_colors_[tool_idx]);
        if (!required_color) {
            continue; // Skip invalid/empty colors (but NOT black #000000!)
        }

        // Look for a matching slot
        bool found_match = false;
        for (uint32_t slot_color : slot_colors) {
            if (ui_color_distance(*required_color, slot_color) <= COLOR_MATCH_TOLERANCE) {
                found_match = true;
                break;
            }
        }

        if (!found_match) {
            missing_tools.push_back(static_cast<int>(tool_idx));
            spdlog::debug("[{}] Tool T{} color #{:06X} not found in AMS slots", get_name(),
                          tool_idx, *required_color);
        }
    }

    return missing_tools;
}

void PrintSelectPanel::show_color_mismatch_warning(const std::vector<int>& missing_tools) {
    // Close any existing dialog first
    if (color_mismatch_dialog_) {
        ui_modal_hide(color_mismatch_dialog_);
        color_mismatch_dialog_ = nullptr;
    }

    // Build message listing missing tools and their colors
    std::string message = "This print requires colors not loaded in the AMS:\n\n";
    for (int tool_idx : missing_tools) {
        if (tool_idx < static_cast<int>(selected_filament_colors_.size())) {
            const std::string& color = selected_filament_colors_[tool_idx];
            message += "  • T" + std::to_string(tool_idx) + ": " + color + "\n";
        }
    }
    message += "\nLoad the required filaments or start anyway?";

    // Static buffer for message - must persist during modal lifetime.
    // Safe because we always close any existing dialog first (line 1822-1825),
    // preventing concurrent access to this buffer.
    static char message_buffer[512];
    snprintf(message_buffer, sizeof(message_buffer), "%s", message.c_str());

    const char* attrs[] = {"title", "Color Mismatch", "message", message_buffer, nullptr};

    ui_modal_configure(ModalSeverity::Warning, true, "Start Anyway", "Cancel");
    color_mismatch_dialog_ = ui_modal_show("modal_dialog", attrs);

    if (!color_mismatch_dialog_) {
        spdlog::error("[{}] Failed to create color mismatch warning dialog", get_name());
        return;
    }

    // Wire up cancel button
    lv_obj_t* cancel_btn = lv_obj_find_by_name(color_mismatch_dialog_, "btn_secondary");
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, on_color_mismatch_cancel_static, LV_EVENT_CLICKED, this);
    }

    // Wire up proceed button
    lv_obj_t* proceed_btn = lv_obj_find_by_name(color_mismatch_dialog_, "btn_primary");
    if (proceed_btn) {
        lv_obj_add_event_cb(proceed_btn, on_color_mismatch_proceed_static, LV_EVENT_CLICKED, this);
    }

    spdlog::debug("[{}] Color mismatch warning dialog shown for {} tools", get_name(),
                  missing_tools.size());
}

void PrintSelectPanel::on_color_mismatch_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintSelectPanel] on_color_mismatch_proceed_static");
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->color_mismatch_dialog_) {
            ui_modal_hide(self->color_mismatch_dialog_);
            self->color_mismatch_dialog_ = nullptr;
        }
        // Execute print despite mismatch
        self->execute_print_start();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintSelectPanel::on_color_mismatch_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintSelectPanel] on_color_mismatch_cancel_static");
    auto* self = static_cast<PrintSelectPanel*>(lv_event_get_user_data(e));
    if (self && self->color_mismatch_dialog_) {
        ui_modal_hide(self->color_mismatch_dialog_);
        self->color_mismatch_dialog_ = nullptr;
        spdlog::debug("[PrintSelectPanel] Print cancelled by user (color mismatch warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}
