// SPDX-License-Identifier: GPL-3.0-or-later

#include "memory_utils.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <string>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/task_info.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace helix {

// Track peak memory on macOS (mach doesn't provide HWM)
static int64_t g_macos_peak_rss_kb = 0;

bool read_memory_stats(int64_t& rss_kb, int64_t& hwm_kb) {
    rss_kb = 0;
    hwm_kb = 0;

#ifdef __linux__
    std::ifstream status("/proc/self/status");
    if (!status.is_open())
        return false;

    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            rss_kb = std::stoll(line.substr(6));
        } else if (line.compare(0, 6, "VmHWM:") == 0) {
            hwm_kb = std::stoll(line.substr(6));
        }
    }
    return rss_kb > 0;

#elif defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) ==
        KERN_SUCCESS) {
        rss_kb = static_cast<int64_t>(info.resident_size / 1024);
        // Track peak ourselves since macOS doesn't provide HWM
        if (rss_kb > g_macos_peak_rss_kb) {
            g_macos_peak_rss_kb = rss_kb;
        }
        hwm_kb = g_macos_peak_rss_kb;
        return true;
    }
    return false;

#else
    return false;
#endif
}

bool read_private_dirty(int64_t& private_dirty_kb) {
    private_dirty_kb = 0;

#ifdef __linux__
    std::ifstream smaps("/proc/self/smaps_rollup");
    if (!smaps.is_open())
        return false;

    std::string line;
    while (std::getline(smaps, line)) {
        if (line.compare(0, 14, "Private_Dirty:") == 0) {
            private_dirty_kb = std::stoll(line.substr(14));
            return true;
        }
    }
#endif
    // macOS: private dirty not easily available
    return false;
}

MemoryInfo get_system_memory_info() {
    MemoryInfo info;

#ifdef __linux__
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open())
        return info;

    std::string line;
    while (std::getline(meminfo, line)) {
        // Parse lines like "MemTotal:       1234567 kB"
        if (line.compare(0, 9, "MemTotal:") == 0) {
            info.total_kb = static_cast<size_t>(std::stoll(line.substr(9)));
        } else if (line.compare(0, 13, "MemAvailable:") == 0) {
            info.available_kb = static_cast<size_t>(std::stoll(line.substr(13)));
        } else if (line.compare(0, 8, "MemFree:") == 0) {
            info.free_kb = static_cast<size_t>(std::stoll(line.substr(8)));
        }
    }

    // Fallback: if MemAvailable not present (older kernels), estimate from free + buffers/cache
    if (info.available_kb == 0 && info.free_kb > 0) {
        info.available_kb = info.free_kb; // Conservative estimate
    }

#elif defined(__APPLE__)
    // macOS: Get total physical memory via sysctl-style approach
    // For available, we use a rough heuristic based on VM stats
    mach_port_t host = mach_host_self();
    vm_size_t page_size;
    host_page_size(host, &page_size);

    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        // Free + inactive pages are roughly "available"
        size_t free_pages = vm_stats.free_count + vm_stats.inactive_count;
        info.free_kb = (vm_stats.free_count * page_size) / 1024;
        info.available_kb = (free_pages * page_size) / 1024;
    }

    // Get total memory
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        info.total_kb = static_cast<size_t>(memsize / 1024);
    }
#endif

    return info;
}

bool is_gcode_3d_render_safe(size_t file_size_bytes) {
    // Environment variable to force memory failure for testing
    // Usage: HELIX_FORCE_GCODE_MEMORY_FAIL=1 ./helix-screen --test
    const char* force_fail = std::getenv("HELIX_FORCE_GCODE_MEMORY_FAIL");
    if (force_fail && force_fail[0] == '1') {
        spdlog::debug(
            "[memory_utils] HELIX_FORCE_GCODE_MEMORY_FAIL=1 - forcing memory check failure");
        return false;
    }

    MemoryInfo mem = get_system_memory_info();

    // If we can't read memory info, be conservative - allow only small files
    if (mem.available_kb == 0) {
        return file_size_bytes < GCodeMemoryLimits::MAX_FILE_SIZE_CONSTRAINED;
    }

    // Check minimum available RAM
    if (mem.available_kb < GCodeMemoryLimits::MIN_AVAILABLE_KB) {
        return false;
    }

    // Determine max file size based on whether system is memory-constrained
    size_t max_file_size = mem.is_constrained() ? GCodeMemoryLimits::MAX_FILE_SIZE_CONSTRAINED
                                                : GCodeMemoryLimits::MAX_FILE_SIZE_NORMAL;

    if (file_size_bytes > max_file_size) {
        return false;
    }

    // Estimate memory needed: file size * expansion factor
    size_t estimated_memory_kb = (file_size_bytes * GCodeMemoryLimits::EXPANSION_FACTOR) / 1024;

    // Check if we have enough headroom (need at least 2x the estimated memory as buffer)
    return mem.available_kb > (estimated_memory_kb * 2);
}

} // namespace helix
