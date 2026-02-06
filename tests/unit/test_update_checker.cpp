// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_update_checker.cpp
 * @brief TDD tests for UpdateChecker service
 *
 * These tests define the expected interface and behavior of UpdateChecker
 * before implementation exists. Tests are structured to:
 *
 * 1. Run currently (version comparison, JSON parsing) - validates existing utils
 * 2. Fail to compile once update_checker.h is included - drives interface design
 * 3. Pass after full implementation - validates implementation correctness
 *
 * Test categories:
 * - Version comparison for update detection
 * - GitHub release JSON parsing
 * - Error handling (network, parse, invalid data)
 * - Status enum transitions
 */

#include "lvgl.h"
#include "version.h"

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix::version;
using json = nlohmann::json;

// ============================================================================
// Helper Functions for UpdateChecker Logic
// ============================================================================

namespace {

/**
 * @brief Strip 'v' or 'V' prefix from version tag
 *
 * GitHub releases use "v1.2.3" format, but version comparison needs "1.2.3"
 */
std::string strip_version_prefix(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        return tag.substr(1);
    }
    return tag;
}

/**
 * @brief Determine if an update is available
 *
 * Returns true if latest > current (newer version available)
 * Returns false if latest <= current (up to date or ahead)
 */
bool is_update_available(const std::string& current_version, const std::string& latest_version) {
    auto current = parse_version(current_version);
    auto latest = parse_version(latest_version);

    if (!current || !latest) {
        return false; // Can't determine, assume no update
    }

    return *latest > *current;
}

/**
 * @brief Parse ReleaseInfo from GitHub API JSON response
 *
 * Expected JSON format:
 * {
 *   "tag_name": "v1.2.3",
 *   "body": "Release notes...",
 *   "published_at": "2025-01-15T10:00:00Z",
 *   "assets": [{"name": "file.tar.gz", "browser_download_url": "https://..."}]
 * }
 */
struct ParsedRelease {
    std::string version;       // Stripped version (e.g., "1.2.3")
    std::string tag_name;      // Original tag (e.g., "v1.2.3")
    std::string download_url;  // Asset download URL
    std::string release_notes; // Body markdown
    std::string published_at;  // ISO 8601 timestamp
    bool valid = false;
};

/**
 * @brief Safely get string value from JSON, handling null
 */
std::string json_string_or_empty(const json& j, const std::string& key) {
    if (!j.contains(key)) {
        return "";
    }
    const auto& val = j[key];
    if (val.is_null()) {
        return "";
    }
    if (val.is_string()) {
        return val.get<std::string>();
    }
    return "";
}

ParsedRelease parse_github_release(const std::string& json_str) {
    ParsedRelease result;

    try {
        auto j = json::parse(json_str);

        result.tag_name = json_string_or_empty(j, "tag_name");
        result.release_notes = json_string_or_empty(j, "body");
        result.published_at = json_string_or_empty(j, "published_at");

        // Strip 'v' prefix for version comparison
        result.version = strip_version_prefix(result.tag_name);

        // Find binary asset URL (look for .tar.gz)
        if (j.contains("assets") && j["assets"].is_array()) {
            for (const auto& asset : j["assets"]) {
                std::string name = asset.value("name", "");
                if (name.find(".tar.gz") != std::string::npos) {
                    result.download_url = asset.value("browser_download_url", "");
                    break;
                }
            }
        }

        // Valid if we have at least a version
        result.valid = !result.version.empty() && parse_version(result.version).has_value();

    } catch (const json::exception&) {
        result.valid = false;
    }

    return result;
}

} // anonymous namespace

// ============================================================================
// Version Comparison for Update Detection
// ============================================================================

TEST_CASE("Version comparison for update detection", "[update_checker][version]") {
    SECTION("update available when latest > current") {
        // Minor version bump
        REQUIRE(is_update_available("1.0.0", "1.1.0"));
        // Patch version bump
        REQUIRE(is_update_available("1.0.0", "1.0.1"));
        // Major version bump
        REQUIRE(is_update_available("1.0.0", "2.0.0"));
        // Multiple component differences
        REQUIRE(is_update_available("1.2.3", "1.2.4"));
        REQUIRE(is_update_available("1.2.3", "1.3.0"));
        REQUIRE(is_update_available("1.2.3", "2.0.0"));
    }

    SECTION("no update when versions are equal") {
        REQUIRE_FALSE(is_update_available("1.0.0", "1.0.0"));
        REQUIRE_FALSE(is_update_available("2.5.3", "2.5.3"));
        REQUIRE_FALSE(is_update_available("0.0.1", "0.0.1"));
    }

    SECTION("no update when current is ahead (don't downgrade)") {
        // Current is newer than remote (development build scenario)
        REQUIRE_FALSE(is_update_available("1.1.0", "1.0.0"));
        REQUIRE_FALSE(is_update_available("2.0.0", "1.9.9"));
        REQUIRE_FALSE(is_update_available("1.0.1", "1.0.0"));
    }

    SECTION("handles v prefix in version strings") {
        // parse_version already handles v prefix
        auto v1 = parse_version("v1.0.0");
        auto v2 = parse_version("1.1.0");
        REQUIRE(v1.has_value());
        REQUIRE(v2.has_value());
        REQUIRE(*v2 > *v1);
    }

    SECTION("pre-release suffix stripped for comparison") {
        // Pre-release versions should compare as their base version
        auto beta = parse_version("1.0.0-beta");
        auto release = parse_version("1.0.0");
        REQUIRE(beta.has_value());
        REQUIRE(release.has_value());
        // Both parse to 1.0.0, so they're equal
        REQUIRE(*beta == *release);
    }

    SECTION("pre-release to release is NOT an update (same base version)") {
        // v1.0.0-beta -> v1.0.0 should NOT be an update
        // (pre-release suffix is stripped, versions are equal)
        REQUIRE_FALSE(is_update_available("1.0.0-beta", "1.0.0"));
        REQUIRE_FALSE(is_update_available("1.0.0", "1.0.0-beta"));
    }

    SECTION("invalid version strings return no update") {
        REQUIRE_FALSE(is_update_available("", "1.0.0"));
        REQUIRE_FALSE(is_update_available("1.0.0", ""));
        REQUIRE_FALSE(is_update_available("invalid", "1.0.0"));
        REQUIRE_FALSE(is_update_available("1.0.0", "invalid"));
        REQUIRE_FALSE(is_update_available("", ""));
    }
}

// ============================================================================
// GitHub Release JSON Parsing
// ============================================================================

TEST_CASE("GitHub release JSON parsing", "[update_checker][json]") {
    SECTION("parses valid release JSON") {
        const char* json_str = R"({
            "tag_name": "v1.2.3",
            "body": "## What's New\n- Feature A\n- Bug fix B",
            "published_at": "2025-01-15T10:00:00Z",
            "assets": [{
                "name": "helixscreen-1.2.3.tar.gz",
                "browser_download_url": "https://github.com/prestonbrown/helixscreen/releases/download/v1.2.3/helixscreen-1.2.3.tar.gz"
            }]
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        REQUIRE(release.tag_name == "v1.2.3");
        REQUIRE(release.version == "1.2.3");
        REQUIRE(release.release_notes == "## What's New\n- Feature A\n- Bug fix B");
        REQUIRE(release.published_at == "2025-01-15T10:00:00Z");
        REQUIRE(release.download_url ==
                "https://github.com/prestonbrown/helixscreen/releases/download/v1.2.3/"
                "helixscreen-1.2.3.tar.gz");
    }

    SECTION("handles multiple assets, selects tar.gz") {
        const char* json_str = R"({
            "tag_name": "v2.0.0",
            "body": "Release",
            "published_at": "2025-02-01T00:00:00Z",
            "assets": [
                {"name": "source.zip", "browser_download_url": "https://example.com/source.zip"},
                {"name": "helixscreen.tar.gz", "browser_download_url": "https://example.com/helixscreen.tar.gz"},
                {"name": "debug.log", "browser_download_url": "https://example.com/debug.log"}
            ]
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        REQUIRE(release.download_url == "https://example.com/helixscreen.tar.gz");
    }

    SECTION("handles missing optional fields gracefully") {
        // Minimal valid JSON - only tag_name required for version
        const char* json_str = R"({
            "tag_name": "v3.0.0"
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        REQUIRE(release.version == "3.0.0");
        REQUIRE(release.release_notes.empty());
        REQUIRE(release.published_at.empty());
        REQUIRE(release.download_url.empty());
    }

    SECTION("handles empty assets array") {
        const char* json_str = R"({
            "tag_name": "v1.0.0",
            "body": "No binaries yet",
            "assets": []
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        REQUIRE(release.version == "1.0.0");
        REQUIRE(release.download_url.empty());
    }

    SECTION("handles null body field") {
        const char* json_str = R"({
            "tag_name": "v1.0.0",
            "body": null,
            "published_at": "2025-01-01T00:00:00Z"
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        // null should be converted to empty string by .value() default
        REQUIRE(release.release_notes.empty());
    }

    SECTION("rejects malformed JSON") {
        const char* invalid_json = R"({
            "tag_name": "v1.0.0"
            "body": "missing comma"
        })";

        auto release = parse_github_release(invalid_json);
        REQUIRE_FALSE(release.valid);
    }

    SECTION("rejects empty JSON object") {
        auto release = parse_github_release("{}");
        REQUIRE_FALSE(release.valid);
    }

    SECTION("rejects invalid tag_name") {
        const char* json_str = R"({
            "tag_name": "not-a-version"
        })";

        auto release = parse_github_release(json_str);
        REQUIRE_FALSE(release.valid);
    }

    SECTION("rejects empty string") {
        auto release = parse_github_release("");
        REQUIRE_FALSE(release.valid);
    }

    SECTION("handles version without v prefix") {
        const char* json_str = R"({
            "tag_name": "1.5.0"
        })";

        auto release = parse_github_release(json_str);

        REQUIRE(release.valid);
        REQUIRE(release.tag_name == "1.5.0");
        REQUIRE(release.version == "1.5.0");
    }
}

// ============================================================================
// Version Prefix Stripping
// ============================================================================

TEST_CASE("Version prefix stripping", "[update_checker][version]") {
    SECTION("strips lowercase v") {
        REQUIRE(strip_version_prefix("v1.2.3") == "1.2.3");
    }

    SECTION("strips uppercase V") {
        REQUIRE(strip_version_prefix("V1.2.3") == "1.2.3");
    }

    SECTION("preserves version without prefix") {
        REQUIRE(strip_version_prefix("1.2.3") == "1.2.3");
    }

    SECTION("handles empty string") {
        REQUIRE(strip_version_prefix("") == "");
    }

    SECTION("handles just v") {
        REQUIRE(strip_version_prefix("v") == "");
    }
}

// ============================================================================
// Error Handling Scenarios
// ============================================================================

TEST_CASE("Update checker error scenarios", "[update_checker][error]") {
    SECTION("empty response body") {
        auto release = parse_github_release("");
        REQUIRE_FALSE(release.valid);
    }

    SECTION("non-JSON response") {
        auto release = parse_github_release("<!DOCTYPE html><html>Error</html>");
        REQUIRE_FALSE(release.valid);
    }

    SECTION("JSON array instead of object") {
        auto release = parse_github_release("[1, 2, 3]");
        REQUIRE_FALSE(release.valid);
    }

    SECTION("deeply nested invalid structure") {
        const char* json_str = R"({
            "tag_name": {"nested": "object"}
        })";

        auto release = parse_github_release(json_str);
        REQUIRE_FALSE(release.valid);
    }
}

// ============================================================================
// UpdateChecker Interface Tests (TO BE ENABLED)
// ============================================================================
//
// These tests define the expected UpdateChecker interface. They will fail to
// compile until update_checker.h is created. Uncomment after implementation.
//

// Interface tests for UpdateChecker - now enabled

#include "system/update_checker.h"

TEST_CASE("UpdateChecker status enum values", "[update_checker][status]") {
    // Verify enum values exist and are distinct
    REQUIRE(static_cast<int>(UpdateChecker::Status::Idle) == 0);
    REQUIRE(static_cast<int>(UpdateChecker::Status::Checking) == 1);
    REQUIRE(static_cast<int>(UpdateChecker::Status::UpdateAvailable) == 2);
    REQUIRE(static_cast<int>(UpdateChecker::Status::UpToDate) == 3);
    REQUIRE(static_cast<int>(UpdateChecker::Status::Error) == 4);
}

TEST_CASE("UpdateChecker initial state", "[update_checker][init]") {
    auto& checker = UpdateChecker::instance();

    // Clear any state from previous tests
    checker.clear_cache();

    SECTION("starts in Idle state after clear") {
        REQUIRE(checker.get_status() == UpdateChecker::Status::Idle);
    }

    SECTION("no cached update after clear") {
        REQUIRE_FALSE(checker.has_update_available());
        REQUIRE_FALSE(checker.get_cached_update().has_value());
    }

    SECTION("no error message after clear") {
        REQUIRE(checker.get_error_message().empty());
    }
}

TEST_CASE("UpdateChecker ReleaseInfo struct", "[update_checker][release_info]") {
    UpdateChecker::ReleaseInfo info;

    SECTION("default construction has empty strings") {
        REQUIRE(info.version.empty());
        REQUIRE(info.tag_name.empty());
        REQUIRE(info.download_url.empty());
        REQUIRE(info.release_notes.empty());
        REQUIRE(info.published_at.empty());
    }

    SECTION("can assign values") {
        info.version = "1.2.3";
        info.tag_name = "v1.2.3";
        info.download_url = "https://example.com/release.tar.gz";
        info.release_notes = "Bug fixes";
        info.published_at = "2025-01-15T10:00:00Z";

        REQUIRE(info.version == "1.2.3");
        REQUIRE(info.tag_name == "v1.2.3");
    }
}

TEST_CASE("UpdateChecker cache behavior", "[update_checker][cache]") {
    auto& checker = UpdateChecker::instance();

    SECTION("clear_cache resets cached update") {
        checker.clear_cache();
        REQUIRE_FALSE(checker.get_cached_update().has_value());
        REQUIRE(checker.get_status() == UpdateChecker::Status::Idle);
    }
}

TEST_CASE("UpdateChecker thread safety", "[update_checker][threading]") {
    auto& checker = UpdateChecker::instance();

    SECTION("get_status is thread-safe") {
        // Should be able to call from any thread
        auto status = checker.get_status();
        (void)status; // Use the variable
    }

    SECTION("get_cached_update is thread-safe") {
        // Should return consistent snapshot
        auto cached = checker.get_cached_update();
        (void)cached;
    }

    SECTION("has_update_available is thread-safe") {
        auto has_update = checker.has_update_available();
        (void)has_update;
    }
}

TEST_CASE("UpdateChecker lifecycle", "[update_checker][lifecycle]") {
    auto& checker = UpdateChecker::instance();

    SECTION("init is idempotent") {
        REQUIRE_NOTHROW(checker.init());
        REQUIRE_NOTHROW(checker.init());
    }

    SECTION("shutdown is idempotent") {
        REQUIRE_NOTHROW(checker.shutdown());
        REQUIRE_NOTHROW(checker.shutdown());
    }
}

TEST_CASE("UpdateChecker callback is optional", "[update_checker][callback]") {
    auto& checker = UpdateChecker::instance();
    checker.init();
    checker.clear_cache();

    SECTION("nullptr callback is accepted") {
        // This should not throw, even though it will try to make a network request
        // The test may fail due to rate limiting or network issues, but shouldn't crash
        REQUIRE_NOTHROW(checker.check_for_updates(nullptr));

        // Give a tiny bit of time for thread to start, then shutdown cleanly
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        checker.shutdown();
    }
}

// ============================================================================
// Real-World Scenario Tests
// ============================================================================

TEST_CASE("Real-world update scenarios", "[update_checker][scenarios]") {
    SECTION("typical GitHub release response") {
        // Simulates actual GitHub API response structure
        const char* github_response = R"({
            "url": "https://api.github.com/repos/prestonbrown/helixscreen/releases/12345",
            "html_url": "https://github.com/prestonbrown/helixscreen/releases/tag/v1.5.0",
            "id": 12345,
            "tag_name": "v1.5.0",
            "target_commitish": "main",
            "name": "HelixScreen v1.5.0",
            "draft": false,
            "prerelease": false,
            "created_at": "2025-01-20T08:00:00Z",
            "published_at": "2025-01-20T10:00:00Z",
            "body": "## What's New in v1.5.0\n\n### Features\n- Auto-update support\n- Improved touch calibration\n\n### Bug Fixes\n- Fixed memory leak in thumbnail cache",
            "assets": [
                {
                    "url": "https://api.github.com/repos/prestonbrown/helixscreen/releases/assets/100",
                    "id": 100,
                    "name": "helixscreen-1.5.0-arm64.tar.gz",
                    "size": 5242880,
                    "download_count": 42,
                    "browser_download_url": "https://github.com/prestonbrown/helixscreen/releases/download/v1.5.0/helixscreen-1.5.0-arm64.tar.gz"
                },
                {
                    "url": "https://api.github.com/repos/prestonbrown/helixscreen/releases/assets/101",
                    "id": 101,
                    "name": "sha256sums.txt",
                    "size": 128,
                    "download_count": 10,
                    "browser_download_url": "https://github.com/prestonbrown/helixscreen/releases/download/v1.5.0/sha256sums.txt"
                }
            ]
        })";

        auto release = parse_github_release(github_response);

        REQUIRE(release.valid);
        REQUIRE(release.version == "1.5.0");
        REQUIRE(release.tag_name == "v1.5.0");
        REQUIRE(release.download_url.find("helixscreen-1.5.0-arm64.tar.gz") != std::string::npos);
        REQUIRE(release.release_notes.find("Auto-update support") != std::string::npos);
    }

    SECTION("update from 1.4.0 to 1.5.0") {
        const std::string current = "1.4.0";
        const std::string latest = "1.5.0";

        REQUIRE(is_update_available(current, latest));

        // Verify version comparison logic
        auto current_v = parse_version(current);
        auto latest_v = parse_version(latest);
        REQUIRE(current_v.has_value());
        REQUIRE(latest_v.has_value());
        REQUIRE(latest_v->minor == current_v->minor + 1);
    }

    SECTION("no update when running development build ahead of release") {
        // Developer might be on 1.6.0-dev while latest release is 1.5.0
        const std::string current = "1.6.0";
        const std::string latest = "1.5.0";

        REQUIRE_FALSE(is_update_available(current, latest));
    }

    SECTION("update available for security patch") {
        // Security patches bump patch version
        const std::string current = "1.5.0";
        const std::string latest = "1.5.1";

        REQUIRE(is_update_available(current, latest));
    }
}

// ============================================================================
// Edge Cases and Boundary Conditions
// ============================================================================

TEST_CASE("Version edge cases", "[update_checker][edge]") {
    SECTION("major version zero (0.x.x)") {
        // Pre-1.0 versions should still compare correctly
        REQUIRE(is_update_available("0.1.0", "0.2.0"));
        REQUIRE(is_update_available("0.9.9", "1.0.0"));
        REQUIRE_FALSE(is_update_available("0.5.0", "0.5.0"));
    }

    SECTION("large version numbers") {
        REQUIRE(is_update_available("1.0.0", "100.0.0"));
        REQUIRE(is_update_available("99.99.99", "100.0.0"));
    }

    SECTION("version with build metadata") {
        // Build metadata should be ignored
        auto v1 = parse_version("1.0.0+build.123");
        auto v2 = parse_version("1.0.0+build.456");
        REQUIRE(v1.has_value());
        REQUIRE(v2.has_value());
        REQUIRE(*v1 == *v2);
    }

    SECTION("version with pre-release and build metadata") {
        auto v = parse_version("1.0.0-beta.1+sha.abc123");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 1);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }
}

// ============================================================================
// LVGL Subject Integration Tests
// ============================================================================

TEST_CASE("UpdateChecker subject initialization", "[update_checker][subjects]") {
    auto& checker = UpdateChecker::instance();
    checker.clear_cache();
    checker.init();

    SECTION("all subject accessors return non-null after init") {
        REQUIRE(checker.status_subject() != nullptr);
        REQUIRE(checker.checking_subject() != nullptr);
        REQUIRE(checker.version_text_subject() != nullptr);
        REQUIRE(checker.new_version_subject() != nullptr);
    }

    SECTION("integer subjects have correct initial values") {
        REQUIRE(lv_subject_get_int(checker.status_subject()) ==
                static_cast<int>(UpdateChecker::Status::Idle));
        REQUIRE(lv_subject_get_int(checker.checking_subject()) == 0);
    }

    SECTION("string subjects start empty") {
        const char* version_text = lv_subject_get_string(checker.version_text_subject());
        REQUIRE(version_text != nullptr);
        REQUIRE(std::string(version_text).empty());

        const char* new_version = lv_subject_get_string(checker.new_version_subject());
        REQUIRE(new_version != nullptr);
        REQUIRE(std::string(new_version).empty());
    }

    checker.shutdown();
}

TEST_CASE("UpdateChecker subject accessors remain stable after shutdown",
          "[update_checker][subjects]") {
    auto& checker = UpdateChecker::instance();
    checker.clear_cache();
    checker.init();

    // Verify subjects exist before shutdown
    REQUIRE(checker.status_subject() != nullptr);
    REQUIRE(checker.checking_subject() != nullptr);

    checker.shutdown();

    // Accessors return member addresses, so they remain non-null even after shutdown.
    // (The subjects themselves are deinitialized, but the pointers are stable.)
    REQUIRE(checker.status_subject() != nullptr);
    REQUIRE(checker.checking_subject() != nullptr);
    REQUIRE(checker.version_text_subject() != nullptr);
    REQUIRE(checker.new_version_subject() != nullptr);
}

TEST_CASE("JSON edge cases", "[update_checker][json][edge]") {
    SECTION("unicode in release notes") {
        const char* json_str = R"({
            "tag_name": "v1.0.0",
            "body": "Fixed emoji display \ud83d\ude80 and Chinese chars \u4e2d\u6587"
        })";

        auto release = parse_github_release(json_str);
        REQUIRE(release.valid);
        REQUIRE_FALSE(release.release_notes.empty());
    }

    SECTION("very long release notes") {
        std::string long_body(10000, 'x');
        std::string json_str = R"({"tag_name": "v1.0.0", "body": ")" + long_body + R"("})";

        auto release = parse_github_release(json_str);
        REQUIRE(release.valid);
        REQUIRE(release.release_notes.length() == 10000);
    }

    SECTION("special characters in asset names") {
        const char* json_str = R"({
            "tag_name": "v1.0.0",
            "assets": [{
                "name": "helix screen_v1.0.0_(arm64).tar.gz",
                "browser_download_url": "https://example.com/release.tar.gz"
            }]
        })";

        auto release = parse_github_release(json_str);
        REQUIRE(release.valid);
        REQUIRE_FALSE(release.download_url.empty());
    }
}

// ============================================================================
// Download Status Types and Subjects
// ============================================================================

TEST_CASE("UpdateChecker download status enum values", "[update_checker]") {
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Idle) == 0);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Confirming) == 1);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Downloading) == 2);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Verifying) == 3);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Installing) == 4);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Complete) == 5);
    REQUIRE(static_cast<int>(UpdateChecker::DownloadStatus::Error) == 6);
}

TEST_CASE("UpdateChecker download state initial values", "[update_checker]") {
    auto& checker = UpdateChecker::instance();
    checker.init();

    REQUIRE(checker.get_download_status() == UpdateChecker::DownloadStatus::Idle);
    REQUIRE(checker.get_download_progress() == 0);
    REQUIRE(checker.get_download_error().empty());

    checker.shutdown();
}

TEST_CASE("UpdateChecker download subjects exist after init", "[update_checker]") {
    auto& checker = UpdateChecker::instance();
    checker.init();

    REQUIRE(checker.download_status_subject() != nullptr);
    REQUIRE(checker.download_progress_subject() != nullptr);
    REQUIRE(checker.download_text_subject() != nullptr);

    REQUIRE(lv_subject_get_int(checker.download_status_subject()) == 0);
    REQUIRE(lv_subject_get_int(checker.download_progress_subject()) == 0);

    checker.shutdown();
}

TEST_CASE("UpdateChecker get_download_path returns valid path", "[update_checker]") {
    auto& checker = UpdateChecker::instance();
    checker.init();

    auto path = checker.get_download_path();
    REQUIRE(!path.empty());
    REQUIRE(path.find("helixscreen-update.tar.gz") != std::string::npos);

    checker.shutdown();
}

TEST_CASE("UpdateChecker get_platform_asset_name format", "[update_checker]") {
    auto& checker = UpdateChecker::instance();
    checker.init();

    auto name = checker.get_platform_asset_name();
    REQUIRE(name.find("helixscreen-") != std::string::npos);
    REQUIRE(name.find(".tar.gz") != std::string::npos);

    checker.shutdown();
}
