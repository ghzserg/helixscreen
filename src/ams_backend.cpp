// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend.h"

#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_backend_mock.h"
#include "moonraker_api.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

std::unique_ptr<AmsBackend> AmsBackend::create(AmsType detected_type) {
    // Check if mock mode is requested
    if (get_runtime_config().should_mock_ams()) {
        spdlog::info("[AMS Backend] Creating mock backend (mock mode enabled)");
        return std::make_unique<AmsBackendMock>(4);
    }

    // Without API/client dependencies, we can only return mock backends
    switch (detected_type) {
    case AmsType::HAPPY_HARE:
        spdlog::warn("[AMS Backend] Happy Hare detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(4);

    case AmsType::AFC:
        spdlog::warn("[AMS Backend] AFC detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(4);

    case AmsType::NONE:
    default:
        spdlog::debug("[AMS Backend] No AMS detected");
        return nullptr;
    }
}

std::unique_ptr<AmsBackend> AmsBackend::create(AmsType detected_type, MoonrakerAPI* api,
                                               MoonrakerClient* client) {
    // Check if mock mode is requested
    if (get_runtime_config().should_mock_ams()) {
        spdlog::info("[AMS Backend] Creating mock backend (mock mode enabled)");
        return std::make_unique<AmsBackendMock>(4);
    }

    switch (detected_type) {
    case AmsType::HAPPY_HARE:
        if (!api || !client) {
            spdlog::error("[AMS Backend] Happy Hare requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::info("[AMS Backend] Creating Happy Hare backend");
        return std::make_unique<AmsBackendHappyHare>(api, client);

    case AmsType::AFC:
        if (!api || !client) {
            spdlog::error("[AMS Backend] AFC requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::info("[AMS Backend] Creating AFC backend");
        return std::make_unique<AmsBackendAfc>(api, client);

    case AmsType::NONE:
    default:
        spdlog::debug("[AMS Backend] No AMS detected");
        return nullptr;
    }
}
