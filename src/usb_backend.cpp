// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usb_backend.h"

#include "usb_backend_mock.h"

#ifdef __linux__
#include "usb_backend_linux.h"
#endif

#include <spdlog/spdlog.h>

std::unique_ptr<UsbBackend> UsbBackend::create(bool force_mock) {
    if (force_mock) {
        spdlog::debug("[UsbBackend] Creating mock backend (force_mock=true)");
        return std::make_unique<UsbBackendMock>();
    }

#ifdef __linux__
    // Linux: Use native backend (inotify preferred, polling fallback)
    spdlog::debug("[UsbBackend] Linux platform detected - using native backend");
    auto backend = std::make_unique<UsbBackendLinux>();
    UsbError result = backend->start();
    if (result.success()) {
        return backend;
    }

    // Fallback to mock if Linux backend fails
    spdlog::warn("[UsbBackend] Linux backend failed: {} - falling back to mock",
                 result.technical_msg);
    return std::make_unique<UsbBackendMock>();
#elif defined(__APPLE__)
    // macOS: Use mock backend for development
    // FSEvents-based backend can be added later for real monitoring
    spdlog::debug("[UsbBackend] macOS platform detected - using mock backend");
    return std::make_unique<UsbBackendMock>();
#else
    // Unsupported platform: return mock
    spdlog::warn("[UsbBackend] Unknown platform - using mock backend");
    return std::make_unique<UsbBackendMock>();
#endif
}
