// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#include "ui_settings_plugins.h"

#include "ui_icon.h"

#include "plugin_manager.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<SettingsPluginsOverlay> g_settings_plugins_overlay;

SettingsPluginsOverlay& get_settings_plugins_overlay() {
    if (!g_settings_plugins_overlay) {
        g_settings_plugins_overlay = std::make_unique<SettingsPluginsOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "SettingsPluginsOverlay", []() { g_settings_plugins_overlay.reset(); });
    }
    return *g_settings_plugins_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

SettingsPluginsOverlay::SettingsPluginsOverlay() {
    spdlog::trace("[{}] Constructor", get_name());
}

SettingsPluginsOverlay::~SettingsPluginsOverlay() {
    deinit_subjects();
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void SettingsPluginsOverlay::set_plugin_manager(helix::plugin::PluginManager* mgr) {
    plugin_manager_ = mgr;
}

// ============================================================================
// OVERLAYBASE IMPLEMENTATION
// ============================================================================

void SettingsPluginsOverlay::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize string subjects
    lv_subject_init_string(&plugins_status_title_subject_, status_title_buf_, nullptr,
                           sizeof(status_title_buf_), "No plugins");
    lv_xml_register_subject(nullptr, "plugins_status_title", &plugins_status_title_subject_);

    lv_subject_init_string(&plugins_status_detail_subject_, status_detail_buf_, nullptr,
                           sizeof(status_detail_buf_), "");
    lv_xml_register_subject(nullptr, "plugins_status_detail", &plugins_status_detail_subject_);

    // Initialize integer subjects for section visibility
    lv_subject_init_int(&plugins_total_count_subject_, 0);
    lv_xml_register_subject(nullptr, "plugins_total_count", &plugins_total_count_subject_);

    lv_subject_init_int(&plugins_loaded_count_subject_, 0);
    lv_xml_register_subject(nullptr, "plugins_loaded_count", &plugins_loaded_count_subject_);

    lv_subject_init_int(&plugins_disabled_count_subject_, 0);
    lv_xml_register_subject(nullptr, "plugins_disabled_count", &plugins_disabled_count_subject_);

    lv_subject_init_int(&plugins_failed_count_subject_, 0);
    lv_xml_register_subject(nullptr, "plugins_failed_count", &plugins_failed_count_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void SettingsPluginsOverlay::register_callbacks() {
    // No custom callbacks needed for this overlay
    // Back button uses default on_header_back_clicked from SettingsPanel
    spdlog::debug("[{}] Callbacks registered", get_name());
}

lv_obj_t* SettingsPluginsOverlay::create(lv_obj_t* parent) {
    if (!parent) {
        spdlog::error("[{}] NULL parent", get_name());
        return nullptr;
    }

    // Create overlay from XML
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "settings_plugins_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Cache widget references
    loaded_plugins_list_ = lv_obj_find_by_name(overlay_root_, "loaded_plugins_list");
    disabled_plugins_list_ = lv_obj_find_by_name(overlay_root_, "disabled_plugins_list");
    failed_plugins_list_ = lv_obj_find_by_name(overlay_root_, "failed_plugins_list");

    // Initially hidden
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void SettingsPluginsOverlay::on_activate() {
    OverlayBase::on_activate();
    refresh_plugin_list();
}

// ============================================================================
// INTERNAL METHODS
// ============================================================================

void SettingsPluginsOverlay::refresh_plugin_list() {
    spdlog::debug("[{}] Refreshing plugin list", get_name());

    // Clear existing lists
    if (loaded_plugins_list_) {
        lv_obj_clean(loaded_plugins_list_);
    }
    if (disabled_plugins_list_) {
        lv_obj_clean(disabled_plugins_list_);
    }
    if (failed_plugins_list_) {
        lv_obj_clean(failed_plugins_list_);
    }

    // Get plugin data from manager
    if (!plugin_manager_) {
        spdlog::warn("[{}] No plugin manager set", get_name());
        update_status(0, 0, 0);
        return;
    }

    auto discovered = plugin_manager_->get_discovered_plugins();
    auto errors = plugin_manager_->get_load_errors();

    // Build error map for quick lookup
    std::unordered_map<std::string, std::string> error_map;
    for (const auto& error : errors) {
        error_map[error.plugin_id] = error.message;
    }

    // Count by status
    int loaded_count = 0;
    int disabled_count = 0;
    int failed_count = 0;

    // Categorize and create cards
    for (const auto& plugin : discovered) {
        if (plugin.loaded) {
            // Loaded plugin
            if (loaded_plugins_list_) {
                create_plugin_card(loaded_plugins_list_, plugin);
            }
            loaded_count++;
        } else if (!plugin.enabled) {
            // Disabled plugin
            if (disabled_plugins_list_) {
                create_plugin_card(disabled_plugins_list_, plugin);
            }
            disabled_count++;
        } else {
            // Failed to load (enabled but not loaded)
            auto it = error_map.find(plugin.manifest.id);
            std::string error_msg = (it != error_map.end()) ? it->second : "Unknown error";
            if (failed_plugins_list_) {
                create_plugin_card(failed_plugins_list_, plugin, error_msg);
            }
            failed_count++;
        }
    }

    // Update status
    update_status(loaded_count, disabled_count, failed_count);

    spdlog::info("[{}] Plugin list refreshed: {} loaded, {} disabled, {} failed", get_name(),
                 loaded_count, disabled_count, failed_count);
}

void SettingsPluginsOverlay::create_plugin_card(lv_obj_t* parent,
                                                const helix::plugin::PluginInfo& info,
                                                const std::string& error_msg) {
    // Build attributes for XML component
    const char* attrs[] = {"plugin_name",
                           info.manifest.name.c_str(),
                           "plugin_version",
                           info.manifest.version.c_str(),
                           "plugin_author",
                           info.manifest.author.c_str(),
                           "plugin_description",
                           info.manifest.description.c_str(),
                           nullptr};

    auto* card = static_cast<lv_obj_t*>(lv_xml_create(parent, "plugin_card", attrs));
    if (!card) {
        spdlog::error("[{}] Failed to create plugin card for {}", get_name(), info.manifest.name);
        return;
    }

    // Show error container and update icon if there's an error message
    if (!error_msg.empty()) {
        lv_obj_t* error_container = lv_obj_find_by_name(card, "error_container");
        lv_obj_t* error_label = lv_obj_find_by_name(card, "error_label");
        if (error_container && error_label) {
            lv_obj_remove_flag(error_container, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(error_label, error_msg.c_str());
        }

        // Change plugin icon to alert icon for failed plugins
        lv_obj_t* plugin_icon = lv_obj_find_by_name(card, "plugin_icon");
        if (plugin_icon) {
            ui_icon_set_source(plugin_icon, "alert_circle");
            ui_icon_set_variant(plugin_icon, "error");
        }
    }

    spdlog::trace("[{}] Created card for plugin: {}", get_name(), info.manifest.name);
}

void SettingsPluginsOverlay::update_status(int loaded, int disabled, int failed) {
    int total = loaded + disabled + failed;

    // Update count subjects
    lv_subject_set_int(&plugins_total_count_subject_, total);
    lv_subject_set_int(&plugins_loaded_count_subject_, loaded);
    lv_subject_set_int(&plugins_disabled_count_subject_, disabled);
    lv_subject_set_int(&plugins_failed_count_subject_, failed);

    // Update status title
    if (total == 0) {
        snprintf(status_title_buf_, sizeof(status_title_buf_), "No plugins discovered");
    } else if (loaded == 1 && total == 1) {
        snprintf(status_title_buf_, sizeof(status_title_buf_), "1 plugin loaded");
    } else if (loaded == total) {
        snprintf(status_title_buf_, sizeof(status_title_buf_), "%d plugins loaded", loaded);
    } else {
        snprintf(status_title_buf_, sizeof(status_title_buf_), "%d of %d plugins loaded", loaded,
                 total);
    }
    lv_subject_copy_string(&plugins_status_title_subject_, status_title_buf_);

    // Update status detail
    if (total == 0) {
        snprintf(status_detail_buf_, sizeof(status_detail_buf_),
                 "Place plugins in the plugins directory");
    } else if (failed > 0) {
        snprintf(status_detail_buf_, sizeof(status_detail_buf_),
                 "%d failed to load - see details below", failed);
    } else if (disabled > 0) {
        snprintf(status_detail_buf_, sizeof(status_detail_buf_), "%d disabled", disabled);
    } else {
        snprintf(status_detail_buf_, sizeof(status_detail_buf_), "All plugins loaded successfully");
    }
    lv_subject_copy_string(&plugins_status_detail_subject_, status_detail_buf_);
}

void SettingsPluginsOverlay::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[{}] Deinitializing subjects", get_name());

    // Deinit all subjects
    lv_subject_deinit(&plugins_status_title_subject_);
    lv_subject_deinit(&plugins_status_detail_subject_);
    lv_subject_deinit(&plugins_total_count_subject_);
    lv_subject_deinit(&plugins_loaded_count_subject_);
    lv_subject_deinit(&plugins_disabled_count_subject_);
    lv_subject_deinit(&plugins_failed_count_subject_);

    subjects_initialized_ = false;
    spdlog::debug("[{}] Subjects deinitialized", get_name());
}
