// SPDX-License-Identifier: GPL-3.0-or-later

#include "xml_hot_reloader.h"

#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <lvgl.h>

namespace fs = std::filesystem;

namespace helix {

XmlHotReloader::~XmlHotReloader() {
    stop();
}

void XmlHotReloader::start(const std::vector<std::string>& xml_dirs, int poll_interval_ms) {
    if (running_.load()) {
        return;
    }

    poll_interval_ms_ = poll_interval_ms;
    initial_scan(xml_dirs);

    spdlog::info("[HotReload] Watching {} XML files across {} directories (poll every {}ms)",
                 file_mtimes_.size(), xml_dirs.size(), poll_interval_ms_);

    running_.store(true);
    poll_thread_ = std::thread(&XmlHotReloader::poll_loop, this);
}

void XmlHotReloader::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    spdlog::debug("[HotReload] Stopped");
}

void XmlHotReloader::initial_scan(const std::vector<std::string>& xml_dirs) {
    file_mtimes_.clear();
    file_to_lvgl_path_.clear();

    for (const auto& dir : xml_dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            spdlog::warn("[HotReload] Directory not found: {}", dir);
            continue;
        }

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".xml")
                continue;

            auto abs_path = fs::absolute(entry.path()).string();
            auto mtime = entry.last_write_time();

            file_mtimes_[abs_path] = mtime;

            // Build the LVGL registration path ("A:ui_xml/filename.xml")
            // Use the relative path from the project root
            auto rel_path = entry.path().string();
            file_to_lvgl_path_[abs_path] = "A:" + rel_path;

            spdlog::trace("[HotReload] Tracking: {} ({})", rel_path,
                          component_name_from_path(entry.path()));
        }
    }
}

void XmlHotReloader::poll_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
        if (!running_.load())
            break;
        scan_and_reload();
    }
}

void XmlHotReloader::scan_and_reload() {
    for (auto& [abs_path, cached_mtime] : file_mtimes_) {
        std::error_code ec;
        auto current_mtime = fs::last_write_time(abs_path, ec);
        if (ec) {
            // File may have been deleted — skip silently
            continue;
        }

        if (current_mtime == cached_mtime) {
            continue;
        }

        // File changed!
        cached_mtime = current_mtime;

        auto comp_name = component_name_from_path(fs::path(abs_path));
        auto lvgl_path = file_to_lvgl_path_[abs_path];

        spdlog::info("[HotReload] Detected change: {} ({})", comp_name, abs_path);

        if (reload_callback_) {
            // Test mode — invoke callback directly instead of LVGL operations
            reload_callback_(comp_name, lvgl_path);
        } else {
            // Marshal the reload to the LVGL main thread
            auto reload_name = comp_name;
            auto reload_path = lvgl_path;
            helix::ui::queue_update([reload_name, reload_path]() {
                auto start = std::chrono::steady_clock::now();

                // Unregister old component definition
                auto result = lv_xml_component_unregister(reload_name.c_str());
                if (result != LV_RESULT_OK) {
                    spdlog::warn("[HotReload] Failed to unregister '{}' — registering fresh",
                                 reload_name);
                }

                // Re-register from the updated file
                result = lv_xml_register_component_from_file(reload_path.c_str());
                if (result != LV_RESULT_OK) {
                    spdlog::error("[HotReload] Failed to re-register '{}' from {}", reload_name,
                                  reload_path);
                    return;
                }

                auto elapsed = std::chrono::steady_clock::now() - start;
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
                spdlog::info("[HotReload] Reloaded: {} ({:.1f}ms)", reload_name, us / 1000.0);
            });
        }
    }
}

std::string XmlHotReloader::component_name_from_path(const fs::path& path) {
    // "home_panel.xml" -> "home_panel"
    return path.stem().string();
}

} // namespace helix
