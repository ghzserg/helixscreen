// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_data_source.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

// For HTTP requests - use libhv which is already in the project
#include "hv/hurl.h"
#include "hv/requests.h"

namespace helix {
namespace gcode {

// =============================================================================
// GCodeDataSource base class
// =============================================================================

std::optional<std::string> GCodeDataSource::read_line(uint64_t offset, size_t max_length) {
    auto data = read_range(offset, static_cast<uint32_t>(max_length));
    if (data.empty() && offset < file_size()) {
        return std::nullopt; // Read failed
    }
    if (data.empty()) {
        return ""; // At end of file
    }

    // Find newline
    auto newline_pos = std::find(data.begin(), data.end(), '\n');
    size_t line_len = std::distance(data.begin(), newline_pos);

    // Strip trailing \r if present
    std::string line(data.begin(), data.begin() + line_len);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    return line;
}

std::vector<char> GCodeDataSource::read_all() {
    uint64_t size = file_size();
    // Guard against truncation - read_range takes uint32_t
    if (size > std::numeric_limits<uint32_t>::max()) {
        spdlog::error("[DataSource] File too large for read_all(): {} bytes (max {})",
                      size, std::numeric_limits<uint32_t>::max());
        return {};
    }
    return read_range(0, static_cast<uint32_t>(size));
}

// =============================================================================
// FileDataSource
// =============================================================================

FileDataSource::FileDataSource(const std::string& filepath) : filepath_(filepath) {
    file_ = std::fopen(filepath.c_str(), "rb");
    if (file_) {
        // Get file size using 64-bit safe fseeko/ftello (handles > 2GB on 32-bit ARM)
        fseeko(file_, 0, SEEK_END);
        size_ = static_cast<uint64_t>(ftello(file_));
        fseeko(file_, 0, SEEK_SET);
        spdlog::debug("[FileDataSource] Opened '{}' ({} bytes)", filepath, size_);
    } else {
        spdlog::error("[FileDataSource] Failed to open '{}'", filepath);
    }
}

FileDataSource::~FileDataSource() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

FileDataSource::FileDataSource(FileDataSource&& other) noexcept
    : filepath_(std::move(other.filepath_)), file_(other.file_), size_(other.size_) {
    other.file_ = nullptr;
    other.size_ = 0;
}

FileDataSource& FileDataSource::operator=(FileDataSource&& other) noexcept {
    if (this != &other) {
        if (file_) {
            std::fclose(file_);
        }
        filepath_ = std::move(other.filepath_);
        file_ = other.file_;
        size_ = other.size_;
        other.file_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

std::vector<char> FileDataSource::read_range(uint64_t offset, uint32_t length) {
    if (!file_ || offset >= size_) {
        return {};
    }

    // Clamp length to available data
    uint32_t available = static_cast<uint32_t>(std::min<uint64_t>(length, size_ - offset));
    if (available == 0) {
        return {};
    }

    std::vector<char> buffer(available);

    // Seek using 64-bit safe fseeko (handles files > 2GB on 32-bit ARM)
    if (fseeko(file_, static_cast<off_t>(offset), SEEK_SET) != 0) {
        spdlog::error("[FileDataSource] Seek failed at offset {}", offset);
        return {};
    }

    size_t read = std::fread(buffer.data(), 1, available, file_);
    if (read != available) {
        spdlog::warn("[FileDataSource] Short read: requested {}, got {}", available, read);
        buffer.resize(read);
    }

    return buffer;
}

uint64_t FileDataSource::file_size() const {
    return size_;
}

bool FileDataSource::supports_range_requests() const {
    return true; // Local files always support random access
}

std::string FileDataSource::source_name() const {
    return filepath_;
}

bool FileDataSource::is_valid() const {
    return file_ != nullptr;
}

// =============================================================================
// MoonrakerDataSource
// =============================================================================

MoonrakerDataSource::MoonrakerDataSource(const std::string& moonraker_url,
                                         const std::string& gcode_path)
    : moonraker_url_(moonraker_url), gcode_path_(gcode_path) {

    // Normalize URL (remove trailing slash)
    while (!moonraker_url_.empty() && moonraker_url_.back() == '/') {
        moonraker_url_.pop_back();
    }

    // Try to get file metadata
    valid_ = fetch_metadata();

    if (valid_) {
        spdlog::debug("[MoonrakerDataSource] Initialized for '{}' ({} bytes)", gcode_path_, size_);
    }
}

MoonrakerDataSource::~MoonrakerDataSource() {
    // Clean up temp file if we created one
    if (!temp_file_path_.empty()) {
        std::filesystem::remove(temp_file_path_);
        spdlog::debug("[MoonrakerDataSource] Removed temp file: {}", temp_file_path_);
    }
}

std::string MoonrakerDataSource::get_download_url() const {
    // URL-encode the path for safety
    std::string encoded_path = HUrl::escape(gcode_path_, "/.-_");
    return moonraker_url_ + "/server/files/gcodes/" + encoded_path;
}

bool MoonrakerDataSource::fetch_metadata() {
    if (metadata_fetched_) {
        return valid_;
    }
    metadata_fetched_ = true;

    // Use Moonraker metadata endpoint to get file size
    std::string encoded_filename = HUrl::escape(gcode_path_);
    std::string url = moonraker_url_ + "/server/files/metadata?filename=" + encoded_filename;

    auto resp = requests::get(url.c_str());

    if (!resp) {
        spdlog::error("[MoonrakerDataSource] Metadata request failed for '{}'", gcode_path_);
        return false;
    }

    if (resp->status_code != 200) {
        spdlog::error("[MoonrakerDataSource] Metadata request returned HTTP {}",
                      static_cast<int>(resp->status_code));
        return false;
    }

    // Parse JSON response for "size" field
    // Simple parsing without JSON library
    const std::string& resp_str = resp->body;
    auto size_pos = resp_str.find("\"size\"");
    if (size_pos == std::string::npos) {
        spdlog::error("[MoonrakerDataSource] No 'size' in metadata response");
        return false;
    }

    // Find the number after "size":
    size_pos = resp_str.find(':', size_pos);
    if (size_pos == std::string::npos) {
        return false;
    }
    size_pos++; // Skip colon

    // Skip whitespace
    while (size_pos < resp_str.size() && std::isspace(resp_str[size_pos])) {
        size_pos++;
    }

    // Parse number
    char* end = nullptr;
    size_ = std::strtoull(resp_str.c_str() + size_pos, &end, 10);

    if (size_ == 0) {
        spdlog::error("[MoonrakerDataSource] Failed to parse file size");
        return false;
    }

    spdlog::debug("[MoonrakerDataSource] File size: {} bytes", size_);
    return true;
}

bool MoonrakerDataSource::probe_range_support() {
    if (range_support_probed_) {
        return range_support_;
    }
    range_support_probed_ = true;

    std::string url = get_download_url();

    // Make a HEAD request to check for Accept-Ranges header
    auto resp = requests::head(url.c_str());

    if (!resp) {
        spdlog::warn("[MoonrakerDataSource] Range probe failed");
        return false;
    }

    // Check for Accept-Ranges: bytes header
    auto it = resp->headers.find("Accept-Ranges");
    if (it != resp->headers.end() && it->second.find("bytes") != std::string::npos) {
        range_support_ = true;
    }

    // Also check lowercase version
    it = resp->headers.find("accept-ranges");
    if (it != resp->headers.end() && it->second.find("bytes") != std::string::npos) {
        range_support_ = true;
    }

    if (range_support_) {
        spdlog::info("[MoonrakerDataSource] Server supports range requests");
    } else {
        spdlog::info("[MoonrakerDataSource] Server does NOT support range requests, "
                     "will use temp file fallback");
    }

    return range_support_;
}

std::vector<char> MoonrakerDataSource::http_range_request(uint64_t offset, uint32_t length) {
    std::string url = get_download_url();

    // Build range header value
    char range_value[64];
    std::snprintf(range_value, sizeof(range_value), "bytes=%" PRIu64 "-%" PRIu64, offset,
                  offset + length - 1);

    // Create request with Range header
    http_headers headers;
    headers["Range"] = range_value;

    auto resp = requests::get(url.c_str(), headers);

    if (!resp) {
        spdlog::error("[MoonrakerDataSource] Range request failed");
        return {};
    }

    // 206 Partial Content is success for range request
    // 200 means server ignored range header and returned everything
    if (resp->status_code == 206) {
        return std::vector<char>(resp->body.begin(), resp->body.end());
    } else if (resp->status_code == 200) {
        spdlog::warn("[MoonrakerDataSource] Server returned 200 instead of 206, "
                     "range requests not properly supported");
        range_support_ = false;
        // Return the portion we wanted (if response is large enough)
        if (resp->body.size() > offset) {
            size_t available = std::min<size_t>(length, resp->body.size() - offset);
            return std::vector<char>(resp->body.begin() + offset,
                                     resp->body.begin() + offset + available);
        }
        return {};
    }

    spdlog::error("[MoonrakerDataSource] Range request returned HTTP {}",
                  static_cast<int>(resp->status_code));
    return {};
}

std::vector<char> MoonrakerDataSource::read_range(uint64_t offset, uint32_t length) {
    // If we've fallen back to temp file, use that
    if (fallback_source_) {
        return fallback_source_->read_range(offset, length);
    }

    if (!is_valid()) {
        return {};
    }

    // Check if server supports range requests
    if (!range_support_probed_) {
        probe_range_support();
    }

    if (range_support_) {
        return http_range_request(offset, length);
    }

    // Fall back to downloading entire file
    spdlog::info("[MoonrakerDataSource] Falling back to temp file download");
    if (!download_to_temp()) {
        return {};
    }

    return fallback_source_->read_range(offset, length);
}

bool MoonrakerDataSource::download_to_temp() {
    if (fallback_source_) {
        return true; // Already downloaded
    }

    // Generate temp file path
    temp_file_path_ = "/tmp/helix_gcode_" + std::to_string(std::hash<std::string>{}(gcode_path_)) +
                      "_" + std::to_string(time(nullptr)) + ".gcode";

    std::string url = get_download_url();

    spdlog::info("[MoonrakerDataSource] Downloading {} to {}", url, temp_file_path_);

    // Use libhv to download
    auto resp = requests::get(url.c_str());

    if (!resp) {
        spdlog::error("[MoonrakerDataSource] Download failed");
        return false;
    }

    if (resp->status_code != 200) {
        spdlog::error("[MoonrakerDataSource] Download returned HTTP {}",
                      static_cast<int>(resp->status_code));
        return false;
    }

    // Write to temp file
    FILE* file = std::fopen(temp_file_path_.c_str(), "wb");
    if (!file) {
        spdlog::error("[MoonrakerDataSource] Failed to create temp file: {}", temp_file_path_);
        return false;
    }

    size_t written = std::fwrite(resp->body.data(), 1, resp->body.size(), file);
    std::fclose(file);

    if (written != resp->body.size()) {
        spdlog::error("[MoonrakerDataSource] Failed to write temp file");
        std::filesystem::remove(temp_file_path_);
        return false;
    }

    // Create file data source from temp file
    fallback_source_ = std::make_unique<FileDataSource>(temp_file_path_);

    if (!fallback_source_->is_valid()) {
        spdlog::error("[MoonrakerDataSource] Failed to open downloaded temp file");
        fallback_source_.reset();
        std::filesystem::remove(temp_file_path_);
        return false;
    }

    size_ = fallback_source_->file_size();
    spdlog::info("[MoonrakerDataSource] Download complete: {} bytes", size_);

    return true;
}

uint64_t MoonrakerDataSource::file_size() const {
    return size_;
}

bool MoonrakerDataSource::supports_range_requests() const {
    if (fallback_source_) {
        return true; // Temp file supports range requests
    }
    return range_support_;
}

std::string MoonrakerDataSource::source_name() const {
    return "moonraker://" + gcode_path_;
}

bool MoonrakerDataSource::is_valid() const {
    if (fallback_source_) {
        return fallback_source_->is_valid();
    }
    return valid_;
}

// =============================================================================
// MemoryDataSource
// =============================================================================

MemoryDataSource::MemoryDataSource(std::string content, std::string name)
    : data_(content.begin(), content.end()), name_(std::move(name)) {
}

MemoryDataSource::MemoryDataSource(std::vector<char> data, std::string name)
    : data_(std::move(data)), name_(std::move(name)) {
}

std::vector<char> MemoryDataSource::read_range(uint64_t offset, uint32_t length) {
    if (offset >= data_.size()) {
        return {};
    }

    size_t available = std::min<size_t>(length, data_.size() - offset);
    return std::vector<char>(data_.begin() + offset, data_.begin() + offset + available);
}

uint64_t MemoryDataSource::file_size() const {
    return data_.size();
}

bool MemoryDataSource::supports_range_requests() const {
    return true;
}

std::string MemoryDataSource::source_name() const {
    return name_;
}

bool MemoryDataSource::is_valid() const {
    return true;
}

} // namespace gcode
} // namespace helix
