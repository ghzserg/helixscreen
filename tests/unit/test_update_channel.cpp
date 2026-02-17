// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_update_channel.cpp
 * @brief TDD tests for update channel parsing (beta, dev, platform assets, config)
 *
 * Phase 1: Tests define the expected interface and behavior BEFORE production
 * code exists. All helper functions are defined locally in this file.
 *
 * Test categories:
 * - Beta channel: GitHub /releases array parsing (prerelease selection)
 * - Dev channel: manifest JSON parsing (platform-specific assets + SHA256)
 * - Platform asset matching: selecting correct tarball from assets array
 * - Channel config: integer-to-enum mapping with fallback
 */

#include "version.h"

#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using namespace helix::version;
using json = nlohmann::json;

// ============================================================================
// Local Helper Functions (will move to production code in Phase 2)
// ============================================================================

namespace {

// --- Shared helpers (same pattern as test_update_checker.cpp) ---------------

std::string strip_version_prefix(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        return tag.substr(1);
    }
    return tag;
}

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

struct ParsedRelease {
    std::string version;       // Stripped version (e.g., "1.1.0")
    std::string tag_name;      // Original tag (e.g., "v1.1.0-beta.2")
    std::string download_url;  // Asset download URL
    std::string release_notes; // Body markdown
    std::string published_at;  // ISO 8601 timestamp
    bool valid = false;
};

// --- Channel enum -----------------------------------------------------------

enum class UpdateChannel { Stable = 0, Beta = 1, Dev = 2 };

UpdateChannel channel_from_int(int value) {
    switch (value) {
    case 1:
        return UpdateChannel::Beta;
    case 2:
        return UpdateChannel::Dev;
    default:
        return UpdateChannel::Stable;
    }
}

// --- Beta channel: parse GitHub /releases array -----------------------------

/**
 * @brief Find the latest prerelease from a GitHub releases array.
 *
 * GitHub returns releases ordered newest-first, so the first prerelease
 * in the array is the latest. If no prerelease exists, falls back to the
 * latest stable release (first non-draft entry).
 *
 * @param json_str   Raw JSON string (array of release objects)
 * @param info       Output: populated with the selected release
 * @param error      Output: error message on failure
 * @return true on success, false on error
 */
bool parse_beta_releases(const std::string& json_str, ParsedRelease& info, std::string& error) {
    try {
        auto releases = json::parse(json_str);

        if (!releases.is_array()) {
            error = "Expected JSON array of releases";
            return false;
        }

        if (releases.empty()) {
            error = "Empty releases array";
            return false;
        }

        // First pass: find latest prerelease (first in array since newest-first)
        for (const auto& rel : releases) {
            bool is_draft = rel.value("draft", false);
            bool is_prerelease = rel.value("prerelease", false);

            if (is_draft) {
                continue;
            }

            if (is_prerelease) {
                info.tag_name = json_string_or_empty(rel, "tag_name");
                info.version = strip_version_prefix(info.tag_name);
                info.release_notes = json_string_or_empty(rel, "body");
                info.published_at = json_string_or_empty(rel, "published_at");

                // Find .tar.gz asset
                if (rel.contains("assets") && rel["assets"].is_array()) {
                    for (const auto& asset : rel["assets"]) {
                        std::string name = asset.value("name", "");
                        if (name.find(".tar.gz") != std::string::npos) {
                            info.download_url = asset.value("browser_download_url", "");
                            break;
                        }
                    }
                }

                info.valid = !info.version.empty() && parse_version(info.version).has_value();
                return true;
            }
        }

        // Fallback: no prerelease found, use latest stable
        for (const auto& rel : releases) {
            bool is_draft = rel.value("draft", false);
            if (is_draft) {
                continue;
            }

            info.tag_name = json_string_or_empty(rel, "tag_name");
            info.version = strip_version_prefix(info.tag_name);
            info.release_notes = json_string_or_empty(rel, "body");
            info.published_at = json_string_or_empty(rel, "published_at");

            if (rel.contains("assets") && rel["assets"].is_array()) {
                for (const auto& asset : rel["assets"]) {
                    std::string name = asset.value("name", "");
                    if (name.find(".tar.gz") != std::string::npos) {
                        info.download_url = asset.value("browser_download_url", "");
                        break;
                    }
                }
            }

            info.valid = !info.version.empty() && parse_version(info.version).has_value();
            return true;
        }

        error = "No non-draft releases found";
        return false;

    } catch (const json::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

// --- Dev channel: manifest parsing ------------------------------------------

struct DevManifestInfo {
    std::string version;
    std::string tag;
    std::string notes;
    std::string published_at;
    std::string asset_url;
    std::string sha256;
    bool valid = false;
};

/**
 * @brief Parse a dev channel manifest JSON.
 *
 * The manifest is a single JSON object with platform-specific asset URLs
 * and SHA256 checksums.
 *
 * @param json_str      Raw JSON string
 * @param platform_key  Platform identifier ("pi", "ad5m", "k1")
 * @param info          Output: populated manifest info
 * @param error         Output: error message on failure
 * @return true on success, false on error
 */
bool parse_dev_manifest(const std::string& json_str, const std::string& platform_key,
                        DevManifestInfo& info, std::string& error) {
    try {
        auto j = json::parse(json_str);

        info.version = json_string_or_empty(j, "version");
        if (info.version.empty()) {
            error = "Missing 'version' field";
            return false;
        }

        info.tag = json_string_or_empty(j, "tag");
        info.notes = json_string_or_empty(j, "notes");
        info.published_at = json_string_or_empty(j, "published_at");

        if (!j.contains("assets") || !j["assets"].is_object()) {
            error = "Missing or invalid 'assets' object";
            return false;
        }

        const auto& assets = j["assets"];

        if (assets.empty()) {
            error = "Empty 'assets' object";
            return false;
        }

        if (!assets.contains(platform_key)) {
            error = "No asset for platform '" + platform_key + "'";
            return false;
        }

        const auto& platform_asset = assets[platform_key];
        info.asset_url = json_string_or_empty(platform_asset, "url");
        info.sha256 = json_string_or_empty(platform_asset, "sha256");
        info.valid = true;

        return true;

    } catch (const json::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

// --- Platform asset matching ------------------------------------------------

/**
 * @brief Select platform-specific asset URL from a GitHub release assets array.
 *
 * Matches assets whose name starts with the given platform prefix
 * (e.g., "helixscreen-pi-") and ends with ".tar.gz".
 *
 * @param assets           JSON array of asset objects
 * @param platform_prefix  Prefix to match (e.g., "helixscreen-pi-")
 * @param error            Output: error message on failure
 * @return Download URL, or empty string on error
 */
std::string select_platform_asset(const json& assets, const std::string& platform_prefix,
                                  std::string& error) {
    if (!assets.is_array() || assets.empty()) {
        error = "Assets array is empty or not an array";
        return "";
    }

    for (const auto& asset : assets) {
        std::string name = asset.value("name", "");
        if (name.find(platform_prefix) == 0 && name.find(".tar.gz") != std::string::npos) {
            return asset.value("browser_download_url", "");
        }
    }

    error = "No asset matching prefix '" + platform_prefix + "'";
    return "";
}

// --- R2 URL helpers ---------------------------------------------------------

/**
 * @brief Resolve R2 base URL from config value with compiled default fallback.
 */
std::string resolve_r2_base_url(const std::string& config_value, const char* default_url) {
    std::string url = config_value.empty() ? std::string(default_url) : config_value;
    // Strip trailing slash
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

/**
 * @brief Build full manifest URL for a given R2 channel.
 */
std::string build_r2_manifest_url(const std::string& base_url, const std::string& channel) {
    std::string base = base_url;
    // Strip trailing slash from base
    if (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + channel + "/manifest.json";
}

} // anonymous namespace

// ============================================================================
// Beta Channel — GitHub Releases Array Parsing
// ============================================================================

TEST_CASE("Beta channel parses releases array", "[update_channel][beta]") {
    SECTION("selects latest prerelease from mixed array") {
        const char* json_str = R"([
            {"tag_name": "v1.1.0-beta.2", "prerelease": true, "draft": false,
             "body": "Beta 2", "published_at": "2026-02-05T12:00:00Z",
             "assets": [{"name": "helixscreen-pi-v1.1.0-beta.2.tar.gz",
                          "browser_download_url": "https://github.com/releases/download/v1.1.0-beta.2/helixscreen-pi-v1.1.0-beta.2.tar.gz"}]},
            {"tag_name": "v1.0.0", "prerelease": false, "draft": false,
             "body": "Stable release", "published_at": "2026-01-15T12:00:00Z",
             "assets": [{"name": "helixscreen-pi-v1.0.0.tar.gz",
                          "browser_download_url": "https://github.com/releases/download/v1.0.0/helixscreen-pi-v1.0.0.tar.gz"}]},
            {"tag_name": "v1.1.0-beta.1", "prerelease": true, "draft": false,
             "body": "Beta 1", "published_at": "2026-01-20T12:00:00Z",
             "assets": [{"name": "helixscreen-pi-v1.1.0-beta.1.tar.gz",
                          "browser_download_url": "https://github.com/releases/download/v1.1.0-beta.1/helixscreen-pi-v1.1.0-beta.1.tar.gz"}]}
        ])";

        ParsedRelease info;
        std::string error;
        REQUIRE(parse_beta_releases(json_str, info, error));
        REQUIRE(info.valid);
        REQUIRE(info.tag_name == "v1.1.0-beta.2");
        REQUIRE(info.version == "1.1.0-beta.2");
        REQUIRE(info.release_notes == "Beta 2");
        REQUIRE(info.published_at == "2026-02-05T12:00:00Z");
        REQUIRE(info.download_url.find("v1.1.0-beta.2") != std::string::npos);
    }

    SECTION("selects first entry when it is a prerelease") {
        const char* json_str = R"([
            {"tag_name": "v2.0.0-rc.1", "prerelease": true, "draft": false,
             "body": "Release candidate", "published_at": "2026-02-06T00:00:00Z",
             "assets": [{"name": "helixscreen-pi-v2.0.0-rc.1.tar.gz",
                          "browser_download_url": "https://example.com/rc1.tar.gz"}]},
            {"tag_name": "v1.9.0", "prerelease": false, "draft": false,
             "body": "Stable", "published_at": "2026-01-01T00:00:00Z",
             "assets": []}
        ])";

        ParsedRelease info;
        std::string error;
        REQUIRE(parse_beta_releases(json_str, info, error));
        REQUIRE(info.valid);
        REQUIRE(info.tag_name == "v2.0.0-rc.1");
        REQUIRE(info.download_url == "https://example.com/rc1.tar.gz");
    }

    SECTION("falls back to latest stable when no prereleases exist") {
        const char* json_str = R"([
            {"tag_name": "v1.2.0", "prerelease": false, "draft": false,
             "body": "Latest stable", "published_at": "2026-02-01T00:00:00Z",
             "assets": [{"name": "helixscreen-pi-v1.2.0.tar.gz",
                          "browser_download_url": "https://example.com/v1.2.0.tar.gz"}]},
            {"tag_name": "v1.1.0", "prerelease": false, "draft": false,
             "body": "Previous stable", "published_at": "2026-01-01T00:00:00Z",
             "assets": [{"name": "helixscreen-pi-v1.1.0.tar.gz",
                          "browser_download_url": "https://example.com/v1.1.0.tar.gz"}]}
        ])";

        ParsedRelease info;
        std::string error;
        REQUIRE(parse_beta_releases(json_str, info, error));
        REQUIRE(info.valid);
        REQUIRE(info.tag_name == "v1.2.0");
        REQUIRE(info.release_notes == "Latest stable");
    }

    SECTION("empty array returns error") {
        ParsedRelease info;
        std::string error;
        REQUIRE_FALSE(parse_beta_releases("[]", info, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(error.find("Empty") != std::string::npos);
    }

    SECTION("malformed JSON returns error") {
        ParsedRelease info;
        std::string error;
        REQUIRE_FALSE(parse_beta_releases("{not valid json", info, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(error.find("JSON parse error") != std::string::npos);
    }

    SECTION("skips draft releases") {
        const char* json_str = R"([
            {"tag_name": "v3.0.0-alpha.1", "prerelease": true, "draft": true,
             "body": "Draft", "published_at": "2026-03-01T00:00:00Z", "assets": []},
            {"tag_name": "v2.0.0-beta.1", "prerelease": true, "draft": false,
             "body": "Real beta", "published_at": "2026-02-01T00:00:00Z",
             "assets": [{"name": "helixscreen-pi-v2.0.0-beta.1.tar.gz",
                          "browser_download_url": "https://example.com/beta1.tar.gz"}]}
        ])";

        ParsedRelease info;
        std::string error;
        REQUIRE(parse_beta_releases(json_str, info, error));
        REQUIRE(info.tag_name == "v2.0.0-beta.1");
    }
}

// ============================================================================
// Dev Channel — Manifest Parsing
// ============================================================================

TEST_CASE("Dev channel parses manifest JSON", "[update_channel][dev]") {
    const char* valid_manifest = R"({
        "version": "0.9.4-dev.1",
        "tag": "v0.9.4-dev.1",
        "notes": "Testing feature X",
        "published_at": "2026-02-06T12:00:00Z",
        "assets": {
            "pi": {
                "url": "https://bucket.example.com/helixscreen-pi-v0.9.4-dev.1.tar.gz",
                "sha256": "abc123def456"
            },
            "ad5m": {
                "url": "https://bucket.example.com/helixscreen-ad5m-v0.9.4-dev.1.tar.gz",
                "sha256": "789ghi012"
            },
            "k1": {
                "url": "https://bucket.example.com/helixscreen-k1-v0.9.4-dev.1.tar.gz",
                "sha256": "jkl345mno678"
            },
            "pi32": {
                "url": "https://bucket.example.com/helixscreen-pi32-v0.9.4-dev.1.tar.gz",
                "sha256": "pi32hash456"
            }
        }
    })";

    SECTION("parses valid manifest for pi platform") {
        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(valid_manifest, "pi", info, error));
        REQUIRE(info.valid);
        REQUIRE(info.version == "0.9.4-dev.1");
        REQUIRE(info.tag == "v0.9.4-dev.1");
        REQUIRE(info.notes == "Testing feature X");
        REQUIRE(info.published_at == "2026-02-06T12:00:00Z");
        REQUIRE(info.asset_url == "https://bucket.example.com/helixscreen-pi-v0.9.4-dev.1.tar.gz");
        REQUIRE(info.sha256 == "abc123def456");
    }

    SECTION("parses valid manifest for ad5m platform") {
        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(valid_manifest, "ad5m", info, error));
        REQUIRE(info.valid);
        REQUIRE(info.asset_url.find("ad5m") != std::string::npos);
        REQUIRE(info.sha256 == "789ghi012");
    }

    SECTION("parses valid manifest for k1 platform") {
        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(valid_manifest, "k1", info, error));
        REQUIRE(info.valid);
        REQUIRE(info.asset_url.find("k1") != std::string::npos);
        REQUIRE(info.sha256 == "jkl345mno678");
    }

    SECTION("parses valid manifest for pi32 platform") {
        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(valid_manifest, "pi32", info, error));
        REQUIRE(info.valid);
        REQUIRE(info.asset_url.find("pi32") != std::string::npos);
        REQUIRE(info.sha256 == "pi32hash456");
    }

    SECTION("missing version field returns error") {
        const char* manifest = R"({
            "tag": "v0.9.4-dev.1",
            "assets": { "pi": { "url": "https://example.com/a.tar.gz", "sha256": "abc" } }
        })";

        DevManifestInfo info;
        std::string error;
        REQUIRE_FALSE(parse_dev_manifest(manifest, "pi", info, error));
        REQUIRE(error.find("version") != std::string::npos);
    }

    SECTION("missing platform key returns error") {
        DevManifestInfo info;
        std::string error;
        REQUIRE_FALSE(parse_dev_manifest(valid_manifest, "rk3566", info, error));
        REQUIRE(error.find("rk3566") != std::string::npos);
    }

    SECTION("empty assets object returns error") {
        const char* manifest = R"({
            "version": "0.9.4-dev.1",
            "assets": {}
        })";

        DevManifestInfo info;
        std::string error;
        REQUIRE_FALSE(parse_dev_manifest(manifest, "pi", info, error));
        REQUIRE_FALSE(error.empty());
    }

    SECTION("missing assets field returns error") {
        const char* manifest = R"({
            "version": "0.9.4-dev.1",
            "tag": "v0.9.4-dev.1"
        })";

        DevManifestInfo info;
        std::string error;
        REQUIRE_FALSE(parse_dev_manifest(manifest, "pi", info, error));
        REQUIRE(error.find("assets") != std::string::npos);
    }

    SECTION("malformed JSON returns error") {
        DevManifestInfo info;
        std::string error;
        REQUIRE_FALSE(parse_dev_manifest("not json at all", "pi", info, error));
        REQUIRE(error.find("JSON parse error") != std::string::npos);
    }

    SECTION("SHA256 field captured correctly") {
        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(valid_manifest, "pi", info, error));
        REQUIRE(info.sha256 == "abc123def456");
        REQUIRE_FALSE(info.sha256.empty());
    }
}

// ============================================================================
// Platform Asset Matching
// ============================================================================

TEST_CASE("Platform asset selection from release assets", "[update_channel][assets]") {
    const char* multi_asset_str = R"([
        {"name": "helixscreen-pi-v1.0.0.tar.gz",
         "browser_download_url": "https://pi-url"},
        {"name": "helixscreen-ad5m-v1.0.0.tar.gz",
         "browser_download_url": "https://ad5m-url"},
        {"name": "helixscreen-k1-v1.0.0.tar.gz",
         "browser_download_url": "https://k1-url"},
        {"name": "helixscreen-pi32-v1.0.0.tar.gz",
         "browser_download_url": "https://pi32-url"},
        {"name": "checksums.txt",
         "browser_download_url": "https://checksums-url"}
    ])";

    auto multi_assets = json::parse(multi_asset_str);

    SECTION("selects correct pi asset from multi-platform release") {
        std::string error;
        auto url = select_platform_asset(multi_assets, "helixscreen-pi-", error);
        REQUIRE(url == "https://pi-url");
        REQUIRE(error.empty());
    }

    SECTION("selects correct ad5m asset from multi-platform release") {
        std::string error;
        auto url = select_platform_asset(multi_assets, "helixscreen-ad5m-", error);
        REQUIRE(url == "https://ad5m-url");
        REQUIRE(error.empty());
    }

    SECTION("selects correct k1 asset from multi-platform release") {
        std::string error;
        auto url = select_platform_asset(multi_assets, "helixscreen-k1-", error);
        REQUIRE(url == "https://k1-url");
        REQUIRE(error.empty());
    }

    SECTION("selects correct pi32 asset from multi-platform release") {
        std::string error;
        auto url = select_platform_asset(multi_assets, "helixscreen-pi32-", error);
        REQUIRE(url == "https://pi32-url");
        REQUIRE(error.empty());
    }

    SECTION("single matching asset still works") {
        auto single = json::parse(R"([
            {"name": "helixscreen-pi-v2.0.0.tar.gz",
             "browser_download_url": "https://single-pi-url"}
        ])");

        std::string error;
        auto url = select_platform_asset(single, "helixscreen-pi-", error);
        REQUIRE(url == "https://single-pi-url");
    }

    SECTION("no matching platform asset returns error") {
        std::string error;
        auto url = select_platform_asset(multi_assets, "helixscreen-rk3566-", error);
        REQUIRE(url.empty());
        REQUIRE(error.find("rk3566") != std::string::npos);
    }

    SECTION("non-matching names only returns error") {
        auto non_matching = json::parse(R"([
            {"name": "source-code.zip",
             "browser_download_url": "https://source-url"},
            {"name": "checksums.txt",
             "browser_download_url": "https://checksums-url"}
        ])");

        std::string error;
        auto url = select_platform_asset(non_matching, "helixscreen-pi-", error);
        REQUIRE(url.empty());
        REQUIRE_FALSE(error.empty());
    }

    SECTION("empty assets array returns error") {
        auto empty_arr = json::parse("[]");
        std::string error;
        auto url = select_platform_asset(empty_arr, "helixscreen-pi-", error);
        REQUIRE(url.empty());
        REQUIRE_FALSE(error.empty());
    }
}

// ============================================================================
// Channel Config — Integer to Enum Mapping
// ============================================================================

TEST_CASE("Channel config integer mapping", "[update_channel][config]") {
    SECTION("value 0 maps to Stable") {
        REQUIRE(channel_from_int(0) == UpdateChannel::Stable);
    }

    SECTION("value 1 maps to Beta") {
        REQUIRE(channel_from_int(1) == UpdateChannel::Beta);
    }

    SECTION("value 2 maps to Dev") {
        REQUIRE(channel_from_int(2) == UpdateChannel::Dev);
    }

    SECTION("out-of-range value 99 falls back to Stable") {
        REQUIRE(channel_from_int(99) == UpdateChannel::Stable);
    }

    SECTION("negative value falls back to Stable") {
        REQUIRE(channel_from_int(-1) == UpdateChannel::Stable);
        REQUIRE(channel_from_int(-100) == UpdateChannel::Stable);
    }

    SECTION("enum values are distinct integers") {
        REQUIRE(static_cast<int>(UpdateChannel::Stable) == 0);
        REQUIRE(static_cast<int>(UpdateChannel::Beta) == 1);
        REQUIRE(static_cast<int>(UpdateChannel::Dev) == 2);
    }
}

// ============================================================================
// R2 Base URL Resolution
// ============================================================================

TEST_CASE("R2 base URL resolution", "[update_channel][r2]") {
    SECTION("default URL when no config override") {
        auto url = resolve_r2_base_url("", "https://releases.helixscreen.org");
        REQUIRE(url == "https://releases.helixscreen.org");
    }

    SECTION("config override replaces default") {
        auto url =
            resolve_r2_base_url("https://my-cdn.example.com", "https://releases.helixscreen.org");
        REQUIRE(url == "https://my-cdn.example.com");
    }

    SECTION("trailing slash normalized") {
        auto url =
            resolve_r2_base_url("https://my-cdn.example.com/", "https://releases.helixscreen.org");
        REQUIRE(url == "https://my-cdn.example.com");
    }

    SECTION("empty config falls back to default") {
        auto url = resolve_r2_base_url("", "https://default.example.com");
        REQUIRE(url == "https://default.example.com");
    }
}

// ============================================================================
// R2 Manifest URL Construction
// ============================================================================

TEST_CASE("R2 manifest URL construction", "[update_channel][r2]") {
    SECTION("stable channel URL") {
        auto url = build_r2_manifest_url("https://releases.helixscreen.org", "stable");
        REQUIRE(url == "https://releases.helixscreen.org/stable/manifest.json");
    }

    SECTION("beta channel URL") {
        auto url = build_r2_manifest_url("https://releases.helixscreen.org", "beta");
        REQUIRE(url == "https://releases.helixscreen.org/beta/manifest.json");
    }

    SECTION("dev channel URL") {
        auto url = build_r2_manifest_url("https://releases.helixscreen.org", "dev");
        REQUIRE(url == "https://releases.helixscreen.org/dev/manifest.json");
    }

    SECTION("custom base URL") {
        auto url = build_r2_manifest_url("https://my-cdn.example.com", "stable");
        REQUIRE(url == "https://my-cdn.example.com/stable/manifest.json");
    }

    SECTION("trailing slash in base URL handled") {
        auto url = build_r2_manifest_url("https://releases.helixscreen.org/", "stable");
        REQUIRE(url == "https://releases.helixscreen.org/stable/manifest.json");
    }
}

// ============================================================================
// R2 Manifest Parsing (same format across all channels)
// ============================================================================

TEST_CASE("Stable channel manifest uses same format as dev", "[update_channel][r2]") {
    const char* stable_manifest = R"({
        "version": "0.9.5",
        "tag": "v0.9.5",
        "notes": "Bug fixes and stability improvements",
        "published_at": "2026-02-07T10:00:00Z",
        "assets": {
            "pi": {"url": "https://releases.helixscreen.org/stable/helixscreen-pi-v0.9.5.tar.gz", "sha256": "abc123"},
            "pi32": {"url": "https://releases.helixscreen.org/stable/helixscreen-pi32-v0.9.5.tar.gz", "sha256": "def456"},
            "ad5m": {"url": "https://releases.helixscreen.org/stable/helixscreen-ad5m-v0.9.5.tar.gz", "sha256": "ghi789"},
            "k1": {"url": "https://releases.helixscreen.org/stable/helixscreen-k1-v0.9.5.tar.gz", "sha256": "jkl012"}
        }
    })";

    SECTION("stable manifest parses correctly for all platforms") {
        for (const auto& platform : {"pi", "pi32", "ad5m", "k1"}) {
            DevManifestInfo info;
            std::string error;
            REQUIRE(parse_dev_manifest(stable_manifest, platform, info, error));
            REQUIRE(info.valid);
            REQUIRE(info.version == "0.9.5");
            REQUIRE(info.tag == "v0.9.5");
            REQUIRE_FALSE(info.asset_url.empty());
            REQUIRE_FALSE(info.sha256.empty());
        }
    }

    SECTION("beta manifest with prerelease version parses correctly") {
        const char* beta_manifest = R"({
            "version": "1.0.0-beta.1",
            "tag": "v1.0.0-beta.1",
            "notes": "Beta release",
            "published_at": "2026-02-07T10:00:00Z",
            "assets": {
                "pi": {"url": "https://releases.helixscreen.org/beta/helixscreen-pi-v1.0.0-beta.1.tar.gz", "sha256": "betahash"}
            }
        })";

        DevManifestInfo info;
        std::string error;
        REQUIRE(parse_dev_manifest(beta_manifest, "pi", info, error));
        REQUIRE(info.valid);
        REQUIRE(info.version == "1.0.0-beta.1");
    }
}
