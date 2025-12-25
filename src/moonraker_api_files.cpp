// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"

#include "hv/hurl.h"
#include "hv/requests.h"
#include "memory_monitor.h"
#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "spdlog/spdlog.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

using namespace moonraker_internal;

// ============================================================================
// File Management Operations
// ============================================================================

void MoonrakerAPI::list_files(const std::string& root, const std::string& path, bool recursive,
                              FileListCallback on_success, ErrorCallback on_error) {
    // Validate root parameter
    if (!is_safe_identifier(root)) {
        NOTIFY_ERROR("File path error: '{}' is not a valid location. Please check the root name.",
                     root);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid root name contains illegal characters";
            err.method = "list_files";
            on_error(err);
        }
        return;
    }

    // Validate path if provided
    if (!path.empty() && !is_safe_path(path)) {
        NOTIFY_ERROR("Invalid file path '{}'. Path contains unsafe characters or references.",
                     path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid path contains directory traversal or illegal characters";
            err.method = "list_files";
            on_error(err);
        }
        return;
    }

    json params = {{"root", root}};

    if (!path.empty()) {
        params["path"] = path;
    }

    if (recursive) {
        params["extended"] = true;
    }

    spdlog::debug("[Moonraker API] Listing files in {}/{}", root, path);

    client_.send_jsonrpc(
        "server.files.list", params,
        [this, on_success](json response) {
            try {
                std::vector<FileInfo> files = parse_file_list(response);
                spdlog::debug("[Moonraker API] Found {} files", files.size());
                on_success(files);
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("Failed to parse file list: {}", e.what());
                on_success(std::vector<FileInfo>{}); // Return empty list on parse error
            }
        },
        on_error);
}

void MoonrakerAPI::get_file_metadata(const std::string& filename, FileMetadataCallback on_success,
                                     ErrorCallback on_error, bool silent) {
    // Validate filename path
    if (!is_safe_path(filename)) {
        if (!silent) {
            NOTIFY_ERROR("Invalid filename '{}'. Check the file path format.", filename);
        }
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid filename contains directory traversal or illegal characters";
            err.method = "get_file_metadata";
            on_error(err);
        }
        return;
    }

    json params = {{"filename", filename}};

    spdlog::trace("[Moonraker API] Getting metadata for file: {}", filename);

    client_.send_jsonrpc(
        "server.files.metadata", params,
        [this, on_success](json response) {
            try {
                FileMetadata metadata = parse_file_metadata(response);
                on_success(metadata);
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("Failed to parse file metadata: {}", e.what());
                FileMetadata empty;
                on_success(empty);
            }
        },
        on_error,
        0,     // timeout_ms: use default
        silent // silent: suppress RPC_ERROR events
    );
}

void MoonrakerAPI::metascan_file(const std::string& filename, FileMetadataCallback on_success,
                                 ErrorCallback on_error, bool silent) {
    // Validate filename path
    if (!is_safe_path(filename)) {
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid filename contains directory traversal or illegal characters";
            err.method = "metascan_file";
            on_error(err);
        }
        return;
    }

    json params = {{"filename", filename}};

    spdlog::debug("[Moonraker API] Triggering metascan for file: {}", filename);

    client_.send_jsonrpc(
        "server.files.metascan", params,
        [this, on_success, filename](json response) {
            try {
                FileMetadata metadata = parse_file_metadata(response);
                spdlog::debug("[Moonraker API] Metascan successful for: {}", filename);
                on_success(metadata);
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("Failed to parse metascan response: {}", e.what());
                FileMetadata empty;
                on_success(empty);
            }
        },
        on_error,
        0,     // timeout_ms: use default
        silent // silent: suppress RPC_ERROR events (default true)
    );
}

void MoonrakerAPI::delete_file(const std::string& filename, SuccessCallback on_success,
                               ErrorCallback on_error) {
    // Validate filename path
    if (!is_safe_path(filename)) {
        NOTIFY_ERROR("Cannot delete '{}'. Invalid file path.", filename);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid filename contains directory traversal or illegal characters";
            err.method = "delete_file";
            on_error(err);
        }
        return;
    }

    json params = {{"path", filename}};

    spdlog::info("[Moonraker API] Deleting file: {}", filename);

    client_.send_jsonrpc(
        "server.files.delete_file", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] File deleted successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::move_file(const std::string& source, const std::string& dest,
                             SuccessCallback on_success, ErrorCallback on_error) {
    // Validate source path
    if (!is_safe_path(source)) {
        NOTIFY_ERROR("Cannot move file. Source path '{}' is invalid.", source);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid source path contains directory traversal or illegal characters";
            err.method = "move_file";
            on_error(err);
        }
        return;
    }

    // Validate destination path
    if (!is_safe_path(dest)) {
        NOTIFY_ERROR("Cannot move file. Destination path '{}' is invalid.", dest);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Invalid destination path contains directory traversal or illegal characters";
            err.method = "move_file";
            on_error(err);
        }
        return;
    }

    spdlog::info("[Moonraker API] Moving file from {} to {}", source, dest);

    json params = {{"source", source}, {"dest", dest}};

    client_.send_jsonrpc(
        "server.files.move", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] File moved successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::copy_file(const std::string& source, const std::string& dest,
                             SuccessCallback on_success, ErrorCallback on_error) {
    // Validate source path
    if (!is_safe_path(source)) {
        NOTIFY_ERROR("Cannot copy file. Source path '{}' is invalid.", source);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid source path contains directory traversal or illegal characters";
            err.method = "copy_file";
            on_error(err);
        }
        return;
    }

    // Validate destination path
    if (!is_safe_path(dest)) {
        NOTIFY_ERROR("Cannot copy file. Destination path '{}' is invalid.", dest);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Invalid destination path contains directory traversal or illegal characters";
            err.method = "copy_file";
            on_error(err);
        }
        return;
    }

    spdlog::info("[Moonraker API] Copying file from {} to {}", source, dest);

    json params = {{"source", source}, {"dest", dest}};

    client_.send_jsonrpc(
        "server.files.copy", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] File copied successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::create_directory(const std::string& path, SuccessCallback on_success,
                                    ErrorCallback on_error) {
    // Validate path
    if (!is_safe_path(path)) {
        NOTIFY_ERROR("Cannot create directory '{}'. Invalid path.", path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Invalid directory path contains directory traversal or illegal characters";
            err.method = "create_directory";
            on_error(err);
        }
        return;
    }

    spdlog::info("[Moonraker API] Creating directory: {}", path);

    json params = {{"path", path}};

    client_.send_jsonrpc(
        "server.files.post_directory", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] Directory created successfully");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::delete_directory(const std::string& path, bool force, SuccessCallback on_success,
                                    ErrorCallback on_error) {
    // Validate path
    if (!is_safe_path(path)) {
        NOTIFY_ERROR("Cannot delete directory '{}'. Invalid path.", path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message =
                "Invalid directory path contains directory traversal or illegal characters";
            err.method = "delete_directory";
            on_error(err);
        }
        return;
    }

    spdlog::info("[Moonraker API] Deleting directory: {} (force: {})", path, force);

    json params = {{"path", path}, {"force", force}};

    client_.send_jsonrpc(
        "server.files.delete_directory", params,
        [on_success](json) {
            spdlog::info("[Moonraker API] Directory deleted successfully");
            on_success();
        },
        on_error);
}

// ============================================================================
// Job Control Operations
// ============================================================================

// ============================================================================
// HTTP File Transfer Operations
// ============================================================================

void MoonrakerAPI::download_file(const std::string& root, const std::string& path,
                                 StringCallback on_success, ErrorCallback on_error) {
    // Validate inputs
    if (!is_safe_path(path)) {
        spdlog::error("[Moonraker API] Invalid file path for download: {}", path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid file path contains unsafe characters";
            err.method = "download_file";
            on_error(err);
        }
        return;
    }

    if (http_base_url_.empty()) {
        spdlog::error(
            "[Moonraker API] HTTP base URL not configured - call set_http_base_url first");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "HTTP base URL not configured";
            err.method = "download_file";
            on_error(err);
        }
        return;
    }

    // Build URL: http://host:port/server/files/{root}/{path}
    // URL-encode the path to handle spaces and special characters
    std::string encoded_path = HUrl::escape(path, "/.-_");
    std::string url = http_base_url_ + "/server/files/" + root + "/" + encoded_path;

    spdlog::debug("[Moonraker API] Downloading file: {}", url);

    // Run HTTP request in a tracked thread to ensure clean shutdown
    launch_http_thread([url, path, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for: {}", url);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "download_file";
                on_error(err);
            }
            return;
        }

        if (resp->status_code == 404) {
            spdlog::error("[Moonraker API] File not found: {}", path);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::FILE_NOT_FOUND;
                err.code = resp->status_code;
                err.message = "File not found: " + path;
                err.method = "download_file";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] HTTP {} downloading {}: {}",
                          static_cast<int>(resp->status_code), path, resp->status_message());
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code)) + ": " +
                              resp->status_message();
                err.method = "download_file";
                on_error(err);
            }
            return;
        }

        spdlog::debug("[Moonraker API] Downloaded {} bytes from {}", resp->body.size(), path);
        helix::MemoryMonitor::log_now("moonraker_download_done");

        if (on_success) {
            on_success(resp->body);
        }
    });
}

void MoonrakerAPI::download_file_to_path(const std::string& root, const std::string& path,
                                         const std::string& dest_path, StringCallback on_success,
                                         ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not set - cannot download file");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "HTTP base URL not configured";
            err.method = "download_file_to_path";
            on_error(err);
        }
        return;
    }

    // Build URL: http://host:port/server/files/{root}/{path}
    // URL-encode the path to handle spaces and special characters
    std::string encoded_path = HUrl::escape(path, "/.-_");
    std::string url = http_base_url_ + "/server/files/" + root + "/" + encoded_path;

    spdlog::debug("[Moonraker API] Streaming download: {} -> {}", url, dest_path);

    // Run HTTP request in a tracked thread to ensure clean shutdown
    // Use requests::downloadFile which streams directly to disk
    launch_http_thread([url, path, dest_path, on_success, on_error]() {
        size_t bytes_written = requests::downloadFile(url.c_str(), dest_path.c_str());

        if (bytes_written == 0) {
            spdlog::error("[Moonraker API] Streaming download failed: {} -> {}", url, dest_path);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "Streaming download failed: " + path;
                err.method = "download_file_to_path";
                on_error(err);
            }
            return;
        }

        spdlog::info("[Moonraker API] Streamed {} bytes to {}", bytes_written, dest_path);

        if (on_success) {
            on_success(dest_path);
        }
    });
}

void MoonrakerAPI::download_thumbnail(const std::string& thumbnail_path,
                                      const std::string& cache_path, StringCallback on_success,
                                      ErrorCallback on_error) {
    // Validate inputs
    if (thumbnail_path.empty()) {
        spdlog::warn("[Moonraker API] Empty thumbnail path");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Empty thumbnail path";
            err.method = "download_thumbnail";
            on_error(err);
        }
        return;
    }

    // Ensure HTTP URL is available (auto-derives from WebSocket if needed)
    if (!ensure_http_base_url()) {
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "HTTP base URL not configured";
            err.method = "download_thumbnail";
            on_error(err);
        }
        return;
    }

    // Build URL: http://host:port/server/files/gcodes/{thumbnail_path}
    // Thumbnail paths from metadata are relative to gcodes root
    // URL-encode the path to handle spaces and special characters
    // Leave /.-_ unescaped as they're valid in URL paths
    std::string encoded_path = HUrl::escape(thumbnail_path, "/.-_");
    std::string url = http_base_url_ + "/server/files/gcodes/" + encoded_path;

    spdlog::trace("[Moonraker API] Downloading thumbnail: {} -> {}", url, cache_path);

    // Run HTTP request in a tracked thread to ensure clean shutdown
    launch_http_thread([url, thumbnail_path, cache_path, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for thumbnail: {}", url);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP request failed";
                err.method = "download_thumbnail";
                on_error(err);
            }
            return;
        }

        if (resp->status_code == 404) {
            spdlog::warn("[Moonraker API] Thumbnail not found: {}", thumbnail_path);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::FILE_NOT_FOUND;
                err.code = resp->status_code;
                err.message = "Thumbnail not found: " + thumbnail_path;
                err.method = "download_thumbnail";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] HTTP {} downloading thumbnail {}: {}",
                          static_cast<int>(resp->status_code), thumbnail_path,
                          resp->status_message());
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code)) + ": " +
                              resp->status_message();
                err.method = "download_thumbnail";
                on_error(err);
            }
            return;
        }

        // Write to cache file
        std::ofstream file(cache_path, std::ios::binary);
        if (!file) {
            spdlog::error("[Moonraker API] Failed to create cache file: {}", cache_path);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = "Failed to create cache file: " + cache_path;
                err.method = "download_thumbnail";
                on_error(err);
            }
            return;
        }

        file.write(resp->body.data(), static_cast<std::streamsize>(resp->body.size()));
        file.close();

        spdlog::trace("[Moonraker API] Cached thumbnail {} bytes -> {}", resp->body.size(),
                      cache_path);
        helix::MemoryMonitor::log_now("moonraker_thumb_downloaded");

        if (on_success) {
            on_success(cache_path);
        }
    });
}

void MoonrakerAPI::upload_file(const std::string& root, const std::string& path,
                               const std::string& content, SuccessCallback on_success,
                               ErrorCallback on_error) {
    upload_file_with_name(root, path, path, content, on_success, on_error);
}

void MoonrakerAPI::upload_file_with_name(const std::string& root, const std::string& path,
                                         const std::string& filename, const std::string& content,
                                         SuccessCallback on_success, ErrorCallback on_error) {
    // Validate inputs
    if (!is_safe_path(path)) {
        spdlog::error("[Moonraker API] Invalid file path for upload: {}", path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid file path contains unsafe characters";
            err.method = "upload_file";
            on_error(err);
        }
        return;
    }

    if (http_base_url_.empty()) {
        spdlog::error(
            "[Moonraker API] HTTP base URL not configured - call set_http_base_url first");
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.message = "HTTP base URL not configured";
            err.method = "upload_file";
            on_error(err);
        }
        return;
    }

    // Build URL: http://host:port/server/files/upload
    std::string url = http_base_url_ + "/server/files/upload";

    spdlog::debug("[Moonraker API] Uploading {} bytes to {}/{}", content.size(), root, path);

    // Run HTTP request in a tracked thread to ensure clean shutdown
    launch_http_thread([url, root, path, filename, content, on_success, on_error]() {
        // Create multipart form request
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_POST;
        req->url = url;
        req->timeout = 120; // 2 minute timeout for uploads
        req->content_type = MULTIPART_FORM_DATA;

        // Add root parameter (e.g., "gcodes" or "config")
        req->SetFormData("root", root);

        // Add path parameter if uploading to subdirectory
        if (path.find('/') != std::string::npos) {
            // Extract directory from path
            size_t last_slash = path.rfind('/');
            if (last_slash != std::string::npos) {
                std::string directory = path.substr(0, last_slash);
                req->SetFormData("path", directory);
            }
        }

        // Add file content with filename
        // Use hv::FormData for multipart file upload
        hv::FormData file_data;
        file_data.content = content;
        file_data.filename = filename;
        req->form["file"] = file_data;
        helix::MemoryMonitor::log_now("moonraker_upload_start");

        // Send request
        auto resp = requests::request(req);

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP upload request failed to: {}", url);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "HTTP upload request failed";
                err.method = "upload_file";
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 201 && resp->status_code != 200) {
            spdlog::error("[Moonraker API] HTTP {} uploading {}: {} - {}",
                          static_cast<int>(resp->status_code), path, resp->status_message(),
                          resp->body);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.code = static_cast<int>(resp->status_code);
                err.message = "HTTP " + std::to_string(static_cast<int>(resp->status_code)) + ": " +
                              resp->status_message();
                err.method = "upload_file";
                on_error(err);
            }
            return;
        }

        spdlog::info("[Moonraker API] Successfully uploaded {} ({} bytes)", path, content.size());

        if (on_success) {
            on_success();
        }
    });
}

void MoonrakerAPI::upload_file_from_path(const std::string& root, const std::string& dest_path,
                                         const std::string& local_path, SuccessCallback on_success,
                                         ErrorCallback on_error) {
    // Validate inputs
    if (!is_safe_path(dest_path)) {
        spdlog::error("[Moonraker API] Invalid destination path for upload: {}", dest_path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::VALIDATION_ERROR;
            err.message = "Invalid destination path contains unsafe characters";
            err.method = "upload_file_from_path";
            on_error(err);
        }
        return;
    }

    // Read file content from disk
    std::ifstream file(local_path, std::ios::binary);
    if (!file) {
        spdlog::error("[Moonraker API] Failed to open local file for upload: {}", local_path);
        if (on_error) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::FILE_NOT_FOUND;
            err.message = "Failed to open local file: " + local_path;
            err.method = "upload_file_from_path";
            on_error(err);
        }
        return;
    }

    // Read entire file into string
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    helix::MemoryMonitor::log_now("moonraker_upload_buffered");

    spdlog::debug("[Moonraker API] Read {} bytes from local file {}", content.size(), local_path);

    // Delegate to existing upload method
    upload_file_with_name(root, dest_path, dest_path, content, on_success, on_error);
}

// ============================================================================
// Private Helper Methods
// ============================================================================

// ============================================================================
// File List/Metadata Parsing
// ============================================================================

std::vector<FileInfo> MoonrakerAPI::parse_file_list(const json& response) {
    std::vector<FileInfo> files;

    if (!response.contains("result")) {
        return files;
    }

    const json& result = response["result"];

    // Moonraker returns a flat array of file/directory objects in "result"
    // Each object has: path, modified, size, permissions
    // Directories are NOT returned by server.files.list - only by server.files.get_directory
    if (result.is_array()) {
        for (const auto& item : result) {
            FileInfo info;
            if (item.contains("path")) {
                info.path = item["path"].get<std::string>();
                // filename is the last component of the path
                size_t last_slash = info.path.rfind('/');
                info.filename = (last_slash != std::string::npos) ? info.path.substr(last_slash + 1)
                                                                  : info.path;
            } else if (item.contains("filename")) {
                info.filename = item["filename"].get<std::string>();
            }
            if (item.contains("size")) {
                info.size = item["size"].get<uint64_t>();
            }
            if (item.contains("modified")) {
                info.modified = item["modified"].get<double>();
            }
            if (item.contains("permissions")) {
                info.permissions = item["permissions"].get<std::string>();
            }
            info.is_dir = false; // server.files.list only returns files
            files.push_back(info);
        }
        return files;
    }

    // Legacy format: result is an object with "dirs" and "files" arrays
    // (may be used by server.files.get_directory or older Moonraker versions)
    if (result.contains("dirs")) {
        for (const auto& dir : result["dirs"]) {
            FileInfo info;
            if (dir.contains("dirname")) {
                info.filename = dir["dirname"].get<std::string>();
                info.is_dir = true;
            }
            if (dir.contains("modified")) {
                info.modified = dir["modified"].get<double>();
            }
            if (dir.contains("permissions")) {
                info.permissions = dir["permissions"].get<std::string>();
            }
            files.push_back(info);
        }
    }

    if (result.contains("files")) {
        for (const auto& file : result["files"]) {
            FileInfo info;
            if (file.contains("filename")) {
                info.filename = file["filename"].get<std::string>();
            }
            if (file.contains("path")) {
                info.path = file["path"].get<std::string>();
            }
            if (file.contains("size")) {
                info.size = file["size"].get<uint64_t>();
            }
            if (file.contains("modified")) {
                info.modified = file["modified"].get<double>();
            }
            if (file.contains("permissions")) {
                info.permissions = file["permissions"].get<std::string>();
            }
            info.is_dir = false;
            files.push_back(info);
        }
    }

    return files;
}

FileMetadata MoonrakerAPI::parse_file_metadata(const json& response) {
    FileMetadata metadata;

    if (!response.contains("result")) {
        return metadata;
    }

    const json& result = response["result"];

    // Helper lambdas to safely extract values (Moonraker returns null for missing metadata)
    auto get_string = [&result](const char* key) -> std::string {
        if (result.contains(key) && result[key].is_string()) {
            return result[key].get<std::string>();
        }
        return {};
    };

    auto get_double = [&result](const char* key) -> double {
        if (result.contains(key) && result[key].is_number()) {
            return result[key].get<double>();
        }
        return 0.0;
    };

    auto get_uint64 = [&result](const char* key) -> uint64_t {
        if (result.contains(key) && result[key].is_number()) {
            return result[key].get<uint64_t>();
        }
        return 0;
    };

    auto get_uint32 = [&result](const char* key) -> uint32_t {
        if (result.contains(key) && result[key].is_number()) {
            return result[key].get<uint32_t>();
        }
        return 0;
    };

    // Basic file info
    metadata.filename = get_string("filename");
    metadata.size = get_uint64("size");
    metadata.modified = get_double("modified");

    // Slicer info
    metadata.slicer = get_string("slicer");
    metadata.slicer_version = get_string("slicer_version");

    // Print info
    metadata.print_start_time = get_double("print_start_time");
    metadata.job_id = get_string("job_id");
    metadata.layer_count = get_uint32("layer_count");
    metadata.object_height = get_double("object_height");
    metadata.estimated_time = get_double("estimated_time");

    // Filament info
    metadata.filament_total = get_double("filament_total");
    metadata.filament_weight_total = get_double("filament_weight_total");
    // Moonraker returns "PLA;PLA;PLA;PLA" for multi-extruder - take first value
    std::string raw_type = get_string("filament_type");
    if (!raw_type.empty()) {
        size_t semicolon = raw_type.find(';');
        metadata.filament_type =
            (semicolon != std::string::npos) ? raw_type.substr(0, semicolon) : raw_type;
    }

    // Filament colors (array of hex strings from slicer metadata)
    if (result.contains("filament_colors") && result["filament_colors"].is_array()) {
        for (const auto& color : result["filament_colors"]) {
            if (color.is_string()) {
                metadata.filament_colors.push_back(color.get<std::string>());
            }
        }
        if (!metadata.filament_colors.empty()) {
            spdlog::debug("[Moonraker API] Found {} filament colors",
                          metadata.filament_colors.size());
        }
    }

    // Temperature info
    metadata.first_layer_bed_temp = get_double("first_layer_bed_temp");
    metadata.first_layer_extr_temp = get_double("first_layer_extr_temp");

    // G-code info
    metadata.gcode_start_byte = get_uint64("gcode_start_byte");
    metadata.gcode_end_byte = get_uint64("gcode_end_byte");

    // Thumbnails - parse with dimensions for selecting largest
    if (result.contains("thumbnails") && result["thumbnails"].is_array()) {
        for (const auto& thumb : result["thumbnails"]) {
            if (thumb.contains("relative_path") && thumb["relative_path"].is_string()) {
                ThumbnailInfo info;
                info.relative_path = thumb["relative_path"].get<std::string>();
                if (thumb.contains("width") && thumb["width"].is_number()) {
                    info.width = thumb["width"].get<int>();
                }
                if (thumb.contains("height") && thumb["height"].is_number()) {
                    info.height = thumb["height"].get<int>();
                }
                metadata.thumbnails.push_back(info);
                spdlog::trace("[Moonraker API] Found thumbnail {}x{}: {}", info.width, info.height,
                              info.relative_path);
            }
        }
    }

    return metadata;
}
