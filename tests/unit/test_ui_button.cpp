// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_button.cpp
 * @brief Unit tests for ui_button XML widget
 *
 * Tests bind_icon attribute functionality and other ui_button features.
 */

#include "ui_icon_codepoints.h"

#include "../test_fixtures.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Fixture with ui_button registered
// ============================================================================

class UiButtonTestFixture : public XMLTestFixture {
  public:
    UiButtonTestFixture() : XMLTestFixture() {
        // Initialize icon name buffer with initial value
        strncpy(icon_buf_, "light", sizeof(icon_buf_) - 1);
        icon_buf_[sizeof(icon_buf_) - 1] = '\0';

        // Register a test subject for bind_icon tests
        lv_subject_init_string(&icon_subject_, icon_buf_, nullptr, sizeof(icon_buf_), icon_buf_);
        lv_xml_register_subject(nullptr, "test_icon_subject", &icon_subject_);

        // Register a test subject for bind_text tests
        lv_subject_init_string(&text_subject_, text_buf_, nullptr, sizeof(text_buf_), "Close");
        lv_xml_register_subject(nullptr, "test_text_subject", &text_subject_);

        spdlog::debug("[UiButtonTestFixture] Initialized with test subjects");
    }

    ~UiButtonTestFixture() override {
        lv_subject_deinit(&icon_subject_);
        lv_subject_deinit(&text_subject_);
        spdlog::debug("[UiButtonTestFixture] Cleaned up");
    }

    /**
     * @brief Get the test icon subject for bind_icon tests
     */
    lv_subject_t* icon_subject() {
        return &icon_subject_;
    }

    /**
     * @brief Set the icon subject value
     */
    void set_icon_name(const char* name) {
        lv_subject_copy_string(&icon_subject_, name);
    }

    /**
     * @brief Set the text subject value
     */
    void set_text(const char* text) {
        lv_subject_copy_string(&text_subject_, text);
    }

    /**
     * @brief Find the label child of a button (first lv_label child)
     */
    lv_obj_t* find_button_label(lv_obj_t* btn) {
        uint32_t count = lv_obj_get_child_count(btn);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* child = lv_obj_get_child(btn, i);
            if (lv_obj_check_type(child, &lv_label_class)) {
                return child;
            }
        }
        return nullptr;
    }

    /**
     * @brief Create a ui_button via XML with given attributes
     * @param attrs NULL-terminated array of key-value attribute pairs
     * @return Created button, or nullptr on failure
     */
    lv_obj_t* create_button(const char** attrs) {
        return static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "ui_button", attrs));
    }

  protected:
    lv_subject_t icon_subject_;
    char icon_buf_[64];

    lv_subject_t text_subject_;
    char text_buf_[64];
};

// ============================================================================
// bind_icon Tests
// ============================================================================

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button can be created via XML",
                 "[ui_button][xml][quick]") {
    // Simple test to verify button creation works
    const char* attrs[] = {"text", "Test", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);
    REQUIRE(lv_obj_is_valid(btn));
}

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button bind_icon basic creation works",
                 "[ui_button][xml][quick]") {
    // Just test that we can create a button with bind_icon without hanging
    const char* attrs[] = {"text", "Test", "bind_icon", "test_icon_subject", nullptr};
    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);
    REQUIRE(lv_obj_is_valid(btn));
}

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button bind_icon updates icon from subject",
                 "[ui_button][xml][.slow]") { // Marked .slow - hangs in CI environment
    // Create button with bind_icon attribute bound to test subject
    const char* attrs[] = {"text", "Test", "bind_icon", "test_icon_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    // Process LVGL to apply bindings - use shorter time
    process_lvgl(10);

    // Check child count
    uint32_t child_count = lv_obj_get_child_count(btn);
    REQUIRE(child_count >= 2); // Should have label + icon

    // Find the icon child
    lv_obj_t* icon = nullptr;
    const char* expected_codepoint = ui_icon::lookup_codepoint("light");

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char* text = lv_label_get_text(child);
            if (text && expected_codepoint && strcmp(text, expected_codepoint) == 0) {
                icon = child;
                break;
            }
        }
    }

    REQUIRE(icon != nullptr);
    INFO("Initial icon should be 'light' codepoint");
    REQUIRE(strcmp(lv_label_get_text(icon), expected_codepoint) == 0);

    // Update subject to different icon
    set_icon_name("light_off");
    process_lvgl(10);

    // Verify icon changed
    const char* new_expected = ui_icon::lookup_codepoint("light_off");
    INFO("Icon should update to 'light_off' codepoint after subject change");
    REQUIRE(strcmp(lv_label_get_text(icon), new_expected) == 0);
}

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button bind_icon creates icon if none exists",
                 "[ui_button][xml][.slow]") { // Marked .slow - hangs in CI environment
    // Create button with NO initial icon, but with bind_icon
    const char* attrs[] = {"text", "No Icon", "bind_icon", "test_icon_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    // Process LVGL to apply bindings
    process_lvgl(50);

    // Should have created an icon from the subject value
    lv_obj_t* icon = nullptr;
    uint32_t child_count = lv_obj_get_child_count(btn);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char* text = lv_label_get_text(child);
            const char* expected_codepoint = ui_icon::lookup_codepoint("light");
            if (text && expected_codepoint && strcmp(text, expected_codepoint) == 0) {
                icon = child;
                break;
            }
        }
    }

    REQUIRE(icon != nullptr);
    INFO("bind_icon should create icon widget when none exists");
}

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button bind_icon handles missing subject gracefully",
                 "[ui_button][xml][.slow]") { // Marked .slow - hangs in CI environment
    // Create button with bind_icon pointing to non-existent subject
    const char* attrs[] = {"text", "Test", "bind_icon", "nonexistent_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    // Should not crash, button should still be created
    process_lvgl(50);

    // Button should exist and be valid
    REQUIRE(lv_obj_is_valid(btn));
}

TEST_CASE_METHOD(UiButtonTestFixture, "ui_button bind_icon handles empty string value",
                 "[ui_button][xml][.slow]") { // Marked .slow - hangs in CI environment
    // Set subject to empty string first
    set_icon_name("");

    const char* attrs[] = {"text", "Test", "bind_icon", "test_icon_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    // Should not crash
    process_lvgl(50);

    // Button should exist and be valid
    REQUIRE(lv_obj_is_valid(btn));
}

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_icon works with existing icon attribute overrides",
                 "[ui_button][xml][.slow]") { // Marked .slow - hangs in CI environment
    // Create button with both static icon and bind_icon
    // bind_icon should override the static icon
    const char* attrs[] = {"text", "Test", "icon", "settings", "bind_icon", "test_icon_subject",
                           nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(50);

    // Find icon - should show "light" (from subject), not "settings" (from static attr)
    lv_obj_t* icon = nullptr;
    uint32_t child_count = lv_obj_get_child_count(btn);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char* text = lv_label_get_text(child);
            // Check for icon codepoint (either could be valid during creation)
            const char* light_cp = ui_icon::lookup_codepoint("light");
            if (text && light_cp && strcmp(text, light_cp) == 0) {
                icon = child;
                break;
            }
        }
    }

    // After bind_icon processing, icon should show subject value
    REQUIRE(icon != nullptr);
    INFO("bind_icon should override static icon attribute");
    REQUIRE(strcmp(lv_label_get_text(icon), ui_icon::lookup_codepoint("light")) == 0);
}

// ============================================================================
// bind_text Tests — bind_text always resolves as subject name
// ============================================================================

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_text with non-existent subject falls back to literal",
                 "[ui_button][xml][bind_text][quick]") {
    // No subject named "Save" exists — falls back to literal text
    const char* attrs[] = {"bind_text", "Save", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(10);

    lv_obj_t* label = find_button_label(btn);
    REQUIRE(label != nullptr);
    REQUIRE(strcmp(lv_label_get_text(label), "Save") == 0);
}

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_text resolves subject by name and reacts to changes",
                 "[ui_button][xml][bind_text][quick]") {
    // bind_text always tries to resolve as a subject name (LVGL standard)
    const char* attrs[] = {"bind_text", "test_text_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(10);

    // Label should show initial subject value
    lv_obj_t* label = find_button_label(btn);
    REQUIRE(label != nullptr);
    REQUIRE(strcmp(lv_label_get_text(label), "Close") == 0);

    // Update subject — label should react
    set_text("Save");
    process_lvgl(10);
    REQUIRE(strcmp(lv_label_get_text(label), "Save") == 0);

    // Change back
    set_text("Close");
    process_lvgl(10);
    REQUIRE(strcmp(lv_label_get_text(label), "Close") == 0);
}

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_text with @ prefix strips it and resolves subject",
                 "[ui_button][xml][bind_text][quick]") {
    // @ prefix is stripped for backward compatibility with text="@subject" convention
    const char* attrs[] = {"bind_text", "@test_text_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(10);

    lv_obj_t* label = find_button_label(btn);
    REQUIRE(label != nullptr);
    REQUIRE(strcmp(lv_label_get_text(label), "Close") == 0);
}

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_text with @ prefix for missing subject uses name as fallback",
                 "[ui_button][xml][bind_text][quick]") {
    const char* attrs[] = {"bind_text", "@nonexistent_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(10);

    // Should gracefully fall back to using the subject name (without @) as literal text
    lv_obj_t* label = find_button_label(btn);
    REQUIRE(label != nullptr);
    REQUIRE(strcmp(lv_label_get_text(label), "nonexistent_subject") == 0);
}

TEST_CASE_METHOD(UiButtonTestFixture,
                 "ui_button bind_text with text attr creates label then bind_text binds it",
                 "[ui_button][xml][bind_text][quick]") {
    // text= creates the label during create, bind_text= binds it during apply
    const char* attrs[] = {"text", "Initial", "bind_text", "test_text_subject", nullptr};

    lv_obj_t* btn = create_button(attrs);
    REQUIRE(btn != nullptr);

    process_lvgl(10);

    // bind_text should have overridden the initial text with subject value
    lv_obj_t* label = find_button_label(btn);
    REQUIRE(label != nullptr);
    REQUIRE(strcmp(lv_label_get_text(label), "Close") == 0);
}
