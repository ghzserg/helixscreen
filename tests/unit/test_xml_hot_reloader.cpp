// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_xml_hot_reloader.cpp
 * @brief Tests for XmlHotReloader — file scanning, mtime tracking, change detection
 *
 * Uses temp directories with real XML files to test the polling/detection logic
 * without needing LVGL initialized. The reload callback injection lets us verify
 * that changes are detected and the correct component names are derived.
 */

#include "xml_hot_reloader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;

// ============================================================================
// Test access helper
// ============================================================================

class XmlHotReloaderTestAccess {
  public:
    static std::string component_name_from_path(const fs::path& path) {
        return helix::XmlHotReloader::component_name_from_path(path);
    }

    static const auto& file_mtimes(const helix::XmlHotReloader& hr) {
        return hr.file_mtimes_;
    }

    static const auto& file_to_lvgl_path(const helix::XmlHotReloader& hr) {
        return hr.file_to_lvgl_path_;
    }
};

// ============================================================================
// Fixture: temp directory with XML files
// ============================================================================

class HotReloadFixture {
  public:
    HotReloadFixture() {
        temp_dir_ = fs::temp_directory_path() /
                    ("helix_hot_reload_test_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
        sub_dir_ = temp_dir_ / "components";
        fs::create_directories(sub_dir_);
    }

    ~HotReloadFixture() {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    /// Create a minimal XML file in the temp directory
    void create_xml(const std::string& filename, const std::string& content = "<component/>") {
        std::ofstream f(temp_dir_ / filename);
        f << content;
    }

    /// Create a minimal XML file in the components subdirectory
    void create_sub_xml(const std::string& filename, const std::string& content = "<component/>") {
        std::ofstream f(sub_dir_ / filename);
        f << content;
    }

    /// Touch a file to update its mtime (write same content)
    void touch_xml(const std::string& filename) {
        auto path = temp_dir_ / filename;
        if (!fs::exists(path)) {
            path = sub_dir_ / filename;
        }
        // Ensure mtime actually changes (some filesystems have 1s granularity)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ofstream f(path, std::ios::app);
        f << " ";
    }

    fs::path temp_dir_;
    fs::path sub_dir_;
};

// ============================================================================
// component_name_from_path [hot-reload]
// ============================================================================

TEST_CASE("component_name_from_path strips .xml extension", "[hot-reload]") {
    REQUIRE(XmlHotReloaderTestAccess::component_name_from_path("home_panel.xml") == "home_panel");
    REQUIRE(XmlHotReloaderTestAccess::component_name_from_path("settings_panel.xml") ==
            "settings_panel");
}

TEST_CASE("component_name_from_path handles paths with directories", "[hot-reload]") {
    REQUIRE(XmlHotReloaderTestAccess::component_name_from_path("ui_xml/home_panel.xml") ==
            "home_panel");
    REQUIRE(XmlHotReloaderTestAccess::component_name_from_path(
                "/abs/path/ui_xml/components/nozzle_icon.xml") == "nozzle_icon");
}

TEST_CASE("component_name_from_path handles file without extension", "[hot-reload]") {
    REQUIRE(XmlHotReloaderTestAccess::component_name_from_path("no_extension") == "no_extension");
}

// ============================================================================
// Initial scan [hot-reload]
// ============================================================================

TEST_CASE_METHOD(HotReloadFixture, "initial scan finds XML files in directory", "[hot-reload]") {
    create_xml("panel_a.xml");
    create_xml("panel_b.xml");
    create_xml("panel_c.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);

    REQUIRE(hr.tracked_file_count() == 3);

    hr.stop();
}

TEST_CASE_METHOD(HotReloadFixture, "initial scan ignores non-XML files", "[hot-reload]") {
    create_xml("panel.xml");
    // Create non-XML files
    {
        std::ofstream f(temp_dir_ / "readme.md");
        f << "# readme";
    }
    {
        std::ofstream f(temp_dir_ / "data.json");
        f << "{}";
    }
    {
        std::ofstream f(temp_dir_ / "image.png");
        f << "\x89PNG";
    }

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);

    REQUIRE(hr.tracked_file_count() == 1);

    hr.stop();
}

TEST_CASE_METHOD(HotReloadFixture, "initial scan handles multiple directories", "[hot-reload]") {
    create_xml("top_level.xml");
    create_sub_xml("component.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string(), sub_dir_.string()}, 50);

    REQUIRE(hr.tracked_file_count() == 2);

    hr.stop();
}

TEST_CASE_METHOD(HotReloadFixture, "initial scan handles non-existent directory gracefully",
                 "[hot-reload]") {
    create_xml("panel.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string(), "/tmp/nonexistent_dir_12345"}, 50);

    // Should still track the one file from the valid directory
    REQUIRE(hr.tracked_file_count() == 1);

    hr.stop();
}

TEST_CASE_METHOD(HotReloadFixture, "initial scan with empty directory tracks zero files",
                 "[hot-reload]") {
    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);

    REQUIRE(hr.tracked_file_count() == 0);

    hr.stop();
}

// ============================================================================
// LVGL path mapping [hot-reload]
// ============================================================================

TEST_CASE_METHOD(HotReloadFixture, "file_to_lvgl_path maps to A: prefixed relative path",
                 "[hot-reload]") {
    create_xml("test_widget.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);

    auto& paths = XmlHotReloaderTestAccess::file_to_lvgl_path(hr);
    REQUIRE(paths.size() == 1);

    // The LVGL path should start with "A:" and contain the filename
    auto& [abs_path, lvgl_path] = *paths.begin();
    REQUIRE(lvgl_path.substr(0, 2) == "A:");
    REQUIRE(lvgl_path.find("test_widget.xml") != std::string::npos);

    hr.stop();
}

// ============================================================================
// Start/stop lifecycle [hot-reload]
// ============================================================================

TEST_CASE_METHOD(HotReloadFixture, "stop without start is safe", "[hot-reload]") {
    helix::XmlHotReloader hr;
    hr.stop(); // Should not crash or hang
    REQUIRE(hr.is_running() == false);
}

TEST_CASE_METHOD(HotReloadFixture, "double stop is safe", "[hot-reload]") {
    create_xml("panel.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);
    REQUIRE(hr.is_running() == true);

    hr.stop();
    REQUIRE(hr.is_running() == false);

    hr.stop(); // Second stop should be no-op
    REQUIRE(hr.is_running() == false);
}

TEST_CASE_METHOD(HotReloadFixture, "double start is ignored", "[hot-reload]") {
    create_xml("panel.xml");

    helix::XmlHotReloader hr;
    hr.set_reload_callback([](const std::string&, const std::string&) {});
    hr.start({temp_dir_.string()}, 50);

    auto count = hr.tracked_file_count();
    REQUIRE(count == 1);

    // Create another file and try to start again — should be no-op
    create_xml("panel2.xml");
    hr.start({temp_dir_.string()}, 50);

    // Should still track only the original file (second start ignored)
    REQUIRE(hr.tracked_file_count() == count);

    hr.stop();
}

TEST_CASE_METHOD(HotReloadFixture, "destructor stops the polling thread", "[hot-reload]") {
    create_xml("panel.xml");

    {
        helix::XmlHotReloader hr;
        hr.set_reload_callback([](const std::string&, const std::string&) {});
        hr.start({temp_dir_.string()}, 50);
        REQUIRE(hr.is_running() == true);
        // Destructor should call stop() and join the thread
    }
    // If we get here without hanging, the destructor worked
    REQUIRE(true);
}

// ============================================================================
// Change detection [hot-reload]
// ============================================================================

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload detects file modification", "[hot-reload]") {
    create_xml("my_panel.xml", "<view><lv_obj/></view>");

    std::mutex mtx;
    std::vector<std::string> reloaded_components;

    helix::XmlHotReloader hr;
    hr.set_reload_callback([&](const std::string& name, const std::string& /*path*/) {
        std::lock_guard<std::mutex> lock(mtx);
        reloaded_components.push_back(name);
    });
    hr.start({temp_dir_.string()}, 50);

    // Modify the file
    touch_xml("my_panel.xml");

    // Wait for the polling thread to pick it up
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!reloaded_components.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    hr.stop();

    std::lock_guard<std::mutex> lock(mtx);
    REQUIRE(reloaded_components.size() == 1);
    REQUIRE(reloaded_components[0] == "my_panel");
}

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload reports correct LVGL path", "[hot-reload]") {
    create_xml("widget.xml");

    std::mutex mtx;
    std::string reported_path;

    helix::XmlHotReloader hr;
    hr.set_reload_callback([&](const std::string& /*name*/, const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx);
        reported_path = path;
    });
    hr.start({temp_dir_.string()}, 50);

    touch_xml("widget.xml");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!reported_path.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    hr.stop();

    std::lock_guard<std::mutex> lock(mtx);
    REQUIRE(reported_path.substr(0, 2) == "A:");
    REQUIRE(reported_path.find("widget.xml") != std::string::npos);
}

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload does not fire for unmodified files",
                 "[hot-reload]") {
    create_xml("stable.xml");

    std::atomic<int> reload_count{0};

    helix::XmlHotReloader hr;
    hr.set_reload_callback(
        [&](const std::string& /*name*/, const std::string& /*path*/) { reload_count++; });
    hr.start({temp_dir_.string()}, 50);

    // Wait a few poll cycles without touching anything
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    hr.stop();

    REQUIRE(reload_count.load() == 0);
}

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload handles deleted file gracefully",
                 "[hot-reload]") {
    create_xml("ephemeral.xml");

    std::atomic<int> reload_count{0};

    helix::XmlHotReloader hr;
    hr.set_reload_callback(
        [&](const std::string& /*name*/, const std::string& /*path*/) { reload_count++; });
    hr.start({temp_dir_.string()}, 50);

    // Delete the file
    fs::remove(temp_dir_ / "ephemeral.xml");

    // Wait a few poll cycles — should not crash or fire reload
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    hr.stop();

    REQUIRE(reload_count.load() == 0);
}

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload detects changes in subdirectory",
                 "[hot-reload]") {
    create_sub_xml("nozzle_icon.xml");

    std::mutex mtx;
    std::vector<std::string> reloaded;

    helix::XmlHotReloader hr;
    hr.set_reload_callback([&](const std::string& name, const std::string& /*path*/) {
        std::lock_guard<std::mutex> lock(mtx);
        reloaded.push_back(name);
    });
    hr.start({temp_dir_.string(), sub_dir_.string()}, 50);

    touch_xml("nozzle_icon.xml");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!reloaded.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    hr.stop();

    std::lock_guard<std::mutex> lock(mtx);
    REQUIRE(reloaded.size() == 1);
    REQUIRE(reloaded[0] == "nozzle_icon");
}

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload detects multiple files changing",
                 "[hot-reload]") {
    create_xml("panel_a.xml");
    create_xml("panel_b.xml");
    create_xml("panel_c.xml"); // This one stays unchanged

    std::mutex mtx;
    std::vector<std::string> reloaded;

    helix::XmlHotReloader hr;
    hr.set_reload_callback([&](const std::string& name, const std::string& /*path*/) {
        std::lock_guard<std::mutex> lock(mtx);
        reloaded.push_back(name);
    });
    hr.start({temp_dir_.string()}, 50);

    touch_xml("panel_a.xml");
    touch_xml("panel_b.xml");
    // panel_c.xml NOT touched

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (reloaded.size() >= 2)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    hr.stop();

    std::lock_guard<std::mutex> lock(mtx);
    REQUIRE(reloaded.size() == 2);

    // Both changed files should be reloaded (order may vary)
    std::sort(reloaded.begin(), reloaded.end());
    REQUIRE(reloaded[0] == "panel_a");
    REQUIRE(reloaded[1] == "panel_b");
}

// ============================================================================
// Manual scan_and_reload (no polling thread) [hot-reload]
// ============================================================================

TEST_CASE_METHOD(HotReloadFixture, "scan_and_reload can be called manually without start",
                 "[hot-reload]") {
    create_xml("manual.xml");

    // Use start() just to do the initial scan, then immediately stop
    std::vector<std::string> reloaded;

    helix::XmlHotReloader hr;
    hr.set_reload_callback(
        [&](const std::string& name, const std::string& /*path*/) { reloaded.push_back(name); });
    hr.start({temp_dir_.string()}, 50);
    hr.stop();

    // No changes yet
    hr.scan_and_reload();
    REQUIRE(reloaded.empty());

    // Touch the file and scan manually
    touch_xml("manual.xml");
    hr.scan_and_reload();

    REQUIRE(reloaded.size() == 1);
    REQUIRE(reloaded[0] == "manual");

    // Second scan without changes should not reload again
    hr.scan_and_reload();
    REQUIRE(reloaded.size() == 1);
}
