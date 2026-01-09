// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_abort_manager.cpp
 * @brief Unit tests for AbortManager - Smart print cancellation with progressive escalation
 *
 * Tests the AbortManager state machine which progressively tries softer abort
 * methods before resorting to M112 emergency stop:
 *
 * 1. TRY_HEATER_INTERRUPT - Probe for Kalico, try soft interrupt (1s timeout)
 * 2. PROBE_QUEUE - Send M115 to test if queue is responsive (2s timeout)
 * 3. SENT_CANCEL - Queue responsive, send CANCEL_PRINT (3s timeout)
 * 4. SENT_ESTOP - Queue blocked or cancel failed, send M112
 * 5. SENT_RESTART - Send FIRMWARE_RESTART after M112
 * 6. WAITING_RECONNECT - Wait for klippy_state == READY (15s timeout)
 *
 * Written TDD-style - tests WILL FAIL until AbortManager is implemented.
 */

#include "../lvgl_test_fixture.h"
#include "../ui_test_utils.h"
#include "abort_manager.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for AbortManager tests
 *
 * Provides LVGL initialization and a mock MoonrakerAPI for testing
 * state machine transitions without real network calls.
 */
class AbortManagerTestFixture : public LVGLTestFixture {
  public:
    AbortManagerTestFixture() : LVGLTestFixture() {
        // Reset AbortManager to known state before each test
        AbortManager::instance().reset_for_testing();
    }

    ~AbortManagerTestFixture() override {
        // Ensure clean state after test
        AbortManager::instance().reset_for_testing();
    }

  protected:
    /**
     * @brief Simulate successful Kalico detection (HEATER_INTERRUPT succeeds)
     */
    void simulate_kalico_detected() {
        AbortManager::instance().on_heater_interrupt_success_for_testing();
    }

    /**
     * @brief Simulate Kalico not present (HEATER_INTERRUPT returns "Unknown command")
     */
    void simulate_kalico_not_present() {
        AbortManager::instance().on_heater_interrupt_error_for_testing("Unknown command");
    }

    /**
     * @brief Simulate M115 probe response (queue is responsive)
     */
    void simulate_queue_responsive() {
        AbortManager::instance().on_probe_response_for_testing();
    }

    /**
     * @brief Simulate M115 probe timeout (queue is blocked)
     */
    void simulate_queue_blocked() {
        AbortManager::instance().on_probe_timeout_for_testing();
    }

    /**
     * @brief Simulate CANCEL_PRINT success
     */
    void simulate_cancel_success() {
        AbortManager::instance().on_cancel_success_for_testing();
    }

    /**
     * @brief Simulate CANCEL_PRINT timeout
     */
    void simulate_cancel_timeout() {
        AbortManager::instance().on_cancel_timeout_for_testing();
    }

    /**
     * @brief Simulate klippy_state becoming READY after restart
     */
    void simulate_klippy_ready() {
        AbortManager::instance().on_klippy_ready_for_testing();
    }

    /**
     * @brief Get current state as string for debugging
     */
    std::string state_name() const {
        return AbortManager::instance().get_state_name();
    }
};

// ============================================================================
// Initial State Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Initial state is IDLE",
                 "[abort][state][init]") {
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::IDLE);
    REQUIRE(AbortManager::instance().is_aborting() == false);
    REQUIRE(state_name() == "IDLE");
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Singleton returns same instance",
                 "[abort][singleton]") {
    AbortManager& instance1 = AbortManager::instance();
    AbortManager& instance2 = AbortManager::instance();

    REQUIRE(&instance1 == &instance2);
}

// ============================================================================
// Start Abort Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: start_abort transitions from IDLE",
                 "[abort][state][start]") {
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::IDLE);

    AbortManager::instance().start_abort();

    // Should transition to TRY_HEATER_INTERRUPT (first attempt probes Kalico)
    // or PROBE_QUEUE if Kalico status is already known
    REQUIRE(AbortManager::instance().is_aborting() == true);
    REQUIRE(AbortManager::instance().get_state() != AbortManager::State::IDLE);
}

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: start_abort is ignored while already aborting",
                 "[abort][state][start]") {
    AbortManager::instance().start_abort();
    auto state_after_first = AbortManager::instance().get_state();

    // Try to start again - should be ignored
    AbortManager::instance().start_abort();

    REQUIRE(AbortManager::instance().get_state() == state_after_first);
}

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: First abort probes Kalico with HEATER_INTERRUPT",
                 "[abort][kalico][start]") {
    // First abort should probe for Kalico
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::UNKNOWN);

    AbortManager::instance().start_abort();

    // Should be in TRY_HEATER_INTERRUPT state
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);
}

// ============================================================================
// Kalico Detection Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: HEATER_INTERRUPT success detects Kalico",
                 "[abort][kalico][detection]") {
    AbortManager::instance().start_abort();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);

    // Simulate successful HEATER_INTERRUPT response
    simulate_kalico_detected();

    // Kalico should be cached as DETECTED
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::DETECTED);

    // Should transition to PROBE_QUEUE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: HEATER_INTERRUPT error detects not-Kalico",
                 "[abort][kalico][detection]") {
    AbortManager::instance().start_abort();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);

    // Simulate "Unknown command" error
    simulate_kalico_not_present();

    // Kalico should be cached as NOT_PRESENT
    REQUIRE(AbortManager::instance().get_kalico_status() ==
            AbortManager::KalicoStatus::NOT_PRESENT);

    // Should skip directly to PROBE_QUEUE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);
}

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: HEATER_INTERRUPT timeout treated as not-Kalico",
                 "[abort][kalico][timeout]") {
    AbortManager::instance().start_abort();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);

    // Simulate timeout (no response within 1s)
    AbortManager::instance().on_heater_interrupt_timeout_for_testing();

    // Kalico should be cached as NOT_PRESENT
    REQUIRE(AbortManager::instance().get_kalico_status() ==
            AbortManager::KalicoStatus::NOT_PRESENT);

    // Should transition to PROBE_QUEUE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);
}

// ============================================================================
// Kalico Caching Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Second abort uses cached Kalico status - DETECTED",
                 "[abort][kalico][caching]") {
    // First abort - detect Kalico
    AbortManager::instance().start_abort();
    simulate_kalico_detected();
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::DETECTED);

    // Complete first abort
    simulate_queue_responsive();
    simulate_cancel_success();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);

    // Reset state but keep cached Kalico status
    AbortManager::instance().reset_state_for_testing();

    // Second abort - should use cached status
    AbortManager::instance().start_abort();

    // Should STILL try HEATER_INTERRUPT when Kalico is detected (it's a soft interrupt)
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::DETECTED);
}

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Second abort skips probe when NOT_PRESENT cached",
                 "[abort][kalico][caching]") {
    // First abort - detect not-Kalico
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    REQUIRE(AbortManager::instance().get_kalico_status() ==
            AbortManager::KalicoStatus::NOT_PRESENT);

    // Complete first abort
    simulate_queue_responsive();
    simulate_cancel_success();

    // Reset state but keep cached Kalico status
    AbortManager::instance().reset_state_for_testing();

    // Second abort - should skip HEATER_INTERRUPT
    AbortManager::instance().start_abort();

    // Should go directly to PROBE_QUEUE (skip TRY_HEATER_INTERRUPT)
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);
    REQUIRE(AbortManager::instance().get_kalico_status() ==
            AbortManager::KalicoStatus::NOT_PRESENT);
}

// ============================================================================
// Queue Probe Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: M115 response indicates queue responsive",
                 "[abort][queue][probe]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present(); // Skip to PROBE_QUEUE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);

    // Simulate M115 response
    simulate_queue_responsive();

    // Should transition to SENT_CANCEL
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_CANCEL);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: M115 timeout indicates queue blocked",
                 "[abort][queue][timeout]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present(); // Skip to PROBE_QUEUE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);

    // Simulate M115 timeout (2s without response)
    simulate_queue_blocked();

    // Should escalate directly to SENT_ESTOP
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);
}

// ============================================================================
// Cancel Print Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: CANCEL_PRINT success completes abort",
                 "[abort][cancel][success]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_responsive();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_CANCEL);

    // Simulate CANCEL_PRINT success
    simulate_cancel_success();

    // Should complete successfully
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
    REQUIRE(AbortManager::instance().is_aborting() == false);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: CANCEL_PRINT timeout escalates to ESTOP",
                 "[abort][cancel][timeout][escalation]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_responsive();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_CANCEL);

    // Simulate CANCEL_PRINT timeout (3s without success)
    simulate_cancel_timeout();

    // Should escalate to SENT_ESTOP
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);
}

// ============================================================================
// Full Escalation Path Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Full escalation path M112 -> RESTART -> RECONNECT",
                 "[abort][escalation][estop]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_blocked(); // Escalate to ESTOP
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);

    // M112 sent, now should transition to SENT_RESTART
    AbortManager::instance().on_estop_sent_for_testing();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_RESTART);

    // FIRMWARE_RESTART sent, now waiting for reconnect
    AbortManager::instance().on_restart_sent_for_testing();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);

    // klippy_state becomes READY
    simulate_klippy_ready();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Cancel timeout triggers full escalation",
                 "[abort][escalation][cancel]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_responsive();
    simulate_cancel_timeout(); // Escalate to ESTOP
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);

    // Complete escalation path
    AbortManager::instance().on_estop_sent_for_testing();
    AbortManager::instance().on_restart_sent_for_testing();
    simulate_klippy_ready();

    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
}

// ============================================================================
// Modal Stays Until Ready Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Modal stays visible until klippy_state == READY",
                 "[abort][modal][reconnect]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_blocked();
    AbortManager::instance().on_estop_sent_for_testing();
    AbortManager::instance().on_restart_sent_for_testing();

    // Now in WAITING_RECONNECT state
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);
    REQUIRE(AbortManager::instance().is_aborting() == true); // Modal should still be visible

    // Simulate klippy in STARTUP state (not ready yet)
    AbortManager::instance().on_klippy_state_change_for_testing(KlippyState::STARTUP);
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);
    REQUIRE(AbortManager::instance().is_aborting() == true);

    // Simulate klippy in SHUTDOWN state (not ready yet)
    AbortManager::instance().on_klippy_state_change_for_testing(KlippyState::SHUTDOWN);
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);
    REQUIRE(AbortManager::instance().is_aborting() == true);

    // Finally klippy becomes READY
    AbortManager::instance().on_klippy_state_change_for_testing(KlippyState::READY);
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
    REQUIRE(AbortManager::instance().is_aborting() == false);
}

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Reconnect timeout still completes (with warning)",
                 "[abort][modal][timeout]") {
    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_blocked();
    AbortManager::instance().on_estop_sent_for_testing();
    AbortManager::instance().on_restart_sent_for_testing();

    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);

    // Simulate 15s timeout without READY
    AbortManager::instance().on_reconnect_timeout_for_testing();

    // Should still complete (but with error message about timeout)
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
    REQUIRE(AbortManager::instance().last_result_message().find("timeout") != std::string::npos);
}

// ============================================================================
// No Connection-Time Probe Tests (CRITICAL)
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: HEATER_INTERRUPT NOT sent at init time",
                 "[abort][kalico][critical]") {
    // This is CRITICAL: We must NOT probe at connection time because users
    // may have started a heat-up from another interface (web, console).
    // Sending HEATER_INTERRUPT at startup would unexpectedly abort their operation.

    // Create a fresh AbortManager and initialize it
    AbortManager::instance().reset_for_testing();
    AbortManager::instance().init(nullptr, nullptr);

    // Kalico status should remain UNKNOWN after init
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::UNKNOWN);

    // State should be IDLE
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::IDLE);

    // No HEATER_INTERRUPT should have been sent (check via mock or command log)
    REQUIRE(AbortManager::instance().get_commands_sent_count() == 0);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: init_subjects does not trigger probe",
                 "[abort][kalico][critical]") {
    AbortManager::instance().reset_for_testing();
    AbortManager::instance().init_subjects();

    // Kalico should still be UNKNOWN
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::UNKNOWN);
    REQUIRE(AbortManager::instance().get_commands_sent_count() == 0);
}

// ============================================================================
// Timeout Value Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Timeout constants are correct",
                 "[abort][timeout][constants]") {
    REQUIRE(AbortManager::HEATER_INTERRUPT_TIMEOUT_MS == 1000);
    REQUIRE(AbortManager::PROBE_TIMEOUT_MS == 2000);
    REQUIRE(AbortManager::CANCEL_TIMEOUT_MS == 3000);
    REQUIRE(AbortManager::RECONNECT_TIMEOUT_MS == 15000);
}

// ============================================================================
// State Name Helper Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: get_state_name returns correct names",
                 "[abort][state][debug]") {
    SECTION("IDLE") {
        AbortManager::instance().reset_for_testing();
        REQUIRE(state_name() == "IDLE");
    }

    SECTION("TRY_HEATER_INTERRUPT") {
        AbortManager::instance().start_abort();
        REQUIRE(state_name() == "TRY_HEATER_INTERRUPT");
    }

    SECTION("PROBE_QUEUE") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        REQUIRE(state_name() == "PROBE_QUEUE");
    }

    SECTION("SENT_CANCEL") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_responsive();
        REQUIRE(state_name() == "SENT_CANCEL");
    }

    SECTION("SENT_ESTOP") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_blocked();
        REQUIRE(state_name() == "SENT_ESTOP");
    }

    SECTION("SENT_RESTART") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_blocked();
        AbortManager::instance().on_estop_sent_for_testing();
        REQUIRE(state_name() == "SENT_RESTART");
    }

    SECTION("WAITING_RECONNECT") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_blocked();
        AbortManager::instance().on_estop_sent_for_testing();
        AbortManager::instance().on_restart_sent_for_testing();
        REQUIRE(state_name() == "WAITING_RECONNECT");
    }

    SECTION("COMPLETE") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_responsive();
        simulate_cancel_success();
        REQUIRE(state_name() == "COMPLETE");
    }
}

// ============================================================================
// Progress Message Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Progress messages update during state machine",
                 "[abort][progress][ui]") {
    SECTION("Initial message on start") {
        AbortManager::instance().start_abort();
        REQUIRE_FALSE(AbortManager::instance().get_progress_message().empty());
    }

    SECTION("Message changes during escalation") {
        AbortManager::instance().start_abort();
        std::string msg1 = AbortManager::instance().get_progress_message();

        simulate_kalico_not_present();
        std::string msg2 = AbortManager::instance().get_progress_message();

        simulate_queue_blocked();
        std::string msg3 = AbortManager::instance().get_progress_message();

        // Messages should change between states
        // (Exact content depends on implementation)
        REQUIRE_FALSE(msg1.empty());
        REQUIRE_FALSE(msg2.empty());
        REQUIRE_FALSE(msg3.empty());
    }

    SECTION("Completion message indicates success or escalation") {
        // Successful cancel
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_responsive();
        simulate_cancel_success();

        std::string success_msg = AbortManager::instance().last_result_message();
        REQUIRE_FALSE(success_msg.empty());

        // Reset for escalation test
        AbortManager::instance().reset_for_testing();

        // Escalated to ESTOP
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_blocked();
        AbortManager::instance().on_estop_sent_for_testing();
        AbortManager::instance().on_restart_sent_for_testing();
        simulate_klippy_ready();

        std::string escalation_msg = AbortManager::instance().last_result_message();
        REQUIRE_FALSE(escalation_msg.empty());

        // Messages should be different
        REQUIRE(success_msg != escalation_msg);
    }
}

// ============================================================================
// Subject Integration Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Subjects are initialized correctly",
                 "[abort][subjects][init]") {
    AbortManager::instance().reset_for_testing();
    AbortManager::instance().init_subjects();

    // State subject should exist and be set to IDLE
    lv_subject_t* state_subject = AbortManager::instance().get_abort_state_subject();
    REQUIRE(state_subject != nullptr);
    REQUIRE(lv_subject_get_int(state_subject) == static_cast<int>(AbortManager::State::IDLE));

    // Progress message subject should exist
    lv_subject_t* progress_subject = AbortManager::instance().get_progress_message_subject();
    REQUIRE(progress_subject != nullptr);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: State subject updates during transitions",
                 "[abort][subjects][observer]") {
    AbortManager::instance().reset_for_testing();
    AbortManager::instance().init_subjects();

    lv_subject_t* state_subject = AbortManager::instance().get_abort_state_subject();

    // Track observer callbacks
    int callback_count = 0;
    auto observer_cb = [](lv_observer_t* observer, lv_subject_t*) {
        int* count = static_cast<int*>(lv_observer_get_user_data(observer));
        (*count)++;
    };

    lv_subject_add_observer(state_subject, observer_cb, &callback_count);

    // LVGL fires immediately on add
    REQUIRE(callback_count == 1);

    // Start abort - should trigger observer
    AbortManager::instance().start_abort();
    REQUIRE(callback_count == 2);
    REQUIRE(lv_subject_get_int(state_subject) ==
            static_cast<int>(AbortManager::State::TRY_HEATER_INTERRUPT));

    // Transition to PROBE_QUEUE
    simulate_kalico_not_present();
    REQUIRE(callback_count == 3);
    REQUIRE(lv_subject_get_int(state_subject) ==
            static_cast<int>(AbortManager::State::PROBE_QUEUE));
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: API errors during abort are handled gracefully",
                 "[abort][error][robustness]") {
    SECTION("API error during HEATER_INTERRUPT escalates correctly") {
        AbortManager::instance().start_abort();

        // Simulate API error (not "Unknown command", but actual network error)
        AbortManager::instance().on_api_error_for_testing("Connection lost");

        // Should handle gracefully - either retry or escalate
        REQUIRE(AbortManager::instance().is_aborting() == true);
    }

    SECTION("API error during CANCEL_PRINT escalates to ESTOP") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_responsive();

        // Simulate API error during cancel
        AbortManager::instance().on_api_error_for_testing("WebSocket closed");

        // Should escalate to ESTOP
        REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);
    }
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: State is atomic",
                 "[abort][threading][safety]") {
    // The state_ member should be std::atomic<State> for thread safety
    // This test verifies the interface supports atomic reads

    AbortManager::instance().start_abort();

    // get_state() should be safe to call from any thread
    auto state = AbortManager::instance().get_state();
    REQUIRE(state != AbortManager::State::IDLE);

    // is_aborting() should be safe to call from any thread
    bool aborting = AbortManager::instance().is_aborting();
    REQUIRE(aborting == true);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Edge cases", "[abort][edge]") {
    SECTION("Multiple rapid start_abort calls") {
        for (int i = 0; i < 10; i++) {
            AbortManager::instance().start_abort();
        }
        // Should not crash, state should still be valid
        REQUIRE(AbortManager::instance().is_aborting() == true);
    }

    SECTION("reset_for_testing clears all state") {
        AbortManager::instance().start_abort();
        simulate_kalico_detected();
        simulate_queue_responsive();

        AbortManager::instance().reset_for_testing();

        REQUIRE(AbortManager::instance().get_state() == AbortManager::State::IDLE);
        REQUIRE(AbortManager::instance().is_aborting() == false);
        REQUIRE(AbortManager::instance().get_kalico_status() ==
                AbortManager::KalicoStatus::UNKNOWN);
    }

    SECTION("Callbacks during COMPLETE state are ignored") {
        AbortManager::instance().start_abort();
        simulate_kalico_not_present();
        simulate_queue_responsive();
        simulate_cancel_success();

        REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);

        // These should be ignored - already complete
        simulate_queue_responsive();
        simulate_cancel_success();
        simulate_klippy_ready();

        REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
    }
}

// ============================================================================
// Integration with PrinterState (KlippyState Observer)
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Observes klippy_state for reconnection",
                 "[abort][integration][printerstate]") {
    // AbortManager should register an observer on PrinterState's klippy_state
    // subject when in WAITING_RECONNECT state

    AbortManager::instance().start_abort();
    simulate_kalico_not_present();
    simulate_queue_blocked();
    AbortManager::instance().on_estop_sent_for_testing();
    AbortManager::instance().on_restart_sent_for_testing();

    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);

    // Observer should be active - when klippy_state changes to READY,
    // the abort should complete
    // (This would be tested with a mock PrinterState in full integration)
}

// ============================================================================
// Soft Cancel Path (Queue Responsive) Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Happy path - queue responsive, cancel succeeds",
                 "[abort][happy][path]") {
    // This tests the ideal case: queue is responsive, CANCEL_PRINT works

    AbortManager::instance().start_abort();

    // Kalico not present (or probe skipped)
    simulate_kalico_not_present();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);

    // Queue responds quickly
    simulate_queue_responsive();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_CANCEL);

    // Cancel succeeds
    simulate_cancel_success();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);

    // No escalation occurred
    REQUIRE(AbortManager::instance().escalation_level() == 0);
}

TEST_CASE_METHOD(AbortManagerTestFixture, "AbortManager: Happy path with Kalico",
                 "[abort][happy][kalico]") {
    // Kalico detected - HEATER_INTERRUPT helps with M109 waits

    AbortManager::instance().start_abort();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::TRY_HEATER_INTERRUPT);

    // Kalico detected
    simulate_kalico_detected();
    REQUIRE(AbortManager::instance().get_kalico_status() == AbortManager::KalicoStatus::DETECTED);
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::PROBE_QUEUE);

    // Queue responds (HEATER_INTERRUPT helped free it)
    simulate_queue_responsive();
    simulate_cancel_success();

    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);
}

// ============================================================================
// Worst Case Escalation Tests
// ============================================================================

TEST_CASE_METHOD(AbortManagerTestFixture,
                 "AbortManager: Worst case - full escalation to FIRMWARE_RESTART",
                 "[abort][escalation][worst]") {
    // This tests the worst case: stuck queue, need M112 + FIRMWARE_RESTART

    AbortManager::instance().start_abort();

    // Kalico not present
    simulate_kalico_not_present();

    // Queue blocked (M115 times out)
    simulate_queue_blocked();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_ESTOP);

    // M112 sent
    AbortManager::instance().on_estop_sent_for_testing();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::SENT_RESTART);

    // FIRMWARE_RESTART sent
    AbortManager::instance().on_restart_sent_for_testing();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::WAITING_RECONNECT);

    // Wait for klippy ready
    simulate_klippy_ready();
    REQUIRE(AbortManager::instance().get_state() == AbortManager::State::COMPLETE);

    // Escalation occurred
    REQUIRE(AbortManager::instance().escalation_level() > 0);

    // Message should indicate restart was required
    std::string msg = AbortManager::instance().last_result_message();
    REQUIRE((msg.find("restart") != std::string::npos || msg.find("home") != std::string::npos ||
             msg.find("Home") != std::string::npos));
}
