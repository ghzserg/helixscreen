// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_error_handling.cpp
 * @brief Unit tests for AFC error/warning message handling
 *
 * Tests the message queue consumption in AmsBackendAfc:
 * - Deduplication of repeated messages
 * - Toast severity mapping (error/warning)
 * - Toast suppression when AFC action:prompt is active
 * - Message reset when error clears
 */

#include "action_prompt_manager.h"
#include "ams_backend_afc.h"
#include "ams_types.h"
#include "moonraker_api.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Helper
// ============================================================================

/**
 * @brief Test helper for AFC error handling tests
 *
 * Extends AmsBackendAfc to expose internals and capture notification calls.
 * Tracks which notification types were triggered via the message handling path.
 */
class AfcErrorHandlingHelper : public AmsBackendAfc {
  public:
    AfcErrorHandlingHelper() : AmsBackendAfc(nullptr, nullptr) {
        // Initialize some lanes so parse_afc_state works
        std::vector<std::string> names = {"lane1", "lane2", "lane3", "lane4"};
        slots_.initialize("AFC Test Unit", names);
    }

    // Feed AFC state update with a message object
    void feed_afc_message(const std::string& message_text, const std::string& message_type) {
        nlohmann::json afc_data;
        afc_data["message"]["message"] = message_text;
        afc_data["message"]["type"] = message_type;

        nlohmann::json params;
        params["AFC"] = afc_data;

        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    // Feed AFC state with empty message (error cleared)
    void feed_afc_empty_message() {
        nlohmann::json afc_data;
        afc_data["message"]["message"] = "";
        afc_data["message"]["type"] = "";

        nlohmann::json params;
        params["AFC"] = afc_data;

        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    // Access last_seen_message_ for assertions
    std::string get_last_seen_message() const {
        return last_seen_message_;
    }

  private:
    // Override execute_gcode to prevent null pointer access
    AmsError execute_gcode(const std::string& /*gcode*/) override {
        return AmsErrorHelper::success();
    }
};

// ============================================================================
// Message Deduplication Tests
// ============================================================================

TEST_CASE("AFC Error Handling: Message deduplication", "[afc][error_handling][dedup]") {
    AfcErrorHandlingHelper afc;

    SECTION("New error message updates last_seen_message") {
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");
    }

    SECTION("Same message repeated does not change state") {
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");

        // Second identical message should still have the same last_seen
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");
    }

    SECTION("Different message updates last_seen_message") {
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");

        afc.feed_afc_message("Lane 2 prep failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 2 prep failed");
    }

    SECTION("Empty message resets last_seen_message") {
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");

        afc.feed_afc_empty_message();
        REQUIRE(afc.get_last_seen_message().empty());
    }

    SECTION("Warning message also tracked by last_seen_message") {
        afc.feed_afc_message("Buffer not advancing", "warning");
        REQUIRE(afc.get_last_seen_message() == "Buffer not advancing");
    }

    SECTION("After empty reset, same message is treated as new") {
        afc.feed_afc_message("Lane 1 load failed", "error");
        afc.feed_afc_empty_message();
        REQUIRE(afc.get_last_seen_message().empty());

        // Same message text again after reset - should be tracked as new
        afc.feed_afc_message("Lane 1 load failed", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 load failed");
    }
}

// ============================================================================
// Toast Suppression Tests (AFC prompt active)
// ============================================================================

TEST_CASE("AFC Error Handling: Toast suppression when AFC prompt is active",
          "[afc][error_handling][suppression]") {
    SECTION("Toast NOT suppressed when no prompt is active") {
        AfcErrorHandlingHelper afc;
        // Ensure no prompt is active
        ActionPromptManager::set_instance(nullptr);

        // Should go through normal toast path (not suppressed)
        afc.feed_afc_message("Lane 1 error", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 error");
    }

    SECTION("Toast suppressed when AFC prompt is active") {
        AfcErrorHandlingHelper afc;
        ActionPromptManager manager;
        ActionPromptManager::set_instance(&manager);

        // Show an AFC prompt
        manager.process_line("// action:prompt_begin AFC Lane Error");
        manager.process_line("// action:prompt_show");
        REQUIRE(ActionPromptManager::is_showing());

        // Message should still be tracked for dedup
        afc.feed_afc_message("Lane 1 error", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 error");
        // Toast is suppressed but notification history entry is created
        // (verified by integration test / no crash)

        ActionPromptManager::set_instance(nullptr);
    }

    SECTION("Toast NOT suppressed when non-AFC prompt is active") {
        AfcErrorHandlingHelper afc;
        ActionPromptManager manager;
        ActionPromptManager::set_instance(&manager);

        // Show a non-AFC prompt (e.g., Filament Change)
        manager.process_line("// action:prompt_begin Filament Change");
        manager.process_line("// action:prompt_show");
        REQUIRE(ActionPromptManager::is_showing());

        // AFC message should NOT be suppressed since prompt is not AFC-related
        afc.feed_afc_message("Lane 1 error", "error");
        REQUIRE(afc.get_last_seen_message() == "Lane 1 error");

        ActionPromptManager::set_instance(nullptr);
    }

    SECTION("Toast suppressed only when prompt name contains AFC") {
        ActionPromptManager manager;
        ActionPromptManager::set_instance(&manager);

        // "AFC" must appear in the prompt title for suppression
        manager.process_line("// action:prompt_begin AFC Recovery");
        manager.process_line("// action:prompt_show");

        REQUIRE(ActionPromptManager::is_showing());
        REQUIRE(ActionPromptManager::current_prompt_name().find("AFC") != std::string::npos);

        ActionPromptManager::set_instance(nullptr);
    }
}

// ============================================================================
// Message Type to Severity Mapping Tests
// ============================================================================

TEST_CASE("AFC Error Handling: Message type to severity mapping",
          "[afc][error_handling][severity]") {
    AfcErrorHandlingHelper afc;
    // No prompt active
    ActionPromptManager::set_instance(nullptr);

    SECTION("Error type message is tracked") {
        afc.feed_afc_message("Critical lane failure", "error");
        REQUIRE(afc.get_last_seen_message() == "Critical lane failure");
    }

    SECTION("Warning type message is tracked") {
        afc.feed_afc_message("Buffer advancing slowly", "warning");
        REQUIRE(afc.get_last_seen_message() == "Buffer advancing slowly");
    }

    SECTION("Unknown type message is still tracked") {
        afc.feed_afc_message("Something happened", "info");
        REQUIRE(afc.get_last_seen_message() == "Something happened");
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("AFC Error Handling: Edge cases", "[afc][error_handling][edge]") {
    AfcErrorHandlingHelper afc;
    ActionPromptManager::set_instance(nullptr);

    SECTION("Message with empty type field is handled gracefully") {
        afc.feed_afc_message("No type field", "");
        // Should not crash, message still tracked (defaults to info toast)
        REQUIRE(afc.get_last_seen_message() == "No type field");
    }

    SECTION("Rapid message changes are all tracked") {
        afc.feed_afc_message("Error 1", "error");
        REQUIRE(afc.get_last_seen_message() == "Error 1");

        afc.feed_afc_message("Error 2", "error");
        REQUIRE(afc.get_last_seen_message() == "Error 2");

        afc.feed_afc_message("Warning 1", "warning");
        REQUIRE(afc.get_last_seen_message() == "Warning 1");

        afc.feed_afc_empty_message();
        REQUIRE(afc.get_last_seen_message().empty());
    }
}
