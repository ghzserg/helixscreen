// SPDX-License-Identifier: GPL-3.0-or-later
#include "android_asset_extractor.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace helix {

AssetExtractionResult extract_assets_if_needed(const std::string& source_dir,
                                               const std::string& target_dir,
                                               const std::string& current_version) {
    // Check if target already has matching version
    fs::path version_file = fs::path(target_dir) / "VERSION";
    if (fs::exists(version_file)) {
        std::ifstream ifs(version_file);
        std::string existing_version;
        std::getline(ifs, existing_version);
        if (existing_version == current_version) {
            spdlog::debug("Assets already at version {}, skipping extraction", current_version);
            return AssetExtractionResult::ALREADY_CURRENT;
        }
        spdlog::info("Asset version mismatch: have '{}', need '{}' - re-extracting",
                     existing_version, current_version);
    }

    // Create target directory if needed
    std::error_code ec;
    fs::create_directories(target_dir, ec);
    if (ec) {
        spdlog::error("Failed to create target directory '{}': {}", target_dir, ec.message());
        return AssetExtractionResult::FAILED;
    }

    // Verify source directory exists
    if (!fs::exists(source_dir) || !fs::is_directory(source_dir)) {
        spdlog::error("Source directory '{}' does not exist or is not a directory", source_dir);
        return AssetExtractionResult::FAILED;
    }

    // Copy all files recursively from source to target
    fs::copy(source_dir, target_dir,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("Failed to copy assets from '{}' to '{}': {}", source_dir, target_dir,
                      ec.message());
        return AssetExtractionResult::FAILED;
    }

    // Write version marker
    {
        std::ofstream ofs(version_file, std::ios::trunc);
        if (!ofs) {
            spdlog::error("Failed to write version marker to '{}'", version_file.string());
            return AssetExtractionResult::FAILED;
        }
        ofs << current_version;
    }

    spdlog::info("Extracted assets to '{}' (version {})", target_dir, current_version);
    return AssetExtractionResult::EXTRACTED;
}

#ifdef __ANDROID__

#include "helix_version.h"

#include <SDL.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstdlib>
#include <jni.h>
#include <vector>

// Get AAssetManager from the Android Activity via JNI
static AAssetManager* get_asset_manager() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        spdlog::error("[AndroidAssets] Failed to get JNI env or activity");
        return nullptr;
    }

    jclass activity_class = env->GetObjectClass(activity);
    jmethodID get_assets =
        env->GetMethodID(activity_class, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject java_asset_mgr = env->CallObjectMethod(activity, get_assets);

    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);

    if (!java_asset_mgr) {
        spdlog::error("[AndroidAssets] Failed to get AssetManager from activity");
        return nullptr;
    }

    AAssetManager* mgr = AAssetManager_fromJava(env, java_asset_mgr);
    env->DeleteLocalRef(java_asset_mgr);
    return mgr;
}

// Recursively extract all files from an APK asset directory to the filesystem
static int extract_asset_dir(AAssetManager* mgr, const std::string& asset_path,
                             const std::string& target_path) {
    int count = 0;

    // Create target directory
    std::error_code ec;
    fs::create_directories(target_path, ec);
    if (ec) {
        spdlog::error("[AndroidAssets] Failed to create dir '{}': {}", target_path, ec.message());
        return -1;
    }

    AAssetDir* dir = AAssetManager_openDir(mgr, asset_path.c_str());
    if (!dir) {
        spdlog::error("[AndroidAssets] Failed to open asset dir '{}'", asset_path);
        return -1;
    }

    // List and copy all files in this directory
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(dir)) != nullptr) {
        std::string asset_file =
            asset_path.empty() ? std::string(filename) : asset_path + "/" + filename;
        std::string target_file = target_path + "/" + filename;

        AAsset* asset = AAssetManager_open(mgr, asset_file.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            spdlog::warn("[AndroidAssets] Could not open asset '{}'", asset_file);
            continue;
        }

        off_t size = AAsset_getLength(asset);
        std::vector<char> buf(size);
        int bytes_read = AAsset_read(asset, buf.data(), size);
        AAsset_close(asset);

        if (bytes_read != size) {
            spdlog::warn("[AndroidAssets] Short read for '{}': {} of {}", asset_file, bytes_read,
                         size);
            continue;
        }

        std::ofstream ofs(target_file, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            spdlog::warn("[AndroidAssets] Could not write '{}'", target_file);
            continue;
        }
        ofs.write(buf.data(), size);
        count++;
    }

    AAssetDir_close(dir);
    return count;
}

// Recursively extract a known directory tree from APK assets.
// AAssetDir only lists files (not subdirs), so we need to know the subdirectory
// names in advance. For ui_xml/ we have a known structure: ui_xml/components/.
static int extract_known_tree(AAssetManager* mgr, const std::string& asset_root,
                              const std::string& target_root,
                              const std::vector<std::string>& subdirs) {
    int total = 0;

    // Extract files from root directory
    int n = extract_asset_dir(mgr, asset_root, target_root);
    if (n < 0)
        return -1;
    total += n;

    // Extract each known subdirectory
    for (const auto& sub : subdirs) {
        std::string asset_sub = asset_root + "/" + sub;
        std::string target_sub = target_root + "/" + sub;
        n = extract_asset_dir(mgr, asset_sub, target_sub);
        if (n >= 0) {
            total += n;
        }
    }

    return total;
}

void android_extract_assets_if_needed() {
    const char* internal_path = SDL_AndroidGetInternalStoragePath();
    if (!internal_path) {
        spdlog::error("[AndroidAssets] Could not get internal storage path from SDL");
        return;
    }

    std::string target_dir = std::string(internal_path) + "/data";
    spdlog::info("[AndroidAssets] Target directory: {}", target_dir);

    // Check version marker to skip extraction if already current
    fs::path version_file = fs::path(target_dir) / "VERSION";
    std::string current_version = helix_version();
    if (fs::exists(version_file)) {
        std::ifstream ifs(version_file);
        std::string existing;
        std::getline(ifs, existing);
        if (existing == current_version) {
            spdlog::info("[AndroidAssets] Assets already at version {}, skipping", current_version);
            setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
            return;
        }
        spdlog::info("[AndroidAssets] Version mismatch: have '{}', need '{}'", existing,
                     current_version);
    }

    AAssetManager* mgr = get_asset_manager();
    if (!mgr) {
        spdlog::error("[AndroidAssets] Could not get AAssetManager, app will lack UI resources");
        setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
        return;
    }

    // Extract the three asset trees from the APK.
    // AAssetDir_getNextFileName() only returns files, not subdirectories,
    // so we enumerate known subdirectory structure explicitly.
    int total = 0;

    // ui_xml/ tree
    int n = extract_known_tree(mgr, "ui_xml", target_dir + "/ui_xml",
                               {"components", "translations", "ultrawide"});
    if (n > 0)
        total += n;
    spdlog::info("[AndroidAssets] Extracted {} files from ui_xml/", n);

    // assets/ tree (images has subdirs: ams, flags, printers)
    n = extract_known_tree(
        mgr, "assets", target_dir + "/assets",
        {"fonts", "images", "images/ams", "images/flags", "images/printers", "test_gcodes"});
    if (n > 0)
        total += n;
    spdlog::info("[AndroidAssets] Extracted {} files from assets/", n);

    // config/ tree
    n = extract_known_tree(mgr, "config", target_dir + "/config",
                           {"platform", "presets", "print_start_profiles", "printer_database.d",
                            "sounds", "themes", "themes/defaults"});
    if (n > 0)
        total += n;
    spdlog::info("[AndroidAssets] Extracted {} files from config/", n);

    spdlog::info("[AndroidAssets] Total: {} files extracted to '{}'", total, target_dir);

    // Write version marker
    {
        std::error_code ec;
        fs::create_directories(target_dir, ec);
        std::ofstream ofs(version_file, std::ios::trunc);
        if (ofs) {
            ofs << current_version;
        }
    }

    // Set HELIX_DATA_DIR so ensure_project_root_cwd() chdir's here
    setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
    spdlog::info("[AndroidAssets] Set HELIX_DATA_DIR={}", target_dir);
}

#endif // __ANDROID__

} // namespace helix
