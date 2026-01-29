// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_lazy_overlay.cpp
 * @brief Unit tests for lazy_push_overlay helper template
 *
 * Tests the lazy overlay creation pattern without requiring LVGL runtime.
 * Uses mock pointers to verify:
 * - Lazy initialization (only creates once)
 * - Cache reuse on subsequent calls
 * - Error handling when creation fails
 * - Push behavior
 */

#include <functional>

#include "../catch_amalgamated.hpp"

// Mock types for testing without LVGL
struct MockObj {
    int id;
};

// Track calls for verification
static int g_create_call_count = 0;
static int g_push_call_count = 0;
static MockObj* g_last_pushed = nullptr;
static bool g_create_should_fail = false;
static const char* g_last_error_msg = nullptr;

// Mock implementations
namespace {

// Simulates lv_obj_t* for testing
using lv_obj_t = MockObj;

// Mock ui_nav_push_overlay
void ui_nav_push_overlay(lv_obj_t* overlay) {
    g_push_call_count++;
    g_last_pushed = overlay;
}

// Mock toast (just capture error message)
enum class ToastSeverity { ERROR };
void ui_toast_show(ToastSeverity, const char* msg, int) {
    g_last_error_msg = msg;
}

} // namespace

// Inline the template logic for testing (avoids LVGL/spdlog dependencies)
namespace test {

template <typename CreateFunc>
bool lazy_push_overlay(lv_obj_t*& cache, CreateFunc create_func, lv_obj_t* parent,
                       const char* error_msg = "Failed to create overlay") {
    if (!cache && parent) {
        cache = create_func(parent);
        if (!cache) {
            g_last_error_msg = error_msg;
            return false;
        }
    }

    if (cache) {
        ui_nav_push_overlay(cache);
        return true;
    }

    return false;
}

} // namespace test

// Reset all mocks before each test
struct TestFixture {
    TestFixture() {
        g_create_call_count = 0;
        g_push_call_count = 0;
        g_last_pushed = nullptr;
        g_create_should_fail = false;
        g_last_error_msg = nullptr;
    }
};

// =============================================================================
// lazy_push_overlay tests
// =============================================================================

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay creates on first call", "[lazy_overlay]") {
    MockObj created_obj{42};
    lv_obj_t* cache = nullptr;
    MockObj parent{1};

    auto create_fn = [&](lv_obj_t*) -> lv_obj_t* {
        g_create_call_count++;
        return &created_obj;
    };

    bool result = test::lazy_push_overlay(cache, create_fn, &parent);

    CHECK(result == true);
    CHECK(g_create_call_count == 1);
    CHECK(cache == &created_obj);
    CHECK(g_push_call_count == 1);
    CHECK(g_last_pushed == &created_obj);
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay reuses cache on subsequent calls",
                 "[lazy_overlay]") {
    MockObj created_obj{42};
    lv_obj_t* cache = nullptr;
    MockObj parent{1};

    auto create_fn = [&](lv_obj_t*) -> lv_obj_t* {
        g_create_call_count++;
        return &created_obj;
    };

    // First call - creates
    test::lazy_push_overlay(cache, create_fn, &parent);
    CHECK(g_create_call_count == 1);

    // Second call - reuses cache
    bool result = test::lazy_push_overlay(cache, create_fn, &parent);

    CHECK(result == true);
    CHECK(g_create_call_count == 1); // Still 1, not called again
    CHECK(g_push_call_count == 2);   // But push was called twice
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay returns false when creation fails",
                 "[lazy_overlay]") {
    lv_obj_t* cache = nullptr;
    MockObj parent{1};

    auto create_fn = [](lv_obj_t*) -> lv_obj_t* {
        g_create_call_count++;
        return nullptr; // Simulate failure
    };

    bool result = test::lazy_push_overlay(cache, create_fn, &parent, "Test error message");

    CHECK(result == false);
    CHECK(g_create_call_count == 1);
    CHECK(cache == nullptr);
    CHECK(g_push_call_count == 0); // No push on failure
    CHECK(std::string(g_last_error_msg) == "Test error message");
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay does nothing with null parent", "[lazy_overlay]") {
    lv_obj_t* cache = nullptr;

    auto create_fn = [](lv_obj_t*) -> lv_obj_t* {
        g_create_call_count++;
        return nullptr;
    };

    bool result = test::lazy_push_overlay(cache, create_fn, nullptr);

    CHECK(result == false);
    CHECK(g_create_call_count == 0); // Never called
    CHECK(g_push_call_count == 0);
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay pushes existing cache without parent",
                 "[lazy_overlay]") {
    MockObj existing_obj{99};
    lv_obj_t* cache = &existing_obj; // Pre-existing cache

    auto create_fn = [](lv_obj_t*) -> lv_obj_t* {
        g_create_call_count++;
        return nullptr;
    };

    // Even with null parent, should push existing cache
    bool result = test::lazy_push_overlay(cache, create_fn, nullptr);

    CHECK(result == true);
    CHECK(g_create_call_count == 0); // Not called - cache exists
    CHECK(g_push_call_count == 1);
    CHECK(g_last_pushed == &existing_obj);
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay uses default error message", "[lazy_overlay]") {
    lv_obj_t* cache = nullptr;
    MockObj parent{1};

    auto create_fn = [](lv_obj_t*) -> lv_obj_t* { return nullptr; };

    test::lazy_push_overlay(cache, create_fn, &parent);

    CHECK(std::string(g_last_error_msg) == "Failed to create overlay");
}

TEST_CASE_METHOD(TestFixture, "lazy_push_overlay works with lambda capturing state",
                 "[lazy_overlay]") {
    MockObj created_obj{42};
    lv_obj_t* cache = nullptr;
    MockObj parent{1};
    int setup_called = 0;

    // Lambda that captures and modifies state (common pattern in real usage)
    auto create_fn = [&](lv_obj_t* p) -> lv_obj_t* {
        g_create_call_count++;
        // Simulate: create from XML, then setup
        if (p) {
            setup_called++;
            return &created_obj;
        }
        return nullptr;
    };

    bool result = test::lazy_push_overlay(cache, create_fn, &parent);

    CHECK(result == true);
    CHECK(setup_called == 1);
    CHECK(cache == &created_obj);
}
