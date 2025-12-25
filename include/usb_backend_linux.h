// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __linux__

#include "usb_backend.h"

#include <atomic>
#include <mutex>
#include <thread>

/**
 * @brief Linux USB backend using inotify and /proc/mounts
 *
 * Monitors USB drive mount/unmount events using:
 * - inotify watch on /proc/mounts for mount changes
 * - Parsing /proc/mounts to detect USB drives (looking for /dev/sd* on /media or /mnt)
 * - statvfs() for capacity information
 *
 * Design notes:
 * - /proc/mounts changes whenever any filesystem is mounted/unmounted
 * - We filter for USB-like mounts (block devices on common USB mount points)
 * - Background thread polls inotify for mount changes
 */
class UsbBackendLinux : public UsbBackend {
  public:
    UsbBackendLinux();
    ~UsbBackendLinux() override;

    // ========================================================================
    // UsbBackend Interface Implementation
    // ========================================================================

    UsbError start() override;
    void stop() override;
    bool is_running() const override;

    void set_event_callback(EventCallback callback) override;

    UsbError get_connected_drives(std::vector<UsbDrive>& drives) override;
    UsbError scan_for_gcode(const std::string& mount_path, std::vector<UsbGcodeFile>& files,
                            int max_depth = 3) override;

  private:
    /**
     * @brief Parse /proc/mounts and return USB drives
     */
    std::vector<UsbDrive> parse_mounts();

    /**
     * @brief Check if a mount entry looks like a USB drive
     * @param device Device path (e.g., /dev/sda1)
     * @param mount_point Mount point path
     * @param fs_type Filesystem type
     * @return true if this appears to be a USB drive
     */
    bool is_usb_mount(const std::string& device, const std::string& mount_point,
                      const std::string& fs_type);

    /**
     * @brief Get volume label for a device
     */
    std::string get_volume_label(const std::string& device, const std::string& mount_point);

    /**
     * @brief Get capacity info for a mount point
     */
    void get_capacity(const std::string& mount_point, uint64_t& total, uint64_t& available);

    /**
     * @brief Background thread function - monitors /proc/mounts via inotify
     */
    void monitor_thread_func();

    /**
     * @brief Recursively scan directory for .gcode files
     */
    void scan_directory(const std::string& path, std::vector<UsbGcodeFile>& files,
                        int current_depth, int max_depth);

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex mutex_;
    EventCallback event_callback_;
    std::vector<UsbDrive> cached_drives_;

    // inotify (when available)
    int inotify_fd_{-1};
    int mounts_watch_fd_{-1};
    std::thread monitor_thread_;

    // Polling fallback (when inotify unavailable)
    // Note: We compare actual content rather than mtime because /proc/mounts
    // is often a symlink to /proc/self/mounts, and symlink mtime never changes.
    bool use_polling_{false};
    std::string last_mounts_content_;

    /**
     * @brief Read contents of /proc/mounts for polling comparison
     */
    std::string read_mounts_content();
};

#endif // __linux__
