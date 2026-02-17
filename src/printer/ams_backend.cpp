// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend.h"

#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_backend_mock.h"
#include "ams_backend_toolchanger.h"
#include "ams_backend_valgace.h"
#include "moonraker_api.h"
#include "runtime_config.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

using namespace helix;

// Helper to create mock backend with optional features
static std::unique_ptr<AmsBackendMock> create_mock_with_features(int gate_count) {
    auto mock = std::make_unique<AmsBackendMock>(gate_count);

    // Check for tool changer simulation mode
    const char* ams_type_env = std::getenv("HELIX_MOCK_AMS_TYPE");
    if (ams_type_env) {
        std::string type_str(ams_type_env);
        // Convert to lowercase for comparison
        std::transform(type_str.begin(), type_str.end(), type_str.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (type_str == "toolchanger" || type_str == "tool_changer" || type_str == "tc") {
            mock->set_tool_changer_mode(true);
            spdlog::info("[AMS Backend] Mock tool changer mode enabled via HELIX_MOCK_AMS_TYPE");
        } else if (type_str == "mixed") {
            mock->set_mixed_topology_mode(true);
            spdlog::info("[AMS Backend] Mock mixed topology mode enabled via HELIX_MOCK_AMS_TYPE");
        } else if (type_str == "afc" || type_str == "box_turtle" || type_str == "boxturtle") {
            mock->set_afc_mode(true);
            spdlog::info("[AMS Backend] Mock AFC mode enabled via HELIX_MOCK_AMS_TYPE");
        }
    }

    // Check for multi-unit mode (overrides AFC mode if both set)
    const char* multi_unit_env = std::getenv("HELIX_MOCK_MULTI_UNIT");
    if (multi_unit_env && std::string(multi_unit_env) == "1") {
        mock->set_multi_unit_mode(true);
        spdlog::info("[AMS Backend] Mock multi-unit mode enabled via HELIX_MOCK_MULTI_UNIT");
    }

    // Inject mock error states for visual testing (slot errors + buffer fault)
    const char* errors_env = std::getenv("HELIX_MOCK_AMS_ERRORS");
    if (errors_env && std::string(errors_env) == "1") {
        mock->inject_mock_errors();
        spdlog::info("[AMS Backend] Mock error states injected via HELIX_MOCK_AMS_ERRORS");
    }

    // Enable mock dryer if requested via environment variable
    // Note: Dryer is typically not applicable for tool changers, but allow override
    const char* dryer_env = std::getenv("HELIX_MOCK_DRYER");
    if (dryer_env && (std::string(dryer_env) == "1" || std::string(dryer_env) == "true")) {
        mock->set_dryer_enabled(true);
        spdlog::info("[AMS Backend] Mock dryer enabled via HELIX_MOCK_DRYER");
    }

    // Enable realistic multi-phase operations if requested
    const char* realistic_env = std::getenv("HELIX_MOCK_AMS_REALISTIC");
    if (realistic_env &&
        (std::string(realistic_env) == "1" || std::string(realistic_env) == "true")) {
        mock->set_realistic_mode(true);
        spdlog::info("[AMS Backend] Mock realistic mode enabled via HELIX_MOCK_AMS_REALISTIC");
    }

    return mock;
}

std::unique_ptr<AmsBackend> AmsBackend::create(AmsType detected_type) {
    const auto* config = get_runtime_config();

    // Check if mock mode is requested
    if (config->should_mock_ams()) {
        spdlog::debug("[AMS Backend] Creating mock backend with {} gates (mock mode enabled)",
                      config->mock_ams_gate_count);
        return create_mock_with_features(config->mock_ams_gate_count);
    }

    // Without API/client dependencies, we can only return mock backends
    switch (detected_type) {
    case AmsType::HAPPY_HARE:
        spdlog::warn("[AMS Backend] Happy Hare detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(config->mock_ams_gate_count);

    case AmsType::AFC:
        spdlog::warn("[AMS Backend] AFC detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(config->mock_ams_gate_count);

    case AmsType::VALGACE:
        spdlog::warn("[AMS Backend] ValgACE detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(config->mock_ams_gate_count);

    case AmsType::TOOL_CHANGER:
        spdlog::warn("[AMS Backend] Tool changer detected but no API/client provided - using mock");
        return std::make_unique<AmsBackendMock>(config->mock_ams_gate_count);

    case AmsType::NONE:
    default:
        spdlog::debug("[AMS Backend] No AMS detected");
        return nullptr;
    }
}

std::unique_ptr<AmsBackend> AmsBackend::create(AmsType detected_type, MoonrakerAPI* api,
                                               MoonrakerClient* client) {
    const auto* config = get_runtime_config();

    // Check if mock mode is requested
    if (config->should_mock_ams()) {
        spdlog::debug("[AMS Backend] Creating mock backend with {} gates (mock mode enabled)",
                      config->mock_ams_gate_count);
        return create_mock_with_features(config->mock_ams_gate_count);
    }

    switch (detected_type) {
    case AmsType::HAPPY_HARE:
        if (!api || !client) {
            spdlog::error("[AMS Backend] Happy Hare requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::debug("[AMS Backend] Creating Happy Hare backend");
        return std::make_unique<AmsBackendHappyHare>(api, client);

    case AmsType::AFC:
        if (!api || !client) {
            spdlog::error("[AMS Backend] AFC requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::debug("[AMS Backend] Creating AFC backend");
        return std::make_unique<AmsBackendAfc>(api, client);

    case AmsType::VALGACE:
        if (!api || !client) {
            spdlog::error("[AMS Backend] ValgACE requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::debug("[AMS Backend] Creating ValgACE backend");
        return std::make_unique<AmsBackendValgACE>(api, client);

    case AmsType::TOOL_CHANGER:
        if (!api || !client) {
            spdlog::error("[AMS Backend] Tool changer requires MoonrakerAPI and MoonrakerClient");
            return nullptr;
        }
        spdlog::debug("[AMS Backend] Creating Tool Changer backend");
        // Note: Caller must use set_discovered_tools() after creation to set tool names
        return std::make_unique<AmsBackendToolChanger>(api, client);

    case AmsType::NONE:
    default:
        spdlog::debug("[AMS Backend] No AMS detected");
        return nullptr;
    }
}
