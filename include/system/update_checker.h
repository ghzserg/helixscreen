// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file update_checker.h
 * @brief Async update checker for HelixScreen
 *
 * Checks GitHub releases API for newer versions of HelixScreen.
 * Uses background thread to avoid blocking the UI during network operations.
 *
 * SAFETY: Downloads and installs require explicit user confirmation and are
 * blocked while a print is in progress. All errors are handled gracefully
 * to ensure the printer is never affected.
 */

#pragma once

#include "lvgl.h"
#include "subject_managed_panel.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/**
 * @brief Async update checker for HelixScreen
 *
 * Checks GitHub releases API to determine if a newer version is available.
 * Rate-limited to 1 check per hour minimum.
 *
 * Usage:
 * @code
 * auto& checker = UpdateChecker::instance();
 * checker.init();
 * checker.check_for_updates([](UpdateChecker::Status status,
 *                              std::optional<UpdateChecker::ReleaseInfo> info) {
 *     if (status == UpdateChecker::Status::UpdateAvailable && info) {
 *         spdlog::info("Update available: {}", info->version);
 *     }
 * });
 * // ... on shutdown:
 * checker.shutdown();
 * @endcode
 */
class UpdateChecker {
  public:
    static constexpr const char* DEFAULT_R2_BASE_URL = "https://releases.helixscreen.org";

    /**
     * @brief Release information from GitHub
     */
    struct ReleaseInfo {
        std::string version;       ///< Stripped version (e.g., "1.2.3")
        std::string tag_name;      ///< Original tag (e.g., "v1.2.3")
        std::string download_url;  ///< Asset download URL for binary
        std::string release_notes; ///< Body markdown
        std::string published_at;  ///< ISO 8601 timestamp
        std::string sha256;        ///< SHA-256 hash (for dev channel verification)
    };

    /**
     * @brief Update check status
     */
    enum class Status {
        Idle = 0,            ///< No check in progress
        Checking = 1,        ///< HTTP request pending
        UpdateAvailable = 2, ///< New version found
        UpToDate = 3,        ///< Already on latest
        Error = 4            ///< Check failed
    };

    /**
     * @brief Update channel selection
     */
    enum class UpdateChannel { Stable = 0, Beta = 1, Dev = 2 };

    /**
     * @brief Download and install status
     */
    enum class DownloadStatus {
        Idle = 0,        ///< No download in progress
        Confirming = 1,  ///< User confirming download
        Downloading = 2, ///< Download in progress
        Verifying = 3,   ///< Verifying tarball integrity
        Installing = 4,  ///< Running install.sh
        Complete = 5,    ///< Install succeeded
        Error = 6        ///< Download/install failed
    };

    /**
     * @brief Get singleton instance
     */
    static UpdateChecker& instance();

    /**
     * @brief Callback invoked when check completes
     * @param status Final status of the check
     * @param info Release info if update is available, nullopt otherwise
     *
     * Callback is invoked on the LVGL thread (via ui_queue_update).
     */
    using Callback = std::function<void(Status, std::optional<ReleaseInfo>)>;

    /**
     * @brief Check for updates asynchronously
     *
     * Spawns background thread to check GitHub releases API.
     * Callback is invoked on LVGL thread when check completes.
     *
     * Rate limited: If called within MIN_CHECK_INTERVAL of last check,
     * returns cached result immediately instead of making a new request.
     *
     * @param callback Optional callback for result notification
     */
    void check_for_updates(Callback callback = nullptr);

    /**
     * @brief Get current status (thread-safe)
     * @return Current status enum value
     */
    Status get_status() const;

    /**
     * @brief Get cached update info if available (thread-safe)
     * @return ReleaseInfo if update is cached, nullopt otherwise
     */
    std::optional<ReleaseInfo> get_cached_update() const;

    /**
     * @brief Check if an update is available (thread-safe)
     * @return true if update is available and cached
     */
    bool has_update_available() const;

    /**
     * @brief Get error message from last failed check (thread-safe)
     * @return Error message, or empty string if no error
     */
    std::string get_error_message() const;

    /**
     * @brief Clear cached update information
     *
     * Resets status to Idle and clears cached release info.
     */
    void clear_cache();

    /**
     * @brief Initialize the update checker
     *
     * Call once at startup. Idempotent - safe to call multiple times.
     */
    void init();

    /**
     * @brief Shutdown and cleanup
     *
     * Cancels any pending check and joins worker thread.
     * Idempotent - safe to call multiple times.
     */
    void shutdown();

    // LVGL subjects for UI binding (update check)
    lv_subject_t* status_subject();
    lv_subject_t* checking_subject();
    lv_subject_t* version_text_subject();
    lv_subject_t* new_version_subject();

    // Download and install
    void start_download();
    void cancel_download();
    DownloadStatus get_download_status() const;
    int get_download_progress() const;
    std::string get_download_error() const;

    // LVGL subjects for download UI
    lv_subject_t* download_status_subject();
    lv_subject_t* download_progress_subject();
    lv_subject_t* download_text_subject();

    // Download state reporting (public for tests and SettingsPanel)
    void report_download_status(DownloadStatus status, int progress, const std::string& text,
                                const std::string& error = "");
    std::string get_download_path() const;
    std::string get_platform_asset_name() const;

    /** @brief Get the configured update channel */
    UpdateChannel get_channel() const;

    /** @brief Get platform key for current build ("pi", "ad5m", "k1") */
    static std::string get_platform_key();

    /**
     * @brief Find a local install.sh by searching well-known paths
     * @param extra_search_paths Additional paths to search (prepended to default list)
     * @return Path to install.sh if found, empty string otherwise
     *
     * Searches exe-relative path first, then well-known install locations.
     * Used as fallback when installer cannot be extracted from update tarball.
     */
    static std::string
    find_local_installer(const std::vector<std::string>& extra_search_paths = {});

    /**
     * @brief Extract install.sh from a release tarball into a directory
     * @param tarball_path Path to the .tar.gz release tarball
     * @param extract_dir  Directory to extract into (helixscreen/ subdir created inside it)
     * @return Path to the extracted installer (chmod +x applied), or empty string on failure
     *
     * Tries GNU tar xzf first; falls back to cp+gunzip+tar for BusyBox compatibility.
     * The fallback avoids gunzip -k (keep-original) which is absent on older BusyBox builds.
     * Exposed as public static for unit testing.
     */
    static std::string extract_installer_from_tarball(const std::string& tarball_path,
                                                      const std::string& extract_dir);

    /** @brief Check if a version is dismissed (user chose to ignore) */
    bool is_version_dismissed(const std::string& version) const;

    /** @brief Dismiss the current cached update version (persists to config) */
    void dismiss_current_version();

    /** @brief Start automatic update checking (15s initial, then 24h periodic) */
    void start_auto_check();

    /** @brief Stop automatic update checking */
    void stop_auto_check();

    // LVGL subjects for notification modal
    lv_subject_t* release_notes_subject();
    lv_subject_t* changelog_visible_subject();

    /** @brief Show the update notification modal */
    void show_update_notification();

    /** @brief Hide the update notification modal */
    void hide_update_notification();

  private:
    UpdateChecker() = default;
    ~UpdateChecker();

    // Non-copyable
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    /**
     * @brief Worker thread entry point
     */
    void do_check();

    // Channel-specific fetch methods
    bool fetch_stable_release(ReleaseInfo& info, std::string& error);
    bool fetch_beta_release(ReleaseInfo& info, std::string& error);
    bool fetch_dev_release(ReleaseInfo& info, std::string& error);

    // R2 CDN fetch (used as primary source before GitHub fallback)
    bool fetch_r2_manifest(const std::string& channel, ReleaseInfo& info, std::string& error);
    std::string get_r2_base_url() const;

    /**
     * @brief Report result to callback on LVGL thread
     * @param status Final status
     * @param info Release info (nullopt if not available)
     * @param error Error message (empty if no error)
     */
    void report_result(Status status, std::optional<ReleaseInfo> info, const std::string& error);

    void init_subjects();

    // State (protected by mutex_)
    std::atomic<Status> status_{Status::Idle};
    std::optional<ReleaseInfo> cached_info_;
    std::string error_message_;
    mutable std::mutex mutex_;

    // Rate limiting
    std::chrono::steady_clock::time_point last_check_time_{};
    static constexpr auto MIN_CHECK_INTERVAL = std::chrono::hours{1};

    // Threading
    std::thread worker_thread_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> initialized_{false};
    Callback pending_callback_;

    // Channel cached on main thread before worker spawns (Config is not thread-safe)
    UpdateChannel cached_channel_{UpdateChannel::Stable};
    std::string cached_dev_url_;
    std::string cached_r2_base_url_;

    // LVGL subjects for UI binding (update check)
    lv_subject_t status_subject_{};
    lv_subject_t checking_subject_{};
    lv_subject_t version_text_subject_{};
    lv_subject_t new_version_subject_{};

    // String buffers for string subjects (must outlive subjects)
    char version_text_buf_[256]{};
    char new_version_buf_[64]{};

    // Download state
    std::atomic<DownloadStatus> download_status_{DownloadStatus::Idle};
    std::atomic<int> download_progress_{0};
    std::string download_error_;
    std::thread download_thread_;
    std::atomic<bool> download_cancelled_{false};

    // Download LVGL subjects
    lv_subject_t download_status_subject_{};
    lv_subject_t download_progress_subject_{};
    lv_subject_t download_text_subject_{};
    char download_text_buf_[256]{};

    // Download internals
    void do_download();
    void do_install(const std::string& tarball_path);

    /** @brief Validate downloaded tarball contains binary for correct architecture */
    bool validate_elf_architecture(const std::string& tarball_path);

    // Auto-check timer
    lv_timer_t* auto_check_timer_{nullptr};

    // Notification modal
    lv_obj_t* notify_modal_{nullptr};

    // Notification subjects
    lv_subject_t release_notes_subject_{};
    lv_subject_t changelog_visible_subject_{};
    char release_notes_buf_[2048]{};

    SubjectManager subjects_;
    bool subjects_initialized_{false};
};
