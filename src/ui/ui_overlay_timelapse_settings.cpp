// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_timelapse_settings.h"

#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_fonts.h"
#include "ui_format_utils.h"
#include "ui_icon_codepoints.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "lvgl/src/xml/lv_xml.h"
#include "runtime_config.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "timelapse_state.h"

#include <spdlog/spdlog.h>

#include <cstring>

using helix::ui::format_file_size;

// Global instance and panel
static std::unique_ptr<TimelapseSettingsOverlay> g_timelapse_settings;
static lv_obj_t* g_timelapse_settings_panel = nullptr;

// Forward declaration for row click callback
static void on_timelapse_row_clicked(lv_event_t* e);

TimelapseSettingsOverlay& get_global_timelapse_settings() {
    if (!g_timelapse_settings) {
        spdlog::error(
            "[Timelapse Settings] get_global_timelapse_settings() called before initialization!");
        throw std::runtime_error("TimelapseSettingsOverlay not initialized");
    }
    return *g_timelapse_settings;
}

void init_global_timelapse_settings(MoonrakerAPI* api) {
    if (g_timelapse_settings) {
        spdlog::warn("[Timelapse Settings] TimelapseSettingsOverlay already initialized, skipping");
        return;
    }
    g_timelapse_settings = std::make_unique<TimelapseSettingsOverlay>(api);
    StaticPanelRegistry::instance().register_destroy("TimelapseSettingsOverlay",
                                                     []() { g_timelapse_settings.reset(); });
    spdlog::trace("[Timelapse Settings] TimelapseSettingsOverlay initialized");
}

// Framerate mapping
constexpr int TimelapseSettingsOverlay::FRAMERATE_VALUES[];

int TimelapseSettingsOverlay::framerate_to_index(int framerate) {
    for (int i = 0; i < FRAMERATE_COUNT; i++) {
        if (FRAMERATE_VALUES[i] == framerate) {
            return i;
        }
    }
    return 2; // Default to 30fps (index 2)
}

int TimelapseSettingsOverlay::index_to_framerate(int index) {
    if (index >= 0 && index < FRAMERATE_COUNT) {
        return FRAMERATE_VALUES[index];
    }
    return 30; // Default to 30fps
}

TimelapseSettingsOverlay::TimelapseSettingsOverlay(MoonrakerAPI* api) : api_(api) {
    spdlog::debug("[{}] Constructor", get_name());
}

void TimelapseSettingsOverlay::init_subjects() {
    spdlog::debug("[{}] init_subjects()", get_name());
}

lv_obj_t* TimelapseSettingsOverlay::create(lv_obj_t* parent) {
    // Create overlay root from XML
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, get_xml_component_name(), nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    spdlog::debug("[{}] create() - finding widgets", get_name());

    // Find row containers first, then get inner widgets
    // setting_toggle_row contains "toggle", setting_dropdown_row contains "dropdown"
    lv_obj_t* enable_row = lv_obj_find_by_name(overlay_root_, "row_timelapse_enable");
    lv_obj_t* mode_row = lv_obj_find_by_name(overlay_root_, "row_timelapse_mode");
    lv_obj_t* framerate_row = lv_obj_find_by_name(overlay_root_, "row_timelapse_framerate");
    lv_obj_t* autorender_row = lv_obj_find_by_name(overlay_root_, "row_timelapse_autorender");

    // Find inner widgets within rows
    if (enable_row) {
        enable_switch_ = lv_obj_find_by_name(enable_row, "toggle");
    }
    if (mode_row) {
        mode_dropdown_ = lv_obj_find_by_name(mode_row, "dropdown");
    }
    if (framerate_row) {
        framerate_dropdown_ = lv_obj_find_by_name(framerate_row, "dropdown");
    }
    if (autorender_row) {
        autorender_switch_ = lv_obj_find_by_name(autorender_row, "toggle");
    }

    // Mode info text is standalone
    mode_info_text_ = lv_obj_find_by_name(overlay_root_, "mode_info_text");

    // Log widget discovery
    spdlog::debug("[{}] Widgets found: enable={} mode={} info={} framerate={} autorender={}",
                  get_name(), enable_switch_ != nullptr, mode_dropdown_ != nullptr,
                  mode_info_text_ != nullptr, framerate_dropdown_ != nullptr,
                  autorender_switch_ != nullptr);

    // Video management widgets
    video_list_container_ = lv_obj_find_by_name(overlay_root_, "video_list_container");
    video_list_empty_ = lv_obj_find_by_name(overlay_root_, "video_list_empty");
    render_progress_container_ = lv_obj_find_by_name(overlay_root_, "render_progress_container");
    btn_render_now_ = lv_obj_find_by_name(overlay_root_, "btn_render_now");

    spdlog::debug("[{}] Video widgets found: list_container={} list_empty={} render_progress={} "
                  "btn_render={}",
                  get_name(), video_list_container_ != nullptr, video_list_empty_ != nullptr,
                  render_progress_container_ != nullptr, btn_render_now_ != nullptr);

    // Register event callbacks via XML system
    register_xml_callbacks({
        {"on_timelapse_row_clicked", on_timelapse_row_clicked},
        {"on_timelapse_enabled_changed", on_enabled_changed},
        {"on_timelapse_mode_changed", on_mode_changed},
        {"on_timelapse_framerate_changed", on_framerate_changed},
        {"on_timelapse_autorender_changed", on_autorender_changed},
        {"on_timelapse_render_now", on_render_now},
    });

    return overlay_root_;
}

void TimelapseSettingsOverlay::on_activate() {
    OverlayBase::on_activate();
    spdlog::debug("[{}] on_activate() - fetching current settings", get_name());
    fetch_settings();
    fetch_video_list();
}

void TimelapseSettingsOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
    spdlog::debug("[{}] on_deactivate()", get_name());
}

void TimelapseSettingsOverlay::cleanup() {
    spdlog::debug("[{}] cleanup()", get_name());
    clear_video_list();
    OverlayBase::cleanup();
}

void TimelapseSettingsOverlay::fetch_settings() {
    if (!api_) {
        spdlog::debug("[{}] No API available, using defaults", get_name());
        // Use defaults for test mode
        current_settings_ = TimelapseSettings{};
        settings_loaded_ = true;
        // Update UI with defaults
        if (enable_switch_) {
            lv_obj_remove_state(enable_switch_, LV_STATE_CHECKED);
        }
        if (mode_dropdown_) {
            lv_dropdown_set_selected(mode_dropdown_, 0); // Layer Macro
        }
        if (framerate_dropdown_) {
            lv_dropdown_set_selected(framerate_dropdown_, 2); // 30fps
        }
        if (autorender_switch_) {
            lv_obj_add_state(autorender_switch_, LV_STATE_CHECKED);
        }
        update_mode_info(0);
        return;
    }

    spdlog::debug("[{}] Fetching timelapse settings from API", get_name());

    api_->timelapse().get_timelapse_settings(
        [this](const TimelapseSettings& settings) {
            spdlog::info("[{}] Got timelapse settings: enabled={} mode={} fps={} autorender={}",
                         get_name(), settings.enabled, settings.mode, settings.output_framerate,
                         settings.autorender);

            current_settings_ = settings;
            settings_loaded_ = true;

            // Update UI on main thread
            if (enable_switch_) {
                if (settings.enabled) {
                    lv_obj_add_state(enable_switch_, LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(enable_switch_, LV_STATE_CHECKED);
                }
            }

            if (mode_dropdown_) {
                int mode_index = (settings.mode == "hyperlapse") ? 1 : 0;
                lv_dropdown_set_selected(mode_dropdown_, mode_index);
                update_mode_info(mode_index);
            }

            if (framerate_dropdown_) {
                int fps_index = framerate_to_index(settings.output_framerate);
                lv_dropdown_set_selected(framerate_dropdown_, fps_index);
            }

            if (autorender_switch_) {
                if (settings.autorender) {
                    lv_obj_add_state(autorender_switch_, LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(autorender_switch_, LV_STATE_CHECKED);
                }
            }
        },
        [this](const MoonrakerError& error) {
            spdlog::error("[{}] Failed to fetch timelapse settings: {}", get_name(), error.message);
            // Use defaults on error
            settings_loaded_ = false;
        });
}

void TimelapseSettingsOverlay::save_settings() {
    if (!api_) {
        spdlog::debug("[{}] No API available, not saving", get_name());
        return;
    }

    spdlog::debug("[{}] Saving timelapse settings: enabled={} mode={} fps={} autorender={}",
                  get_name(), current_settings_.enabled, current_settings_.mode,
                  current_settings_.output_framerate, current_settings_.autorender);

    api_->timelapse().set_timelapse_settings(
        current_settings_,
        [this]() { spdlog::info("[{}] Timelapse settings saved successfully", get_name()); },
        [this](const MoonrakerError& error) {
            spdlog::error("[{}] Failed to save timelapse settings: {}", get_name(), error.message);
        });
}

void TimelapseSettingsOverlay::update_mode_info(int mode_index) {
    if (!mode_info_text_) {
        return;
    }

    const char* info_text = (mode_index == 1)
                                ? "Hyperlapse captures frames at fixed time intervals. "
                                  "Good for very long prints."
                                : "Layer Macro captures one frame per layer change. "
                                  "Best for most prints.";

    lv_label_set_text(mode_info_text_, info_text);
}

// Static event handlers
void TimelapseSettingsOverlay::on_enabled_changed(lv_event_t* e) {
    if (!g_timelapse_settings) {
        return;
    }

    lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    spdlog::debug("[Timelapse Settings] Enable changed: {}", enabled);
    g_timelapse_settings->current_settings_.enabled = enabled;
    g_timelapse_settings->save_settings();
}

void TimelapseSettingsOverlay::on_mode_changed(lv_event_t* e) {
    if (!g_timelapse_settings) {
        return;
    }

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int index = lv_dropdown_get_selected(dropdown);

    const char* mode = (index == 1) ? "hyperlapse" : "layermacro";
    spdlog::debug("[Timelapse Settings] Mode changed: {} (index {})", mode, index);

    g_timelapse_settings->current_settings_.mode = mode;
    g_timelapse_settings->update_mode_info(index);
    g_timelapse_settings->save_settings();
}

void TimelapseSettingsOverlay::on_framerate_changed(lv_event_t* e) {
    if (!g_timelapse_settings) {
        return;
    }

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int index = lv_dropdown_get_selected(dropdown);
    int framerate = index_to_framerate(index);

    spdlog::debug("[Timelapse Settings] Framerate changed: {} fps (index {})", framerate, index);

    g_timelapse_settings->current_settings_.output_framerate = framerate;
    g_timelapse_settings->save_settings();
}

void TimelapseSettingsOverlay::on_autorender_changed(lv_event_t* e) {
    if (!g_timelapse_settings) {
        return;
    }

    lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool autorender = lv_obj_has_state(sw, LV_STATE_CHECKED);

    spdlog::debug("[Timelapse Settings] Autorender changed: {}", autorender);
    g_timelapse_settings->current_settings_.autorender = autorender;
    g_timelapse_settings->save_settings();
}

// ============================================================================
// Video Management
// ============================================================================

void TimelapseSettingsOverlay::fetch_video_list() {
    if (!api_) {
        spdlog::debug("[{}] No API available, skipping video list", get_name());
        // Show empty state in test mode
        if (video_list_empty_) {
            lv_obj_remove_flag(video_list_empty_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    api_->list_files(
        "timelapse", "", false,
        [this](const std::vector<FileInfo>& files) {
            helix::ui::queue_update([this, files]() { populate_video_list(files); });
        },
        [this](const MoonrakerError& error) {
            spdlog::error("[{}] Failed to fetch video list: {}", get_name(), error.message);
            helix::ui::queue_update([this]() {
                if (video_list_empty_) {
                    lv_obj_remove_flag(video_list_empty_, LV_OBJ_FLAG_HIDDEN);
                }
            });
        });
}

void TimelapseSettingsOverlay::populate_video_list(const std::vector<FileInfo>& files) {
    clear_video_list();

    // Filter for video files only
    std::vector<const FileInfo*> videos;
    for (const auto& f : files) {
        if (!f.is_dir) {
            const auto& name = f.filename;
            if (name.size() > 4) {
                auto ext = name.substr(name.size() - 4);
                if (ext == ".mp4" || ext == ".mkv" || ext == ".avi") {
                    videos.push_back(&f);
                }
            }
        }
    }

    if (videos.empty()) {
        if (video_list_empty_) {
            lv_obj_remove_flag(video_list_empty_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Hide empty state
    if (video_list_empty_) {
        lv_obj_add_flag(video_list_empty_, LV_OBJ_FLAG_HIDDEN);
    }

    if (!video_list_container_) {
        return;
    }

    // Get delete icon codepoint for buttons
    const char* delete_icon = ui_icon::lookup_codepoint("delete");

    for (const auto* file : videos) {
        // Create a row for each video (dynamic widget - exception to XML-only rule)
        lv_obj_t* row = lv_obj_create(video_list_container_);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_bg_color(row, theme_manager_get_color("card_bg"), 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);

        // Filename + size column
        lv_obj_t* info_col = lv_obj_create(row);
        lv_obj_set_flex_grow(info_col, 1);
        lv_obj_set_height(info_col, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(info_col, 0, 0);
        lv_obj_set_scroll_dir(info_col, LV_DIR_NONE);

        // Filename label
        lv_obj_t* name_label = lv_label_create(info_col);
        lv_label_set_text(name_label, file->filename.c_str());
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_label, lv_pct(100));

        // Size label
        lv_obj_t* size_label = lv_label_create(info_col);
        std::string size_str = format_file_size(file->size);
        lv_label_set_text(size_label, size_str.c_str());
        lv_obj_set_style_text_color(size_label, theme_manager_get_color("text_muted"), 0);

        // Delete button
        lv_obj_t* del_btn = lv_button_create(row);
        lv_obj_set_size(del_btn, 40, 40);
        lv_obj_set_style_bg_color(del_btn, theme_manager_get_color("error"), 0);
        lv_obj_set_style_bg_opa(del_btn, LV_OPA_20, 0);
        lv_obj_set_style_bg_opa(del_btn, LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_set_style_radius(del_btn, 20, 0);

        // Store filename as user data for delete callback
        char* filename_copy = static_cast<char*>(lv_malloc(file->filename.size() + 1));
        std::strcpy(filename_copy, file->filename.c_str());
        lv_obj_set_user_data(del_btn, filename_copy);

        // Free the heap-allocated filename when the button is destroyed.
        // Using LV_EVENT_DELETE ensures cleanup happens automatically via
        // lv_obj_clean(), avoiding the need to iterate children and blindly
        // free user_data (which risks freeing unrelated user_data on
        // non-button children like labels or containers).
        lv_obj_add_event_cb(
            del_btn,
            [](lv_event_t* e) {
                lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                void* ud = lv_obj_get_user_data(btn);
                if (ud) {
                    lv_free(ud);
                    lv_obj_set_user_data(btn, nullptr);
                }
            },
            LV_EVENT_DELETE, nullptr);

        // Delete icon
        lv_obj_t* del_icon = lv_label_create(del_btn);
        if (delete_icon) {
            lv_label_set_text(del_icon, delete_icon);
            lv_obj_set_style_text_font(del_icon, &mdi_icons_24, 0);
        } else {
            lv_label_set_text(del_icon, "X");
        }
        lv_obj_set_style_text_color(del_icon, theme_manager_get_color("error"), 0);
        lv_obj_center(del_icon);

        // Delete click handler (dynamic widget - exception to XML-only rule)
        lv_obj_add_event_cb(
            del_btn,
            [](lv_event_t* e) {
                lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                const char* filename = static_cast<const char*>(lv_obj_get_user_data(btn));
                if (!filename || !g_timelapse_settings)
                    return;

                g_timelapse_settings->pending_delete_filename_ = filename;

                g_timelapse_settings->delete_confirmation_dialog_ =
                    helix::ui::modal_show_confirmation(
                        "Delete Video?", filename, ModalSeverity::Warning, "Delete",
                        on_delete_video_confirmed, on_delete_video_cancelled, nullptr);
            },
            LV_EVENT_CLICKED, nullptr);
    }
}

void TimelapseSettingsOverlay::clear_video_list() {
    if (video_list_container_) {
        // Each delete button has an LV_EVENT_DELETE callback that frees its
        // heap-allocated filename user_data. lv_obj_clean() triggers DELETE
        // events on all descendants, so cleanup is automatic and safe.
        lv_obj_clean(video_list_container_);
    }
}

void TimelapseSettingsOverlay::on_render_now(lv_event_t* e) {
    (void)e;
    if (!g_timelapse_settings || !g_timelapse_settings->api_)
        return;

    spdlog::debug("[Timelapse Settings] Render Now clicked");
    g_timelapse_settings->api_->timelapse().render_timelapse(
        []() { spdlog::info("[Timelapse Settings] Render triggered successfully"); },
        [](const MoonrakerError& error) {
            spdlog::error("[Timelapse Settings] Failed to trigger render: {}", error.message);
            NOTIFY_ERROR("Failed to start timelapse render: {}", error.message);
        });
}

void TimelapseSettingsOverlay::on_delete_video_confirmed(lv_event_t* e) {
    (void)e;
    if (!g_timelapse_settings || !g_timelapse_settings->api_)
        return;

    std::string filename = g_timelapse_settings->pending_delete_filename_;
    if (filename.empty())
        return;

    std::string full_path = "timelapse/" + filename;
    spdlog::debug("[Timelapse Settings] Deleting video: {}", full_path);

    g_timelapse_settings->api_->delete_file(
        full_path,
        [filename]() {
            spdlog::info("[Timelapse Settings] Deleted video: {}", filename);
            // Refresh the list
            if (g_timelapse_settings) {
                g_timelapse_settings->fetch_video_list();
            }
        },
        [filename](const MoonrakerError& error) {
            spdlog::error("[Timelapse Settings] Failed to delete {}: {}", filename, error.message);
            NOTIFY_ERROR("Failed to delete video: {}", error.message);
        });

    g_timelapse_settings->pending_delete_filename_.clear();
    g_timelapse_settings->delete_confirmation_dialog_ = nullptr;
}

void TimelapseSettingsOverlay::on_delete_video_cancelled(lv_event_t* e) {
    (void)e;
    if (!g_timelapse_settings)
        return;
    g_timelapse_settings->pending_delete_filename_.clear();
    g_timelapse_settings->delete_confirmation_dialog_ = nullptr;
}

// ============================================================================
// Row Click Callback (opens this overlay from Advanced panel)
// ============================================================================

static void on_timelapse_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Timelapse Settings] Timelapse row clicked");

    if (!g_timelapse_settings) {
        spdlog::error("[Timelapse Settings] Global instance not initialized!");
        return;
    }

    // Lazy-create the timelapse settings panel using OverlayBase::create()
    if (!g_timelapse_settings_panel) {
        spdlog::debug("[Timelapse Settings] Creating timelapse settings panel...");
        g_timelapse_settings_panel =
            g_timelapse_settings->create(lv_display_get_screen_active(nullptr));

        if (g_timelapse_settings_panel) {
            // Register with NavigationManager for lifecycle callbacks
            NavigationManager::instance().register_overlay_instance(g_timelapse_settings_panel,
                                                                    g_timelapse_settings.get());
            spdlog::debug("[Timelapse Settings] Panel created and registered");
        } else {
            spdlog::error("[Timelapse Settings] Failed to create timelapse_settings_overlay");
            return;
        }
    }

    // Show the overlay - NavigationManager will call on_activate()
    NavigationManager::instance().push_overlay(g_timelapse_settings_panel);
}
