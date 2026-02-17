// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

struct BundleOptions {
    bool include_klipper_logs = false;
    bool include_moonraker_logs = false;
};

struct BundleResult {
    bool success = false;
    std::string share_code;
    std::string error_message;
};

class DebugBundleCollector {
  public:
    /// Collect all debug data into JSON
    static nlohmann::json collect(const BundleOptions& options = {});

    /// Collect, compress, and upload asynchronously.
    /// Callback is invoked on the UI thread via helix::ui::queue_update().
    using ResultCallback = std::function<void(const BundleResult&)>;
    static void upload_async(const BundleOptions& options, ResultCallback callback);

    /// Individual collectors (public for testing)
    static nlohmann::json collect_system_info();
    static nlohmann::json collect_printer_info();
    static std::string collect_log_tail(int num_lines = 500);
    static std::string collect_crash_txt();
    static nlohmann::json collect_sanitized_settings();
    static std::string collect_klipper_log_tail(int num_lines = 500);
    static std::string collect_moonraker_log_tail(int num_lines = 200);

    /// Gzip compression using zlib
    static std::vector<uint8_t> gzip_compress(const std::string& data);

  private:
    static constexpr const char* WORKER_URL = "https://crash.helixscreen.org/v1/debug-bundle";
    static constexpr const char* INGEST_API_KEY = "hx-tel-v1-a7f3c9e2d1b84056";

    /// Recursively strip sensitive keys from JSON
    static nlohmann::json sanitize_json(const nlohmann::json& input);

    /// Check if a key name matches a sensitive pattern
    static bool is_sensitive_key(const std::string& key);
};

} // namespace helix
