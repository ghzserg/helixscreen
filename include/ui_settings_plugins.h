// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

/**
 * @file ui_settings_plugins.h
 * @brief Settings Plugins overlay - displays all discovered plugins
 *
 * Shows plugin information organized by status:
 * - Loaded: Successfully initialized plugins
 * - Disabled: Discovered but not enabled plugins
 * - Failed: Plugins that failed to load
 *
 * @pattern Overlay (two-phase init: init_subjects -> create -> callbacks)
 * @threading Main thread only
 *
 * @see PluginManager for plugin lifecycle
 * @see OverlayBase for base class documentation
 */

#pragma once

#include "overlay_base.h"

#include <string>

namespace helix::plugin {
class PluginManager;
struct PluginInfo;
struct PluginError;
} // namespace helix::plugin

/**
 * @class SettingsPluginsOverlay
 * @brief Overlay panel showing all discovered plugins and their status
 *
 * This overlay displays plugins organized by status:
 * - Loaded plugins with green indicator
 * - Disabled plugins with yellow indicator
 * - Failed plugins with red indicator and error details
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = get_settings_plugins_overlay();
 * overlay.set_plugin_manager(mgr);
 * overlay.init_subjects();
 * overlay.register_callbacks();
 * overlay.create(parent_screen);
 * ui_nav_push_overlay(overlay.get_root());
 * @endcode
 */
class SettingsPluginsOverlay : public OverlayBase {
  public:
    /**
     * @brief Default constructor
     */
    SettingsPluginsOverlay();

    /**
     * @brief Destructor - cleans up subjects
     */
    ~SettingsPluginsOverlay() override;

    // Non-copyable
    SettingsPluginsOverlay(const SettingsPluginsOverlay&) = delete;
    SettingsPluginsOverlay& operator=(const SettingsPluginsOverlay&) = delete;

    //
    // === Configuration ===
    //

    /**
     * @brief Set the plugin manager to query for plugin info
     *
     * @param mgr Pointer to PluginManager (may be nullptr)
     */
    void set_plugin_manager(helix::plugin::PluginManager* mgr);

    //
    // === OverlayBase Implementation ===
    //

    /**
     * @brief Initialize LVGL subjects for XML data binding
     *
     * Creates subjects for:
     * - plugins_status_title: "X plugins loaded"
     * - plugins_status_detail: Detailed count breakdown
     * - plugins_total_count: Total discovered plugins
     * - plugins_loaded_count: Successfully loaded plugins
     * - plugins_disabled_count: Discovered but disabled plugins
     * - plugins_failed_count: Failed to load plugins
     */
    void init_subjects() override;

    /**
     * @brief Register event callbacks with lv_xml system
     */
    void register_callbacks() override;

    /**
     * @brief Create overlay UI from XML
     *
     * @param parent Parent widget to attach overlay to (usually screen)
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     */
    const char* get_name() const override {
        return "Settings Plugins";
    }

    //
    // === Lifecycle Hooks ===
    //

    /**
     * @brief Called when overlay becomes visible
     *
     * Refreshes the plugin list from PluginManager.
     */
    void on_activate() override;

  private:
    //
    // === Internal Methods ===
    //

    /**
     * @brief Refresh the plugin list from PluginManager
     *
     * Updates all sections (loaded, disabled, failed) and status text.
     */
    void refresh_plugin_list();

    /**
     * @brief Create a plugin card widget
     *
     * @param parent Parent container for the card
     * @param info Plugin info to display
     * @param error_msg Error message (for failed plugins, empty otherwise)
     */
    void create_plugin_card(lv_obj_t* parent, const helix::plugin::PluginInfo& info,
                            const std::string& error_msg = "");

    /**
     * @brief Update status card subjects
     *
     * @param loaded Number of loaded plugins
     * @param disabled Number of disabled plugins
     * @param failed Number of failed plugins
     */
    void update_status(int loaded, int disabled, int failed);

    /**
     * @brief Deinitialize subjects for clean shutdown
     */
    void deinit_subjects();

    //
    // === Plugin Manager Reference ===
    //

    helix::plugin::PluginManager* plugin_manager_ = nullptr;

    //
    // === LVGL Subjects ===
    //

    lv_subject_t plugins_status_title_subject_;
    lv_subject_t plugins_status_detail_subject_;
    lv_subject_t plugins_total_count_subject_;
    lv_subject_t plugins_loaded_count_subject_;
    lv_subject_t plugins_disabled_count_subject_;
    lv_subject_t plugins_failed_count_subject_;

    // String buffers for subject values
    char status_title_buf_[64];
    char status_detail_buf_[128];

    //
    // === Widget References ===
    //

    lv_obj_t* loaded_plugins_list_ = nullptr;
    lv_obj_t* disabled_plugins_list_ = nullptr;
    lv_obj_t* failed_plugins_list_ = nullptr;
};

/**
 * @brief Global instance accessor
 *
 * @return Reference to singleton SettingsPluginsOverlay
 */
SettingsPluginsOverlay& get_settings_plugins_overlay();
