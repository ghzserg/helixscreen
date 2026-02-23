// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class XmlHotReloaderTestAccess;

namespace helix {

/**
 * @brief Hot-reloads XML components when files change on disk
 *
 * Polls ui_xml/ directories for mtime changes and re-registers modified
 * components via lv_xml_component_unregister + lv_xml_register_component_from_file.
 * Only active when HELIX_HOT_RELOAD=1 environment variable is set.
 *
 * @note Only tracks files present at start() time. Newly created XML files
 *       are not detected — restart the app to pick them up.
 *
 * @threading Polling runs on a dedicated background thread; actual LVGL
 *            operations are marshalled to the main thread via queue_update().
 */
class XmlHotReloader {
  public:
    XmlHotReloader() = default;
    ~XmlHotReloader();

    // Non-copyable
    XmlHotReloader(const XmlHotReloader&) = delete;
    XmlHotReloader& operator=(const XmlHotReloader&) = delete;

    /**
     * @brief Start watching the given directories for XML changes
     * @param xml_dirs Directories to watch (e.g., {"ui_xml", "ui_xml/components"})
     * @param poll_interval_ms How often to check for changes (default 500ms)
     */
    void start(const std::vector<std::string>& xml_dirs, int poll_interval_ms = 500);

    /**
     * @brief Stop the polling thread (blocks until joined)
     */
    void stop();

    /**
     * @brief Check all tracked files for changes and reload any that changed
     *
     * Normally called by the polling thread, but can be called manually for testing.
     */
    void scan_and_reload();

    /// Number of XML files currently being tracked
    /// @note Only safe to call when polling is not running (before start() or after stop())
    size_t tracked_file_count() const {
        return file_mtimes_.size();
    }

    /// Check if a specific absolute path is being tracked
    /// @note Only safe to call when polling is not running (before start() or after stop())
    bool is_tracking(const std::string& abs_path) const {
        return file_mtimes_.count(abs_path) > 0;
    }

    /// Check if the polling thread is running
    bool is_running() const {
        return running_.load();
    }

    /// Callback type for reload events: (component_name, lvgl_path)
    using ReloadCallback = std::function<void(const std::string&, const std::string&)>;

    /// Set a custom reload callback (for testing). If set, bypasses LVGL queue_update.
    void set_reload_callback(ReloadCallback cb) {
        reload_callback_ = std::move(cb);
    }

  private:
    friend class ::XmlHotReloaderTestAccess;
    void poll_loop();
    void initial_scan(const std::vector<std::string>& xml_dirs);

    /// Derive LVGL component name from filename (strip .xml extension)
    static std::string component_name_from_path(const std::filesystem::path& path);

    std::thread poll_thread_;
    std::atomic<bool> running_{false};
    int poll_interval_ms_{500};

    /// Map: absolute file path -> last modification time
    std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;

    /// Map: absolute file path -> LVGL registration path ("A:ui_xml/...")
    std::unordered_map<std::string, std::string> file_to_lvgl_path_;

    /// Optional test callback — if set, called instead of LVGL unregister/register
    ReloadCallback reload_callback_;
};

} // namespace helix
