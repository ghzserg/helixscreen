// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_plugin_install_modal.h"

#include "ui_event_safety.h"
#include "ui_theme.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#ifdef HELIX_DISPLAY_SDL
#include <SDL.h>
#endif
#include <thread>

// ============================================================================
// Configuration
// ============================================================================

void PluginInstallModal::set_installer(helix::HelixPluginInstaller* installer) {
    installer_ = installer;
}

void PluginInstallModal::set_on_install_complete(InstallCompleteCallback cb) {
    on_install_complete_cb_ = std::move(cb);
}

// ============================================================================
// Lifecycle Hooks
// ============================================================================

void PluginInstallModal::on_show() {
    // Register custom event callbacks
    lv_xml_register_event_cb(nullptr, "on_plugin_install_clicked", install_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_plugin_copy_clicked", copy_clicked_cb);

    // Find widgets
    local_description_ = find_widget("local_description");
    remote_description_ = find_widget("remote_description");
    command_textarea_ = find_widget("command_textarea");
    local_button_row_ = find_widget("local_button_row");
    remote_button_row_ = find_widget("remote_button_row");
    result_button_row_ = find_widget("result_button_row");
    installing_container_ = find_widget("installing_container");
    result_container_ = find_widget("result_container");
    checkbox_container_ = find_widget("checkbox_container");
    dont_ask_checkbox_ = find_widget("dont_ask_checkbox");
    copy_feedback_ = find_widget("copy_feedback");

    // Determine mode based on installer
    is_local_mode_ = installer_ && installer_->is_local_moonraker();

    spdlog::info("[Plugin Install] Showing in {} mode", is_local_mode_ ? "LOCAL" : "REMOTE");

    if (is_local_mode_) {
        // LOCAL mode: Show install button
        if (local_description_)
            lv_obj_remove_flag(local_description_, LV_OBJ_FLAG_HIDDEN);
        if (remote_description_)
            lv_obj_add_flag(remote_description_, LV_OBJ_FLAG_HIDDEN);
        if (local_button_row_)
            lv_obj_remove_flag(local_button_row_, LV_OBJ_FLAG_HIDDEN);
        if (remote_button_row_)
            lv_obj_add_flag(remote_button_row_, LV_OBJ_FLAG_HIDDEN);
    } else {
        // REMOTE mode: Show curl command
        if (local_description_)
            lv_obj_add_flag(local_description_, LV_OBJ_FLAG_HIDDEN);
        if (remote_description_)
            lv_obj_remove_flag(remote_description_, LV_OBJ_FLAG_HIDDEN);
        if (local_button_row_)
            lv_obj_add_flag(local_button_row_, LV_OBJ_FLAG_HIDDEN);
        if (remote_button_row_)
            lv_obj_remove_flag(remote_button_row_, LV_OBJ_FLAG_HIDDEN);

        // Populate the curl command
        if (command_textarea_ && installer_) {
            std::string cmd = installer_->get_remote_install_command();
            lv_textarea_set_text(command_textarea_, cmd.c_str());
        }

        // Reset copy feedback from previous show
        if (copy_feedback_) {
            lv_label_set_text(copy_feedback_, "");
            lv_obj_add_flag(copy_feedback_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Wire cancel button and set user_data for all buttons
    wire_cancel_button("btn_cancel");
    wire_cancel_button("btn_done");
    wire_ok_button("btn_ok");

    // Set user_data for custom buttons
    lv_obj_t* btn_install = find_widget("btn_install");
    if (btn_install) {
        lv_obj_set_user_data(btn_install, this);
    }

    lv_obj_t* btn_copy = find_widget("btn_copy");
    if (btn_copy) {
        lv_obj_set_user_data(btn_copy, this);
    }
}

void PluginInstallModal::on_hide() {
    // Clear widget references
    local_description_ = nullptr;
    remote_description_ = nullptr;
    command_textarea_ = nullptr;
    local_button_row_ = nullptr;
    remote_button_row_ = nullptr;
    result_button_row_ = nullptr;
    installing_container_ = nullptr;
    result_container_ = nullptr;
    checkbox_container_ = nullptr;
    dont_ask_checkbox_ = nullptr;
    copy_feedback_ = nullptr;
}

void PluginInstallModal::on_cancel() {
    check_dont_ask_preference();
    hide();
}

// ============================================================================
// UI State Management
// ============================================================================

void PluginInstallModal::show_installing_state() {
    // Hide all content except installing spinner
    if (local_description_)
        lv_obj_add_flag(local_description_, LV_OBJ_FLAG_HIDDEN);
    if (remote_description_)
        lv_obj_add_flag(remote_description_, LV_OBJ_FLAG_HIDDEN);
    if (local_button_row_)
        lv_obj_add_flag(local_button_row_, LV_OBJ_FLAG_HIDDEN);
    if (remote_button_row_)
        lv_obj_add_flag(remote_button_row_, LV_OBJ_FLAG_HIDDEN);
    if (checkbox_container_)
        lv_obj_add_flag(checkbox_container_, LV_OBJ_FLAG_HIDDEN);

    // Show installing container
    if (installing_container_)
        lv_obj_remove_flag(installing_container_, LV_OBJ_FLAG_HIDDEN);
}

void PluginInstallModal::show_result_state(bool success, const std::string& message) {
    // Hide installing spinner
    if (installing_container_)
        lv_obj_add_flag(installing_container_, LV_OBJ_FLAG_HIDDEN);

    // Show result container
    if (result_container_)
        lv_obj_remove_flag(result_container_, LV_OBJ_FLAG_HIDDEN);

    // Show OK button row
    if (result_button_row_)
        lv_obj_remove_flag(result_button_row_, LV_OBJ_FLAG_HIDDEN);

    // Update result content
    lv_obj_t* result_icon = find_widget("result_icon");
    lv_obj_t* result_title = find_widget("result_title");
    lv_obj_t* result_message = find_widget("result_message");

    if (success) {
        if (result_icon) {
            lv_image_set_src(result_icon, "check_circle");
            lv_obj_set_style_image_recolor(result_icon, ui_theme_get_color("success_color"),
                                           LV_PART_MAIN);
        }
        if (result_title) {
            lv_label_set_text(result_title, "Success!");
        }
    } else {
        if (result_icon) {
            lv_image_set_src(result_icon, "alert_circle");
            lv_obj_set_style_image_recolor(result_icon, ui_theme_get_color("error_color"),
                                           LV_PART_MAIN);
        }
        if (result_title) {
            lv_label_set_text(result_title, "Installation Failed");
        }
    }

    if (result_message) {
        lv_label_set_text(result_message, message.c_str());
    }
}

void PluginInstallModal::check_dont_ask_preference() {
    if (dont_ask_checkbox_ && installer_) {
        bool checked = lv_obj_has_state(dont_ask_checkbox_, LV_STATE_CHECKED);
        if (checked) {
            spdlog::info("[Plugin Install] User selected 'Don't ask again'");
            installer_->set_install_declined();
        }
    }
}

// ============================================================================
// Button Handlers
// ============================================================================

void PluginInstallModal::on_install_clicked() {
    if (!installer_) {
        spdlog::error("[Plugin Install] No installer set");
        return;
    }

    spdlog::info("[Plugin Install] Starting local installation (async)");
    show_installing_state();

    // Capture state for async callback
    // We capture a pointer to is_destroying_ to detect if the modal was destroyed
    // before the async callback executes. This prevents use-after-free.
    auto* self = this;
    auto* installer = installer_;
    auto callback = on_install_complete_cb_;
    auto* destroying_flag = &is_destroying_;

    // Run installation in background thread to keep UI responsive
    std::thread install_thread([self, installer, callback, destroying_flag]() {
        bool success = false;
        std::string message;

        // Run blocking installation in background
        installer->install_local([&success, &message](bool s, const std::string& m) {
            success = s;
            message = m;
        });

        spdlog::info("[Plugin Install] Installation result: {} - {}",
                     success ? "success" : "failed", message);

        // Allocate result data for async callback (will be freed in callback)
        struct InstallResult {
            PluginInstallModal* modal;
            std::atomic<bool>* destroying_flag;
            bool success;
            std::string message;
            InstallCompleteCallback callback;
        };
        auto* result = new InstallResult{self, destroying_flag, success, message, callback};

        // Schedule UI update on main LVGL thread
        ui_async_call(
            [](void* user_data) {
                auto* r = static_cast<InstallResult*>(user_data);

                // Guard: check if modal was destroyed before we got here
                if (r->destroying_flag->load()) {
                    spdlog::debug(
                        "[Plugin Install] Modal destroyed before result - skipping UI update");
                    delete r;
                    return;
                }

                // Guard: only update UI if modal is still visible
                if (r->modal->is_visible()) {
                    r->modal->show_result_state(r->success, r->message);
                    r->modal->check_dont_ask_preference();

                    if (r->callback) {
                        r->callback(r->success);
                    }
                } else {
                    spdlog::debug(
                        "[Plugin Install] Modal hidden before result - skipping UI update");
                }

                delete r;
            },
            result);
    });

    // Detach thread - it will clean up via lv_async_call when done
    install_thread.detach();
}

void PluginInstallModal::on_copy_clicked() {
    if (!command_textarea_) {
        return;
    }

    // Get the command text
    const char* cmd = lv_textarea_get_text(command_textarea_);
    if (!cmd || strlen(cmd) == 0) {
        return;
    }

    spdlog::info("[Plugin Install] Copying command to clipboard");

#ifdef HELIX_DISPLAY_SDL
    // Use SDL's cross-platform clipboard API (safe, no shell injection)
    int result = SDL_SetClipboardText(cmd);

    if (copy_feedback_) {
        if (result == 0) {
            lv_label_set_text(copy_feedback_, "Copied to clipboard!");
            spdlog::debug("[Plugin Install] Command copied successfully");
        } else {
            lv_label_set_text(copy_feedback_, "Copy failed - use SSH manually");
            spdlog::warn("[Plugin Install] SDL clipboard failed: {}", SDL_GetError());
        }
        lv_obj_remove_flag(copy_feedback_, LV_OBJ_FLAG_HIDDEN);
    }
#else
    // No clipboard support on framebuffer displays
    if (copy_feedback_) {
        lv_label_set_text(copy_feedback_, "Clipboard unavailable - use SSH");
        lv_obj_remove_flag(copy_feedback_, LV_OBJ_FLAG_HIDDEN);
    }
    spdlog::info("[Plugin Install] Clipboard not available on this platform");
#endif
}

// ============================================================================
// Static Event Handlers
// ============================================================================

void PluginInstallModal::install_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PluginInstallModal] install_clicked_cb");

    auto* self = static_cast<PluginInstallModal*>(lv_event_get_user_data(e));
    if (self) {
        self->on_install_clicked();
    }

    LVGL_SAFE_EVENT_CB_END();
}

void PluginInstallModal::copy_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PluginInstallModal] copy_clicked_cb");

    auto* self = static_cast<PluginInstallModal*>(lv_event_get_user_data(e));
    if (self) {
        self->on_copy_clicked();
    }

    LVGL_SAFE_EVENT_CB_END();
}
