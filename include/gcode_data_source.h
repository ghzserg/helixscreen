// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helix {
namespace gcode {

/**
 * @brief Abstract interface for reading G-code data
 *
 * Provides a uniform interface for reading G-code from various sources:
 * - Local files (FileDataSource)
 * - Moonraker HTTP API with range requests (MoonrakerDataSource)
 * - In-memory buffers (for testing)
 *
 * This abstraction enables streaming G-code parsing on memory-constrained
 * devices by loading only the needed byte ranges rather than entire files.
 */
class GCodeDataSource {
  public:
    virtual ~GCodeDataSource() = default;

    /**
     * @brief Read a byte range from the source
     *
     * @param offset Starting byte position
     * @param length Number of bytes to read
     * @return Vector of bytes, or empty if read failed
     *
     * @note May return fewer bytes than requested if at end of source
     */
    virtual std::vector<char> read_range(uint64_t offset, uint32_t length) = 0;

    /**
     * @brief Get total size of the data source
     * @return Size in bytes, or 0 if unknown
     */
    virtual uint64_t file_size() const = 0;

    /**
     * @brief Check if source supports efficient range requests
     *
     * For HTTP sources, this indicates whether Range headers work.
     * For local files, this is always true.
     *
     * @return true if range requests are efficient
     */
    virtual bool supports_range_requests() const = 0;

    /**
     * @brief Get descriptive name of the source
     * @return Source description (e.g., filename, URL)
     */
    virtual std::string source_name() const = 0;

    /**
     * @brief Check if source is valid/accessible
     * @return true if source can be read
     */
    virtual bool is_valid() const = 0;

    /**
     * @brief Read a single line starting at offset
     *
     * Reads characters until newline or end of source.
     * Convenience method built on read_range().
     *
     * @param offset Starting byte position
     * @param max_length Maximum line length (default 4096)
     * @return Line content (without newline), or nullopt if read failed
     */
    std::optional<std::string> read_line(uint64_t offset, size_t max_length = 4096);

    /**
     * @brief Read entire source into memory
     *
     * Warning: Can use a lot of memory for large files!
     * Use read_range() for streaming access instead.
     *
     * @return All bytes from source
     */
    std::vector<char> read_all();
};

/**
 * @brief Data source for local files
 *
 * Uses standard file I/O with fseek/fread for efficient random access.
 * This is the most efficient source for local G-code files.
 */
class FileDataSource : public GCodeDataSource {
  public:
    /**
     * @brief Create data source from file path
     * @param filepath Path to local file
     */
    explicit FileDataSource(const std::string& filepath);

    ~FileDataSource() override;

    // Non-copyable
    FileDataSource(const FileDataSource&) = delete;
    FileDataSource& operator=(const FileDataSource&) = delete;

    // Moveable
    FileDataSource(FileDataSource&& other) noexcept;
    FileDataSource& operator=(FileDataSource&& other) noexcept;

    std::vector<char> read_range(uint64_t offset, uint32_t length) override;
    uint64_t file_size() const override;
    bool supports_range_requests() const override;
    std::string source_name() const override;
    bool is_valid() const override;

    /**
     * @brief Get the file path
     * @return Path to the source file
     */
    const std::string& filepath() const {
        return filepath_;
    }

  private:
    std::string filepath_;
    FILE* file_{nullptr};
    uint64_t size_{0};
};

/**
 * @brief Data source for Moonraker HTTP API
 *
 * Attempts to use HTTP Range requests for efficient streaming.
 * If Range requests aren't supported by the server, falls back to
 * downloading the entire file to a temporary location.
 *
 * The fallback behavior is transparent - callers don't need to
 * handle it differently.
 */
class MoonrakerDataSource : public GCodeDataSource {
  public:
    /**
     * @brief Create data source from Moonraker file path
     *
     * @param moonraker_url Base Moonraker URL (e.g., "http://192.168.1.100:7125")
     * @param gcode_path G-code file path on the printer (e.g., "model.gcode")
     */
    MoonrakerDataSource(const std::string& moonraker_url, const std::string& gcode_path);

    ~MoonrakerDataSource() override;

    // Non-copyable
    MoonrakerDataSource(const MoonrakerDataSource&) = delete;
    MoonrakerDataSource& operator=(const MoonrakerDataSource&) = delete;

    std::vector<char> read_range(uint64_t offset, uint32_t length) override;
    uint64_t file_size() const override;
    bool supports_range_requests() const override;
    std::string source_name() const override;
    bool is_valid() const override;

    /**
     * @brief Force download of entire file to temp storage
     *
     * After this, read_range() uses local temp file.
     * Useful if you know you'll need the whole file.
     *
     * @return true if download succeeded
     */
    bool download_to_temp();

    /**
     * @brief Check if we've fallen back to temp file
     * @return true if using local temp file
     */
    bool is_using_temp_file() const {
        return fallback_source_ != nullptr;
    }

    /**
     * @brief Get the download URL
     * @return Full URL for the G-code file
     */
    std::string get_download_url() const;

  private:
    /**
     * @brief Test if server supports Range requests
     * @return true if Range requests work
     */
    bool probe_range_support();

    /**
     * @brief Fetch file metadata (size) from Moonraker
     * @return true if successful
     */
    bool fetch_metadata();

    /**
     * @brief Perform HTTP range request
     * @param offset Start position
     * @param length Bytes to read
     * @return Data or empty vector on failure
     */
    std::vector<char> http_range_request(uint64_t offset, uint32_t length);

    std::string moonraker_url_;
    std::string gcode_path_;
    uint64_t size_{0};
    bool range_support_probed_{false};
    bool range_support_{false};
    bool metadata_fetched_{false};
    bool valid_{false};

    // Fallback to local temp file if range requests don't work
    std::unique_ptr<FileDataSource> fallback_source_;
    std::string temp_file_path_;
};

/**
 * @brief In-memory data source (for testing)
 *
 * Useful for unit tests without needing actual files.
 */
class MemoryDataSource : public GCodeDataSource {
  public:
    /**
     * @brief Create from string content
     * @param content G-code content
     * @param name Optional source name for debugging
     */
    explicit MemoryDataSource(std::string content, std::string name = "memory");

    /**
     * @brief Create from vector of bytes
     * @param data G-code bytes
     * @param name Optional source name for debugging
     */
    explicit MemoryDataSource(std::vector<char> data, std::string name = "memory");

    std::vector<char> read_range(uint64_t offset, uint32_t length) override;
    uint64_t file_size() const override;
    bool supports_range_requests() const override;
    std::string source_name() const override;
    bool is_valid() const override;

  private:
    std::vector<char> data_;
    std::string name_;
};

} // namespace gcode
} // namespace helix
