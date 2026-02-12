// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test_utils.h"

#include "ui_modal.h"
#include "ui_update_queue.h"

#include "lib/lvgl/src/misc/lv_timer_private.h"
#include "spdlog/spdlog.h"
#include "test_helpers/update_queue_test_access.h"

#include <chrono>
#include <filesystem>
#include <thread>

// ============================================================================
// LVGL safe initialization (idempotent)
// ============================================================================

void lv_init_safe() {
    if (!lv_is_initialized()) {
        lv_init();
    }
    // UpdateQueue init is handled by LVGLTestFixture constructor per-test,
    // NOT here. Having it here (called once via call_once) conflicts with
    // the per-test shutdown/reinit lifecycle in the fixture destructor.
}

uint32_t lv_timer_handler_safe() {
    // Drain the UpdateQueue — executes pending callbacks which set subjects.
    // Subject observers fire synchronously during drain, propagating bindings.
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

    // Pause ALL timers to prevent infinite handler loops, then selectively
    // execute one-shot timers (lv_async_call, retry timers) manually.
    //
    // Background: LVGL's test fixture leaks display refresh timers with stale
    // last_run timestamps. When lv_timer_handler()'s do-while loop processes
    // them all simultaneously, any timer fire that creates/deletes a timer
    // restarts the loop from the head — infinite loop.
    lv_timer_t* t = lv_timer_get_next(nullptr);
    while (t) {
        lv_timer_pause(t);
        t = lv_timer_get_next(t);
    }

    // Execute one-shot timers (repeat_count >= 1) that are ready.
    // These include lv_async_call (period=0, repeat=1) and scheduled
    // retry timers. Process in a loop since callbacks may create new ones.
    uint32_t now = lv_tick_get();
    for (int safety = 0; safety < 100; safety++) {
        bool found = false;
        t = lv_timer_get_next(nullptr);
        while (t) {
            lv_timer_t* next = lv_timer_get_next(t); // Save next before potential deletion
            if (t->repeat_count > 0 && (now - t->last_run >= t->period)) {
                if (t->timer_cb) {
                    t->timer_cb(t);
                    found = true;
                    break; // Restart iteration since list may have changed
                }
            }
            t = next;
        }
        if (!found)
            break; // No more ready one-shot timers
    }

    // Call lv_timer_handler() with all timers paused (no-op, just updates state)
    uint32_t result = lv_timer_handler();

    // Resume all timers
    t = lv_timer_get_next(nullptr);
    while (t) {
        lv_timer_resume(t);
        t = lv_timer_get_next(t);
    }

    return result;
}

namespace UITest {

// Virtual input device for simulating touches/clicks
static lv_indev_t* virtual_indev = nullptr;
static lv_indev_data_t last_data;

// Input device read callback
static void virtual_indev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    // Copy last simulated input state
    *data = last_data;
}

void init(lv_obj_t* screen) {
    (void)screen; // Screen parameter reserved for future use

    if (virtual_indev) {
        spdlog::warn("[UITest] Already initialized");
        return;
    }

    spdlog::info("[UITest] Initializing virtual input device");

    // Initialize last_data
    last_data.point.x = 0;
    last_data.point.y = 0;
    last_data.state = LV_INDEV_STATE_RELEASED;

    // Create virtual input device
    virtual_indev = lv_indev_create();
    lv_indev_set_type(virtual_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(virtual_indev, virtual_indev_read_cb);

    spdlog::info("[UITest] Virtual input device created");
}

void cleanup() {
    if (virtual_indev) {
        // Note: Don't call lv_indev_delete() - lv_deinit() handles cleanup
        // Just null our reference so we don't use a stale pointer
        virtual_indev = nullptr;
        spdlog::info("[UITest] Virtual input device reference cleared");
    }
}

bool click(lv_obj_t* widget) {
    if (!widget || !virtual_indev) {
        spdlog::error("[UITest] Invalid widget or input device not initialized");
        return false;
    }

    // Get widget center coordinates
    int32_t x = lv_obj_get_x(widget) + lv_obj_get_width(widget) / 2;
    int32_t y = lv_obj_get_y(widget) + lv_obj_get_height(widget) / 2;

    // Convert to absolute coordinates if widget has parent
    lv_obj_t* parent = lv_obj_get_parent(widget);
    while (parent) {
        x += lv_obj_get_x(parent);
        y += lv_obj_get_y(parent);
        parent = lv_obj_get_parent(parent);
    }

    return click_at(x, y);
}

bool click_at(int32_t x, int32_t y) {
    if (!virtual_indev) {
        spdlog::error("[UITest] Input device not initialized - call init() first");
        return false;
    }

    spdlog::debug("[UITest] Simulating click at ({}, {})", x, y);

    // Simulate press
    last_data.point.x = x;
    last_data.point.y = y;
    last_data.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(virtual_indev); // Directly read indev to process press
    wait_ms(50);                  // Minimum press duration

    // Simulate release
    last_data.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(virtual_indev); // Directly read indev to process release
    wait_ms(50);                  // Allow click handlers to execute

    spdlog::debug("[UITest] Click simulation complete");
    return true;
}

bool type_text(const std::string& text) {
    // Get focused textarea
    lv_obj_t* focused = lv_group_get_focused(lv_group_get_default());
    if (!focused) {
        spdlog::error("[UITest] No focused textarea");
        return false;
    }

    // Check if it's a textarea
    if (!lv_obj_check_type(focused, &lv_textarea_class)) {
        spdlog::error("[UITest] Focused widget is not a textarea");
        return false;
    }

    spdlog::debug("[UITest] Typing text: {}", text);

    // Add text directly to textarea
    lv_textarea_add_text(focused, text.c_str());
    lv_timer_handler_safe();
    wait_ms(50); // Allow text processing

    return true;
}

bool type_text(lv_obj_t* textarea, const std::string& text) {
    if (!textarea) {
        spdlog::error("[UITest] Invalid textarea");
        return false;
    }

    // Check if it's a textarea
    if (!lv_obj_check_type(textarea, &lv_textarea_class)) {
        spdlog::error("[UITest] Widget is not a textarea");
        return false;
    }

    spdlog::debug("[UITest] Typing text into textarea: {}", text);

    // Add text directly to textarea
    lv_textarea_add_text(textarea, text.c_str());
    lv_timer_handler_safe();
    wait_ms(50); // Allow text processing

    return true;
}

bool send_key(uint32_t key) {
    // Get focused textarea
    lv_obj_t* focused = lv_group_get_focused(lv_group_get_default());
    if (!focused) {
        spdlog::error("[UITest] No focused widget");
        return false;
    }

    spdlog::debug("[UITest] Sending key: {}", key);

    // For special keys (Enter, Backspace, etc.), use appropriate LVGL functions
    if (lv_obj_check_type(focused, &lv_textarea_class)) {
        if (key == LV_KEY_BACKSPACE) {
            lv_textarea_delete_char(focused);
        } else if (key == LV_KEY_ENTER) {
            // Trigger READY event on textarea
            lv_obj_send_event(focused, LV_EVENT_READY, nullptr);
        }
        lv_timer_handler_safe();
        wait_ms(50);
        return true;
    }

    spdlog::warn("[UITest] send_key() only supports textarea widgets");
    return false;
}

void wait_ms(uint32_t ms) {
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < end) {
        lv_timer_handler_safe(); // Process LVGL tasks
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

bool wait_until(std::function<bool()> condition, uint32_t timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < end) {
        lv_timer_handler_safe(); // Process LVGL tasks

        if (condition()) {
            return true; // Condition met
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    spdlog::warn("[UITest] wait_until() timed out after {}ms", timeout_ms);
    return false; // Timeout
}

bool wait_for_visible(lv_obj_t* widget, uint32_t timeout_ms) {
    if (!widget) {
        spdlog::error("[UITest] Invalid widget");
        return false;
    }

    return wait_until([widget]() { return !lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN); },
                      timeout_ms);
}

bool wait_for_hidden(lv_obj_t* widget, uint32_t timeout_ms) {
    if (!widget) {
        spdlog::error("[UITest] Invalid widget");
        return false;
    }

    return wait_until([widget]() { return lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN); },
                      timeout_ms);
}

bool wait_for_timers(uint32_t timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < end) {
        uint32_t next_timer = lv_timer_handler_safe();

        // If next timer is in the far future (> 1 second), no active timers
        if (next_timer > 1000) {
            spdlog::debug("[UITest] All timers completed");
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    spdlog::warn("[UITest] wait_for_timers() timed out after {}ms", timeout_ms);
    return false;
}

bool is_visible(lv_obj_t* widget) {
    if (!widget) {
        return false;
    }
    return !lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN);
}

std::string get_text(lv_obj_t* widget) {
    if (!widget) {
        return "";
    }

    // Try label first
    if (lv_obj_check_type(widget, &lv_label_class)) {
        const char* text = lv_label_get_text(widget);
        return text ? std::string(text) : "";
    }

    // Try textarea
    if (lv_obj_check_type(widget, &lv_textarea_class)) {
        const char* text = lv_textarea_get_text(widget);
        return text ? std::string(text) : "";
    }

    spdlog::warn("[UITest] get_text() called on non-text widget");
    return "";
}

bool is_checked(lv_obj_t* widget) {
    if (!widget) {
        return false;
    }
    return lv_obj_has_state(widget, LV_STATE_CHECKED);
}

lv_obj_t* find_by_name(lv_obj_t* parent, const std::string& name) {
    if (!parent) {
        return nullptr;
    }
    return lv_obj_find_by_name(parent, name.c_str());
}

int count_children_with_marker(lv_obj_t* parent, const char* marker) {
    if (!parent || !marker) {
        return 0;
    }

    int count = 0;
    uint32_t child_count = lv_obj_get_child_count(parent);

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child)
            continue;

        const void* user_data = lv_obj_get_user_data(child);
        if (user_data && strcmp((const char*)user_data, marker) == 0) {
            count++;
        }
    }

    return count;
}

} // namespace UITest

// Stub implementations for main.cpp functions needed by wizard tests
// These return nullptr since wizard tests don't actually use Moonraker
#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_state.h"

MoonrakerClient* get_moonraker_client() {
    return nullptr;
}

MoonrakerAPI* get_moonraker_api() {
    return nullptr;
}

PrinterState& get_printer_state() {
    static PrinterState instance;
    return instance;
}

// Stub implementations for notification functions (tests don't display UI)
void ui_notification_init() {
    // No-op in tests
}

void ui_notification_info(const char* message) {
    spdlog::debug("[Test Stub] ui_notification_info: {}", message ? message : "(null)");
}

void ui_notification_info(const char* title, const char* message) {
    spdlog::debug("[Test Stub] ui_notification_info: {} - {}", title ? title : "(null)",
                  message ? message : "(null)");
}

void ui_notification_success(const char* message) {
    spdlog::debug("[Test Stub] ui_notification_success: {}", message ? message : "(null)");
}

void ui_notification_success(const char* title, const char* message) {
    spdlog::debug("[Test Stub] ui_notification_success: {} - {}", title ? title : "(null)",
                  message ? message : "(null)");
}

void ui_notification_warning(const char* message) {
    spdlog::debug("[Test Stub] ui_notification_warning: {}", message ? message : "(null)");
}

void ui_notification_warning(const char* title, const char* message) {
    spdlog::debug("[Test Stub] ui_notification_warning: {} - {}", title ? title : "(null)",
                  message ? message : "(null)");
}

void ui_notification_error(const char* title, const char* message, bool modal) {
    spdlog::debug("[Test Stub] ui_notification_error: {} - {} (modal={})", title ? title : "(null)",
                  message ? message : "(null)", modal);
}

// Stub implementations for toast functions (tests don't display UI)
#include "ui_toast_manager.h"

void ui_toast_init() {
    // No-op in tests
}

void ui_toast_show(ToastSeverity severity, const char* message, uint32_t duration_ms) {
    (void)severity;
    (void)duration_ms;
    spdlog::debug("[Test Stub] ui_toast_show: {}", message ? message : "(null)");
}

void ui_toast_show_with_action(ToastSeverity severity, const char* message, const char* action_text,
                               toast_action_callback_t action_callback, void* user_data,
                               uint32_t duration_ms) {
    (void)severity;
    (void)action_text;
    (void)action_callback;
    (void)user_data;
    (void)duration_ms;
    spdlog::debug("[Test Stub] ui_toast_show_with_action: {}", message ? message : "(null)");
}

void ui_toast_hide() {
    // No-op in tests
}

bool ui_toast_is_visible() {
    return false;
}

// Stub ToastManager class for tests
// The real ToastManager is excluded from test build, so we need a stub singleton
static ToastManager* s_test_toast_manager_instance = nullptr;

ToastManager& ToastManager::instance() {
    if (!s_test_toast_manager_instance) {
        s_test_toast_manager_instance = new ToastManager();
    }
    return *s_test_toast_manager_instance;
}

ToastManager::~ToastManager() {
    // Stub destructor
}

void ToastManager::init() {
    spdlog::debug("[Test Stub] ToastManager::init()");
}

void ToastManager::show(ToastSeverity severity, const char* message, uint32_t duration_ms) {
    (void)severity;
    (void)duration_ms;
    spdlog::debug("[Test Stub] ToastManager::show: {}", message ? message : "(null)");
}

void ToastManager::show_with_action(ToastSeverity severity, const char* message,
                                    const char* action_text,
                                    toast_action_callback_t action_callback, void* user_data,
                                    uint32_t duration_ms) {
    (void)severity;
    (void)action_text;
    (void)action_callback;
    (void)user_data;
    (void)duration_ms;
    spdlog::debug("[Test Stub] ToastManager::show_with_action: {}", message ? message : "(null)");
}

void ToastManager::hide() {
    spdlog::debug("[Test Stub] ToastManager::hide()");
}

bool ToastManager::is_visible() const {
    return false;
}

// Stub implementations for EmergencyStopOverlay (tests don't use the overlay)
#include "ui_emergency_stop.h"

// The real EmergencyStopOverlay singleton is used - all methods are provided
// as stubs that satisfy the linker. Tests that need real behavior should
// call the methods directly (they're safe with LVGL initialized).

EmergencyStopOverlay& EmergencyStopOverlay::instance() {
    static EmergencyStopOverlay inst;
    return inst;
}

void EmergencyStopOverlay::init(PrinterState& /* printer_state */, MoonrakerAPI* /* api */) {}

void EmergencyStopOverlay::init_subjects() {
    if (subjects_initialized_)
        return;
    UI_MANAGED_SUBJECT_INT(estop_visible_, 0, "estop_visible", subjects_);
    UI_MANAGED_SUBJECT_STRING(recovery_title_subject_, recovery_title_buf_, "Printer Shutdown",
                              "recovery_title", subjects_);
    UI_MANAGED_SUBJECT_STRING(recovery_message_subject_, recovery_message_buf_, "",
                              "recovery_message", subjects_);
    UI_MANAGED_SUBJECT_INT(recovery_can_restart_, 1, "recovery_can_restart", subjects_);
    subjects_initialized_ = true;
}

void EmergencyStopOverlay::deinit_subjects() {
    if (!subjects_initialized_)
        return;
    // Reset dialog state — screen destruction invalidates these pointers
    recovery_dialog_ = nullptr;
    confirmation_dialog_ = nullptr;
    recovery_reason_ = RecoveryReason::NONE;
    suppress_recovery_until_ = 0;
    restart_in_progress_ = false;
    subjects_.deinit_all();
    subjects_initialized_ = false;
}
void EmergencyStopOverlay::create() {}
void EmergencyStopOverlay::update_visibility() {}
void EmergencyStopOverlay::set_require_confirmation(bool /* require */) {}

void EmergencyStopOverlay::show_recovery_for(RecoveryReason reason) {
    if (is_recovery_suppressed())
        return;

    // If dialog already showing, update reason if connection dropped
    if (recovery_dialog_) {
        if (reason == RecoveryReason::DISCONNECTED &&
            recovery_reason_ == RecoveryReason::SHUTDOWN) {
            recovery_reason_ = RecoveryReason::DISCONNECTED;
            ui_async_call(
                [](void*) { EmergencyStopOverlay::instance().update_recovery_dialog_content(); },
                nullptr);
        }
        return;
    }

    recovery_reason_ = reason;
    ui_async_call(
        [](void*) {
            auto& inst = EmergencyStopOverlay::instance();
            if (inst.recovery_dialog_)
                return;
            inst.show_recovery_dialog();
            inst.update_recovery_dialog_content();
        },
        nullptr);
}

void EmergencyStopOverlay::suppress_recovery_dialog(uint32_t duration_ms) {
    suppress_recovery_until_ = lv_tick_get() + duration_ms;
}

bool EmergencyStopOverlay::is_recovery_suppressed() const {
    if (suppress_recovery_until_ == 0)
        return false;
    return lv_tick_elaps(suppress_recovery_until_) > (UINT32_MAX / 2);
}

void EmergencyStopOverlay::show_recovery_dialog() {
    if (recovery_dialog_)
        return;
    // Use Modal system — backdrop is created programmatically
    recovery_dialog_ = ui_modal_show("klipper_recovery_dialog");
    if (recovery_dialog_) {
        // XML <view name="..."> is not applied by lv_xml_create — set explicitly for lookups
        lv_obj_set_name(recovery_dialog_, "klipper_recovery_card");
    }
}

void EmergencyStopOverlay::dismiss_recovery_dialog() {
    if (recovery_dialog_) {
        ui_modal_hide(recovery_dialog_);
        recovery_dialog_ = nullptr;
        recovery_reason_ = RecoveryReason::NONE;
    }
}

void EmergencyStopOverlay::update_recovery_dialog_content() {
    const char* title = "Printer Error";
    const char* message = "An unexpected printer error occurred.";

    if (recovery_reason_ == RecoveryReason::SHUTDOWN) {
        title = "Printer Shutdown";
        message = "Klipper has entered shutdown state.";
    } else if (recovery_reason_ == RecoveryReason::DISCONNECTED) {
        title = "Printer Firmware Disconnected";
        message = "Klipper firmware has disconnected from the host.";
    }

    // Update subjects — XML bindings react automatically
    lv_subject_copy_string(&recovery_title_subject_, title);
    lv_subject_copy_string(&recovery_message_subject_, message);
    lv_subject_set_int(&recovery_can_restart_,
                       recovery_reason_ != RecoveryReason::DISCONNECTED ? 1 : 0);
}

// Remaining methods are no-ops (button handlers, etc.)
void EmergencyStopOverlay::handle_click() {}
void EmergencyStopOverlay::execute_emergency_stop() {}
void EmergencyStopOverlay::show_confirmation_dialog() {}
void EmergencyStopOverlay::dismiss_confirmation_dialog() {}
void EmergencyStopOverlay::restart_klipper() {}
void EmergencyStopOverlay::firmware_restart() {}

void EmergencyStopOverlay::emergency_stop_clicked(lv_event_t*) {}
void EmergencyStopOverlay::estop_dialog_cancel_clicked(lv_event_t*) {}
void EmergencyStopOverlay::estop_dialog_confirm_clicked(lv_event_t*) {}
void EmergencyStopOverlay::recovery_restart_klipper_clicked(lv_event_t*) {}
void EmergencyStopOverlay::recovery_firmware_restart_clicked(lv_event_t*) {}
void EmergencyStopOverlay::recovery_dismiss_clicked(lv_event_t*) {}
void EmergencyStopOverlay::advanced_estop_clicked(lv_event_t*) {}
void EmergencyStopOverlay::advanced_restart_klipper_clicked(lv_event_t*) {}
void EmergencyStopOverlay::advanced_firmware_restart_clicked(lv_event_t*) {}
void EmergencyStopOverlay::home_firmware_restart_clicked(lv_event_t*) {}

// Text input widget implementation for tests
// This is a full implementation, not a stub, because tests need to actually
// test the text_input widget's placeholder and max_length attributes.
#include "ui_text_input.h"

#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/lv_xml_utils.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_textarea_parser.h"

// Magic value to identify text_input widgets
static constexpr uintptr_t TEXT_INPUT_MAGIC = 0xBADC0DE0;
static constexpr uintptr_t TEXT_INPUT_HINT_MASK = 0x0000000F;

KeyboardHint ui_text_input_get_keyboard_hint(lv_obj_t* textarea) {
    if (textarea == nullptr) {
        return KeyboardHint::TEXT;
    }
    auto user_data = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(textarea));
    if ((user_data & 0xFFFFFFF0) != TEXT_INPUT_MAGIC) {
        return KeyboardHint::TEXT;
    }
    return static_cast<KeyboardHint>(user_data & TEXT_INPUT_HINT_MASK);
}

// Stub for notification manager functions (tests don't have notification UI)
#include "ui_notification.h"
#include "ui_notification_manager.h"
#include "ui_printer_status_icon.h"

void ui_notification_update(NotificationStatus /* status */) {
    // No-op in tests
}

void ui_notification_update_count(size_t /* count */) {
    // No-op in tests
}

// Text input widget create callback
static void* ui_text_input_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    lv_obj_t* parent = static_cast<lv_obj_t*>(lv_xml_state_get_parent(state));
    lv_obj_t* textarea = lv_textarea_create(parent);

    // One-line mode by default for form inputs
    lv_textarea_set_one_line(textarea, true);

    // Set default keyboard hint (TEXT) via user_data magic value
    lv_obj_set_user_data(
        textarea,
        reinterpret_cast<void*>(TEXT_INPUT_MAGIC | static_cast<uintptr_t>(KeyboardHint::TEXT)));

    return textarea;
}

// Text input widget apply callback
static void ui_text_input_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // First apply standard textarea properties
    lv_xml_textarea_apply(state, attrs);

    lv_obj_t* textarea = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));

    // Handle our custom attributes
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (lv_streq("placeholder", name)) {
            // Shorthand for placeholder_text
            lv_textarea_set_placeholder_text(textarea, value);
        } else if (lv_streq("max_length", name)) {
            lv_textarea_set_max_length(textarea, lv_xml_atoi(value));
        } else if (lv_streq("keyboard_hint", name)) {
            KeyboardHint hint = KeyboardHint::TEXT;
            if (std::strcmp(value, "numeric") == 0) {
                hint = KeyboardHint::NUMERIC;
            }
            lv_obj_set_user_data(
                textarea, reinterpret_cast<void*>(TEXT_INPUT_MAGIC | static_cast<uintptr_t>(hint)));
        }
    }
}

void ui_text_input_init() {
    lv_xml_register_widget("text_input", ui_text_input_create, ui_text_input_apply);
    spdlog::debug("[ui_text_input] Registered <text_input> widget");
}

// Stub for app_request_restart (tests don't restart)
void app_request_restart() {
    spdlog::debug("[Test Stub] app_request_restart called - no-op in tests");
}

// Stub for app_request_restart_service (tests don't restart)
void app_request_restart_service() {
    spdlog::debug("[Test Stub] app_request_restart_service called - no-op in tests");
}

// Stub for get_helix_cache_dir (tests use temp directory)
// Respects HELIX_CACHE_DIR env var for testing the override, falls back to /tmp
#include "app_globals.h"
std::string get_helix_cache_dir(const std::string& subdir) {
    const char* helix_cache = std::getenv("HELIX_CACHE_DIR");
    if (helix_cache && helix_cache[0] != '\0') {
        std::string path = std::string(helix_cache) + "/" + subdir;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (!ec && std::filesystem::exists(path)) {
            return path;
        }
        // Fall through if HELIX_CACHE_DIR path is invalid
    }
    std::string path = "/tmp/helix_test_" + subdir;
    std::filesystem::create_directories(path);
    return path;
}

// Stub for get_moonraker_manager (tests don't have manager)
MoonrakerManager* get_moonraker_manager() {
    return nullptr;
}

// Stub for get_print_history_manager (tests don't have manager)
class PrintHistoryManager;
PrintHistoryManager* get_print_history_manager() {
    return nullptr;
}

// Stub for get_temperature_history_manager (tests don't have manager)
class TemperatureHistoryManager;
TemperatureHistoryManager* get_temperature_history_manager() {
    return nullptr;
}

// Stub for MoonrakerManager::macro_analysis (never called since get_moonraker_manager returns null)
#include "moonraker_manager.h"
namespace helix {
class MacroModificationManager;
}
helix::MacroModificationManager* MoonrakerManager::macro_analysis() const {
    return nullptr;
}

// Stub for MoonrakerManager::connect (never called since get_moonraker_manager returns null)
int MoonrakerManager::connect(const std::string& /*websocket_url*/,
                              const std::string& /*http_base_url*/) {
    return -1;
}

// ============================================================================
// Stubs for LVGLUITestFixture - Full UI Integration Tests
// ============================================================================
// These stubs support tests that need more complete UI initialization
// but don't need real network/hardware connections.

// Stub for app_globals_init_subjects (creates test notification subject)
static lv_subject_t s_test_notification_subject;
static bool s_test_notification_subject_initialized = false;

void app_globals_init_subjects() {
    if (!s_test_notification_subject_initialized) {
        lv_subject_init_pointer(&s_test_notification_subject, nullptr);
        s_test_notification_subject_initialized = true;
        spdlog::debug("[Test Stub] app_globals_init_subjects: notification subject initialized");
    }
}

void app_globals_deinit_subjects() {
    if (s_test_notification_subject_initialized) {
        lv_subject_deinit(&s_test_notification_subject);
        s_test_notification_subject_initialized = false;
        spdlog::debug(
            "[Test Stub] app_globals_deinit_subjects: notification subject deinitialized");
    }
}

lv_subject_t& get_notification_subject() {
    if (!s_test_notification_subject_initialized) {
        app_globals_init_subjects();
    }
    return s_test_notification_subject;
}

// Stub for ui_notification_init_subjects (creates test subjects for notification badge)
static lv_subject_t s_test_notification_count_subject;
static bool s_test_notification_subjects_initialized = false;

void ui_notification_init_subjects() {
    if (!s_test_notification_subjects_initialized) {
        lv_subject_init_int(&s_test_notification_count_subject, 0);
        s_test_notification_subjects_initialized = true;
        spdlog::debug("[Test Stub] ui_notification_init_subjects: subjects initialized");
    }
}

void ui_notification_deinit_subjects() {
    if (s_test_notification_subjects_initialized) {
        lv_subject_deinit(&s_test_notification_count_subject);
        s_test_notification_subjects_initialized = false;
        spdlog::debug("[Test Stub] ui_notification_deinit_subjects: subjects deinitialized");
    }
}

void ui_notification_register_callbacks() {
    spdlog::debug("[Test Stub] ui_notification_register_callbacks: no-op in tests");
}

void ui_notification_manager_init() {
    spdlog::debug("[Test Stub] ui_notification_manager_init: no-op in tests");
}

// Stub for PrinterStatusIcon functions
void ui_printer_status_icon_init_subjects() {
    spdlog::debug("[Test Stub] ui_printer_status_icon_init_subjects: no-op in tests");
}

void ui_printer_status_icon_init() {
    spdlog::debug("[Test Stub] ui_printer_status_icon_init: no-op in tests");
}

void ui_printer_status_icon_deinit_subjects() {
    spdlog::debug("[Test Stub] ui_printer_status_icon_deinit_subjects: no-op in tests");
}
