// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#include "plugin_manager.h"

#include "helix_version.h"
#include "injection_point_manager.h"
#include "version.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>

#include "hv/json.hpp"

namespace fs = std::filesystem;

namespace helix::plugin {

// ============================================================================
// PluginManager Implementation
// ============================================================================

PluginManager::PluginManager() {
    spdlog::debug("[plugin] PluginManager created");
}

PluginManager::~PluginManager() {
    unload_all();
    spdlog::debug("[plugin] PluginManager destroyed");
}

// ============================================================================
// Configuration
// ============================================================================

void PluginManager::set_core_services(MoonrakerAPI* api, MoonrakerClient* client,
                                      PrinterState& state, Config* config) {
    moonraker_api_ = api;
    moonraker_client_ = client;
    printer_state_ = &state;
    config_ = config;
    spdlog::debug("[plugin] Core services set");
}

void PluginManager::set_enabled_plugins(const std::vector<std::string>& enabled_ids) {
    enabled_ids_ = enabled_ids;
    spdlog::debug("[plugin] Enabled plugins set: {} plugins", enabled_ids.size());
}

// ============================================================================
// Discovery
// ============================================================================

bool PluginManager::discover_plugins(const std::string& plugins_dir) {
    plugins_dir_ = plugins_dir;
    discovered_.clear();
    errors_.clear();

    if (!fs::exists(plugins_dir)) {
        spdlog::info("[plugin] Plugins directory does not exist: {}", plugins_dir);
        return true; // Not an error - just no plugins
    }

    if (!fs::is_directory(plugins_dir)) {
        spdlog::error("[plugin] Plugins path is not a directory: {}", plugins_dir);
        return false;
    }

    spdlog::info("[plugin] Discovering plugins in: {}", plugins_dir);

    int discovered_count = 0;
    int error_count = 0;

    for (const auto& entry : fs::directory_iterator(plugins_dir)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string plugin_dir = entry.path().string();
        std::string manifest_path = plugin_dir + "/manifest.json";

        if (!fs::exists(manifest_path)) {
            spdlog::debug("[plugin] No manifest.json in: {}", plugin_dir);
            continue;
        }

        PluginManifest manifest;
        std::string error_msg;

        if (!parse_manifest(manifest_path, manifest, error_msg)) {
            add_error(entry.path().filename().string(), PluginError::Type::MANIFEST_PARSE_ERROR,
                      error_msg);
            error_count++;
            continue;
        }

        if (!validate_manifest(manifest, error_msg)) {
            add_error(manifest.id, PluginError::Type::MANIFEST_MISSING_FIELD, error_msg);
            error_count++;
            continue;
        }

        // Check helix_version compatibility
        if (!manifest.helix_version.empty()) {
            if (!version::check_version_constraint(manifest.helix_version, HELIX_VERSION)) {
                add_error(manifest.id, PluginError::Type::VERSION_MISMATCH,
                          fmt::format("Requires HelixScreen {}, running {}", manifest.helix_version,
                                      HELIX_VERSION));
                error_count++;
                continue;
            }
            spdlog::debug("[plugin] {} version constraint {} satisfied by {}", manifest.id,
                          manifest.helix_version, HELIX_VERSION);
        }

        // Find library file
        std::string library_path = find_library(plugin_dir, manifest.id);
        if (library_path.empty()) {
            add_error(manifest.id, PluginError::Type::LIBRARY_NOT_FOUND,
                      "No .so/.dylib file found in plugin directory");
            error_count++;
            continue;
        }

        // Check if plugin is enabled
        bool enabled = enabled_ids_.empty() || std::find(enabled_ids_.begin(), enabled_ids_.end(),
                                                         manifest.id) != enabled_ids_.end();

        PluginInfo info;
        info.manifest = manifest;
        info.directory = plugin_dir;
        info.library_path = library_path;
        info.enabled = enabled;
        info.loaded = false;

        discovered_[manifest.id] = info;
        discovered_count++;

        spdlog::info("[plugin] Discovered: {} v{} ({})", manifest.name, manifest.version,
                     enabled ? "enabled" : "disabled");
    }

    spdlog::info("[plugin] Discovery complete: {} plugins found, {} errors", discovered_count,
                 error_count);

    return true;
}

// ============================================================================
// Loading
// ============================================================================

bool PluginManager::load_all() {
    if (printer_state_ == nullptr) {
        spdlog::error("[plugin] Cannot load plugins: core services not set");
        return false;
    }

    // Build load order respecting dependencies
    if (!build_load_order(load_order_)) {
        spdlog::error("[plugin] Failed to build load order (dependency cycle?)");
        return false;
    }

    spdlog::info("[plugin] Loading {} plugins in dependency order", load_order_.size());

    int loaded_count = 0;
    for (const auto& plugin_id : load_order_) {
        if (load_plugin_internal(plugin_id)) {
            loaded_count++;
        }
    }

    spdlog::info("[plugin] Loaded {} of {} plugins", loaded_count, load_order_.size());
    return loaded_count == static_cast<int>(load_order_.size());
}

bool PluginManager::load_plugin(const std::string& plugin_id) {
    // Check if already loaded
    if (loaded_.find(plugin_id) != loaded_.end()) {
        spdlog::warn("[plugin] Plugin already loaded: {}", plugin_id);
        return true;
    }

    return load_plugin_internal(plugin_id);
}

bool PluginManager::load_plugin_internal(const std::string& plugin_id) {
    auto it = discovered_.find(plugin_id);
    if (it == discovered_.end()) {
        add_error(plugin_id, PluginError::Type::LIBRARY_NOT_FOUND, "Plugin not discovered");
        return false;
    }

    PluginInfo& info = it->second;

    if (!info.enabled) {
        spdlog::debug("[plugin] Skipping disabled plugin: {}", plugin_id);
        return true; // Not an error
    }

    // Check dependencies are loaded
    for (const auto& dep_id : info.manifest.dependencies) {
        if (loaded_.find(dep_id) == loaded_.end()) {
            add_error(plugin_id, PluginError::Type::MISSING_DEPENDENCY,
                      "Missing dependency: " + dep_id);
            return false;
        }
    }

    spdlog::info("[plugin] Loading: {} from {}", plugin_id, info.library_path);

    // Load shared library
    void* handle = dlopen(info.library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        add_error(plugin_id, PluginError::Type::LOAD_FAILED,
                  error ? error : "Unknown dlopen error");
        return false;
    }

    // Look up entry point
    std::string entry_point =
        info.manifest.entry_point.empty() ? "helix_plugin_init" : info.manifest.entry_point;

    auto init_func = reinterpret_cast<PluginInitFunc>(dlsym(handle, entry_point.c_str()));
    if (init_func == nullptr) {
        const char* error = dlerror();
        dlclose(handle);
        add_error(plugin_id, PluginError::Type::SYMBOL_NOT_FOUND,
                  "Entry point not found: " + entry_point +
                      (error ? std::string(" (") + error + ")" : ""));
        return false;
    }

    // Look up deinit function (required)
    auto deinit_func = reinterpret_cast<PluginDeinitFunc>(dlsym(handle, "helix_plugin_deinit"));
    if (deinit_func == nullptr) {
        dlclose(handle);
        add_error(plugin_id, PluginError::Type::SYMBOL_NOT_FOUND, "helix_plugin_deinit not found");
        return false;
    }

    // Check API version (optional)
    auto version_func =
        reinterpret_cast<PluginApiVersionFunc>(dlsym(handle, "helix_plugin_api_version"));
    if (version_func != nullptr) {
        const char* plugin_version = version_func();
        if (plugin_version != nullptr && std::string(plugin_version) != PLUGIN_API_VERSION) {
            dlclose(handle);
            add_error(plugin_id, PluginError::Type::VERSION_MISMATCH,
                      std::string("API version mismatch: plugin requires ") + plugin_version +
                          ", host provides " + PLUGIN_API_VERSION);
            return false;
        }
    }

    // Create PluginAPI instance for this plugin
    auto api = std::make_unique<PluginAPI>(moonraker_api_, moonraker_client_, *printer_state_,
                                           config_, plugin_id);

    // Call plugin init
    bool init_result = init_func(api.get(), info.directory.c_str());
    if (!init_result) {
        dlclose(handle);
        add_error(plugin_id, PluginError::Type::INIT_FAILED, "Plugin init returned false");
        return false;
    }

    // Store loaded plugin state
    LoadedPlugin loaded;
    loaded.info = info;
    loaded.info.loaded = true;
    loaded.handle = handle;
    loaded.init_func = init_func;
    loaded.deinit_func = deinit_func;
    loaded.api = std::move(api);

    loaded_[plugin_id] = std::move(loaded);
    discovered_[plugin_id].loaded = true;

    spdlog::info("[plugin] Loaded successfully: {} v{}", info.manifest.name, info.manifest.version);
    return true;
}

// ============================================================================
// Unloading
// ============================================================================

void PluginManager::unload_all() {
    // Unload in reverse dependency order
    std::vector<std::string> unload_order = load_order_;
    std::reverse(unload_order.begin(), unload_order.end());

    spdlog::info("[plugin] Unloading {} plugins", loaded_.size());

    for (const auto& plugin_id : unload_order) {
        unload_plugin(plugin_id);
    }

    // Clear any remaining (shouldn't happen)
    loaded_.clear();
}

bool PluginManager::unload_plugin(const std::string& plugin_id) {
    auto it = loaded_.find(plugin_id);
    if (it == loaded_.end()) {
        return false;
    }

    LoadedPlugin& loaded = it->second;

    spdlog::info("[plugin] Unloading: {}", plugin_id);

    // Remove all UI widgets injected by this plugin
    InjectionPointManager::instance().remove_plugin_widgets(plugin_id);

    // Call deinit
    if (loaded.deinit_func != nullptr) {
        try {
            loaded.deinit_func();
        } catch (const std::exception& e) {
            spdlog::error("[plugin] Exception in deinit for {}: {}", plugin_id, e.what());
        }
    }

    // Cleanup API (unregisters services, subjects, etc.)
    if (loaded.api) {
        loaded.api->cleanup();
    }

    // Close library
    if (loaded.handle != nullptr) {
        dlclose(loaded.handle);
    }

    // Update discovered state
    if (discovered_.find(plugin_id) != discovered_.end()) {
        discovered_[plugin_id].loaded = false;
    }

    loaded_.erase(it);

    spdlog::info("[plugin] Unloaded: {}", plugin_id);
    return true;
}

// ============================================================================
// Moonraker Connection Events
// ============================================================================

void PluginManager::on_moonraker_connected() {
    spdlog::info("[plugin] Moonraker connected, applying deferred subscriptions");

    for (auto& [plugin_id, loaded] : loaded_) {
        if (loaded.api) {
            loaded.api->apply_deferred_subscriptions();
        }
    }
}

void PluginManager::on_moonraker_disconnected() {
    spdlog::info("[plugin] Moonraker disconnected");
    // Plugins may want to know about this - emit event
    EventDispatcher::instance().emit(events::PRINTER_DISCONNECTED);
}

void PluginManager::update_moonraker_services(MoonrakerAPI* api, MoonrakerClient* client) {
    moonraker_api_ = api;
    moonraker_client_ = client;

    for (auto& [plugin_id, loaded] : loaded_) {
        if (loaded.api) {
            loaded.api->set_moonraker(api, client);
        }
    }

    spdlog::debug("[plugin] Moonraker services updated for all plugins");
}

// ============================================================================
// Status Queries
// ============================================================================

std::vector<PluginInfo> PluginManager::get_discovered_plugins() const {
    std::vector<PluginInfo> result;
    result.reserve(discovered_.size());
    for (const auto& [id, info] : discovered_) {
        result.push_back(info);
    }
    return result;
}

std::vector<PluginInfo> PluginManager::get_loaded_plugins() const {
    std::vector<PluginInfo> result;
    result.reserve(loaded_.size());
    for (const auto& [id, loaded] : loaded_) {
        result.push_back(loaded.info);
    }
    return result;
}

std::vector<PluginError> PluginManager::get_load_errors() const {
    return errors_;
}

bool PluginManager::is_loaded(const std::string& plugin_id) const {
    return loaded_.find(plugin_id) != loaded_.end();
}

const PluginInfo* PluginManager::get_plugin(const std::string& plugin_id) const {
    auto it = discovered_.find(plugin_id);
    if (it != discovered_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool PluginManager::parse_manifest(const std::string& manifest_path, PluginManifest& manifest,
                                   std::string& error_msg) {
    try {
        std::ifstream file(manifest_path);
        if (!file.is_open()) {
            error_msg = "Failed to open manifest.json";
            return false;
        }

        json j = json::parse(file);

        // Required fields
        manifest.id = j.value("id", "");
        manifest.name = j.value("name", "");
        manifest.version = j.value("version", "");

        // Optional fields
        manifest.helix_version = j.value("helix_version", "");
        manifest.author = j.value("author", "");
        manifest.description = j.value("description", "");
        manifest.entry_point = j.value("entry_point", "helix_plugin_init");

        // Dependencies array
        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& dep : j["dependencies"]) {
                if (dep.is_string()) {
                    manifest.dependencies.push_back(dep.get<std::string>());
                }
            }
        }

        // UI config
        if (j.contains("ui") && j["ui"].is_object()) {
            const auto& ui = j["ui"];
            manifest.ui.settings_page = ui.value("settings_page", false);
            manifest.ui.navbar_panel = ui.value("navbar_panel", false);

            if (ui.contains("injection_points") && ui["injection_points"].is_array()) {
                for (const auto& point : ui["injection_points"]) {
                    if (point.is_string()) {
                        manifest.ui.injection_points.push_back(point.get<std::string>());
                    }
                }
            }
        }

        return true;

    } catch (const json::exception& e) {
        error_msg = std::string("JSON parse error: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        error_msg = std::string("Error reading manifest: ") + e.what();
        return false;
    }
}

bool PluginManager::validate_manifest(const PluginManifest& manifest, std::string& error_msg) {
    if (manifest.id.empty()) {
        error_msg = "Missing required field: id";
        return false;
    }

    if (manifest.name.empty()) {
        error_msg = "Missing required field: name";
        return false;
    }

    if (manifest.version.empty()) {
        error_msg = "Missing required field: version";
        return false;
    }

    // Validate ID format (alphanumeric + hyphens)
    for (char c : manifest.id) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            error_msg = "Invalid plugin ID: must be alphanumeric with hyphens/underscores";
            return false;
        }
    }

    return true;
}

bool PluginManager::build_load_order(std::vector<std::string>& load_order) {
    load_order.clear();

    // Collect enabled plugins
    std::set<std::string> enabled_set;
    for (const auto& [id, info] : discovered_) {
        if (info.enabled) {
            enabled_set.insert(id);
        }
    }

    // Build dependency graph (adjacency list)
    std::unordered_map<std::string, std::vector<std::string>> deps; // plugin -> dependencies
    std::unordered_map<std::string, int> in_degree;                 // plugin -> incoming edges

    for (const auto& id : enabled_set) {
        in_degree[id] = 0;
        deps[id] = {};
    }

    // Count incoming edges (dependencies)
    for (const auto& id : enabled_set) {
        const auto& info = discovered_.at(id);
        for (const auto& dep_id : info.manifest.dependencies) {
            if (enabled_set.find(dep_id) == enabled_set.end()) {
                // Dependency not in enabled set - might be missing
                if (discovered_.find(dep_id) == discovered_.end()) {
                    add_error(id, PluginError::Type::MISSING_DEPENDENCY,
                              "Dependency not found: " + dep_id);
                    // Continue anyway - will fail during load
                }
            }
            deps[id].push_back(dep_id);
            in_degree[id]++;
        }
    }

    // Kahn's algorithm for topological sort
    std::queue<std::string> ready;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) {
            ready.push(id);
        }
    }

    while (!ready.empty()) {
        std::string id = ready.front();
        ready.pop();
        load_order.push_back(id);

        // Reduce in-degree for plugins that depend on this one
        for (const auto& [other_id, other_deps] : deps) {
            if (std::find(other_deps.begin(), other_deps.end(), id) != other_deps.end()) {
                in_degree[other_id]--;
                if (in_degree[other_id] == 0) {
                    ready.push(other_id);
                }
            }
        }
    }

    // Check for cycles
    if (load_order.size() != enabled_set.size()) {
        // Find plugins involved in cycle
        for (const auto& id : enabled_set) {
            if (std::find(load_order.begin(), load_order.end(), id) == load_order.end()) {
                add_error(id, PluginError::Type::DEPENDENCY_CYCLE,
                          "Plugin involved in dependency cycle");
            }
        }
        return false;
    }

    // Log load order
    if (spdlog::should_log(spdlog::level::debug)) {
        std::string order_str;
        for (size_t i = 0; i < load_order.size(); ++i) {
            if (i > 0) {
                order_str += " -> ";
            }
            order_str += load_order[i];
        }
        spdlog::debug("[plugin] Load order: {}", order_str);
    }
    return true;
}

std::string PluginManager::find_library(const std::string& plugin_dir,
                                        const std::string& plugin_id) {
    // Try different naming conventions
    std::vector<std::string> candidates;

#ifdef __APPLE__
    candidates.push_back(plugin_dir + "/libhelix_" + plugin_id + ".dylib");
    candidates.push_back(plugin_dir + "/lib" + plugin_id + ".dylib");
    candidates.push_back(plugin_dir + "/" + plugin_id + ".dylib");
#else
    candidates.push_back(plugin_dir + "/libhelix_" + plugin_id + ".so");
    candidates.push_back(plugin_dir + "/lib" + plugin_id + ".so");
    candidates.push_back(plugin_dir + "/" + plugin_id + ".so");
#endif

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            return path;
        }
    }

    // Fall back to scanning directory for any .so/.dylib
    for (const auto& entry : fs::directory_iterator(plugin_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string ext = entry.path().extension().string();
#ifdef __APPLE__
        if (ext == ".dylib") {
            return entry.path().string();
        }
#else
        if (ext == ".so") {
            return entry.path().string();
        }
#endif
    }

    return "";
}

void PluginManager::add_error(const std::string& plugin_id, PluginError::Type type,
                              const std::string& msg) {
    errors_.push_back({plugin_id, msg, type});
    spdlog::error("[plugin] {}: {}", plugin_id, msg);
}

} // namespace helix::plugin
