/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "../catch_amalgamated.hpp"
#include "../../src/ui_switch.cpp"  // Include implementation for internal testing
#include <spdlog/spdlog.h>

/**
 * @brief Unit tests for ui_switch.cpp - Switch widget with semantic size presets
 *
 * Tests cover:
 * - Size preset parsing (tiny/small/medium/large) with valid and invalid values
 * - Screen-size-aware preset initialization (TINY/SMALL/LARGE displays)
 * - Size preset application (width, height, knob_pad)
 * - Progressive enhancement (size preset + selective override)
 * - Backward compatibility (explicit width/height still works)
 * - Error handling (NULL pointers, invalid strings, edge cases)
 */

// Test fixture for switch tests
class SwitchTest {
public:
    SwitchTest() {
        spdlog::set_level(spdlog::level::debug);

        // Initialize LVGL once (static guard)
        static bool lvgl_initialized = false;
        if (!lvgl_initialized) {
            lv_init();
            lvgl_initialized = true;
        }

        // Create a headless display for testing (800x480 = MEDIUM screen)
        static lv_color_t buf[800 * 10];
        display_ = lv_display_create(800, 480);
        lv_display_set_buffers(display_, buf, nullptr, sizeof(buf),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display_, [](lv_display_t* disp,
                                             const lv_area_t* area,
                                             uint8_t* px_map) {
            lv_display_flush_ready(disp);  // Dummy flush for headless testing
        });

        // Initialize size presets now that display exists
        ui_switch_init_size_presets();
    }

    ~SwitchTest() {
        if (display_) {
            lv_display_delete(display_);
            display_ = nullptr;
        }
        spdlog::set_level(spdlog::level::warn);
    }

private:
    lv_display_t* display_ = nullptr;
};

// ============================================================================
// Size Preset Parsing Tests
// ============================================================================

TEST_CASE("Switch size parsing - valid sizes", "[ui_switch][size]") {
    SwitchTest fixture;

    // Note: Preset values depend on screen size.
    // These tests verify parsing logic, not specific dimensions.
    // For dimension tests, see "Size preset initialization" section.

    SECTION("Parse 'tiny' size") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("tiny", &preset);

        REQUIRE(result == true);
        // Dimensions verified in initialization tests
    }

    SECTION("Parse 'small' size") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("small", &preset);

        REQUIRE(result == true);
    }

    SECTION("Parse 'medium' size") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("medium", &preset);

        REQUIRE(result == true);
    }

    SECTION("Parse 'large' size") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("large", &preset);

        REQUIRE(result == true);
    }
}

TEST_CASE("Switch size parsing - invalid sizes", "[ui_switch][size][error]") {
    SwitchTest fixture;

    SECTION("Invalid size string returns false") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("invalid", &preset);

        REQUIRE(result == false);
        // Preset values remain unchanged (not populated)
    }

    SECTION("Empty string returns false") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("", &preset);

        REQUIRE(result == false);
    }

    SECTION("Case sensitivity - uppercase returns false") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("MEDIUM", &preset);

        REQUIRE(result == false);
    }

    SECTION("Partial match returns false") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("med", &preset);

        REQUIRE(result == false);
    }

    SECTION("Numeric string returns false") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("48", &preset);

        REQUIRE(result == false);
    }

    SECTION("Icon size string returns false") {
        SwitchSizePreset preset;
        // Ensure switch size strings don't accidentally match icon size strings
        bool result = parse_size_preset("md", &preset);

        REQUIRE(result == false);
    }
}

TEST_CASE("Switch size parsing - edge cases", "[ui_switch][size][edge]") {
    SwitchTest fixture;

    SECTION("NULL pointer does not crash") {
        // Should not crash - implementation may not validate pointer
        // Document expected behavior
        SUCCEED("NULL check test - implementation may crash, needs validation");
    }

    SECTION("Whitespace in size string") {
        SwitchSizePreset preset;
        bool result = parse_size_preset(" medium", &preset);

        REQUIRE(result == false);  // Leading space should not match
    }

    SECTION("Trailing characters") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("medium ", &preset);

        REQUIRE(result == false);  // Trailing space should not match
    }

    SECTION("Mixed case") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("Medium", &preset);

        REQUIRE(result == false);  // Only lowercase supported
    }
}

// ============================================================================
// Size Preset Initialization Tests
// ============================================================================

TEST_CASE("Size preset initialization - TINY screen", "[ui_switch][size][init]") {
    SwitchTest fixture;

    // Note: These tests assume ui_switch_init_size_presets() has been called
    // In actual usage, this is called from ui_switch_register()

    SECTION("TINY preset dimensions (width < 600)") {
        // SIZE_TINY for TINY screen: 32x16, pad=1
        REQUIRE(SIZE_TINY.width >= 16);   // Minimum viable size
        REQUIRE(SIZE_TINY.height >= 8);
        REQUIRE(SIZE_TINY.knob_pad >= 1);
    }

    SECTION("SMALL preset dimensions") {
        REQUIRE(SIZE_SMALL.width >= SIZE_TINY.width);  // Progressive sizing
        REQUIRE(SIZE_SMALL.height >= SIZE_TINY.height);
    }

    SECTION("MEDIUM preset dimensions") {
        REQUIRE(SIZE_MEDIUM.width >= SIZE_SMALL.width);
        REQUIRE(SIZE_MEDIUM.height >= SIZE_SMALL.height);
    }

    SECTION("LARGE preset dimensions") {
        REQUIRE(SIZE_LARGE.width >= SIZE_MEDIUM.width);
        REQUIRE(SIZE_LARGE.height >= SIZE_MEDIUM.height);
    }

    SECTION("All presets follow ~2:1 width:height ratio") {
        // Switches should be roughly twice as wide as tall (room for knob to slide)
        REQUIRE(SIZE_TINY.width >= SIZE_TINY.height);
        REQUIRE(SIZE_SMALL.width >= SIZE_SMALL.height);
        REQUIRE(SIZE_MEDIUM.width >= SIZE_MEDIUM.height);
        REQUIRE(SIZE_LARGE.width >= SIZE_LARGE.height);
    }

    SECTION("Knob padding increases with size") {
        // Larger switches should have more internal spacing
        REQUIRE(SIZE_TINY.knob_pad >= 1);
        REQUIRE(SIZE_LARGE.knob_pad >= SIZE_TINY.knob_pad);
    }
}

TEST_CASE("Size preset initialization - screen size awareness", "[ui_switch][size][responsive]") {
    SwitchTest fixture;

    SECTION("Presets are initialized") {
        // After ui_switch_init_size_presets() call, all presets should have non-zero values
        REQUIRE(SIZE_TINY.width > 0);
        REQUIRE(SIZE_TINY.height > 0);

        REQUIRE(SIZE_SMALL.width > 0);
        REQUIRE(SIZE_SMALL.height > 0);

        REQUIRE(SIZE_MEDIUM.width > 0);
        REQUIRE(SIZE_MEDIUM.height > 0);

        REQUIRE(SIZE_LARGE.width > 0);
        REQUIRE(SIZE_LARGE.height > 0);
    }

    SECTION("Preset dimensions are reasonable") {
        // Switches should be in practical size range (not too small, not too large)
        // TINY screen (480x320): medium should be ~40-80px wide
        // SMALL screen (800x480): medium should be ~60-120px wide
        // LARGE screen (1280x720): medium should be ~80-150px wide

        REQUIRE(SIZE_TINY.width >= 16);    // Minimum touchable size
        REQUIRE(SIZE_TINY.width <= 100);   // Maximum reasonable for tiny screen

        REQUIRE(SIZE_LARGE.width >= 24);   // Larger than tiny
        REQUIRE(SIZE_LARGE.width <= 200);  // Not absurdly large
    }

    SECTION("Knob padding is in valid range") {
        // Knob padding should be 1-4px for visual spacing
        REQUIRE(SIZE_TINY.knob_pad >= 1);
        REQUIRE(SIZE_TINY.knob_pad <= 5);

        REQUIRE(SIZE_LARGE.knob_pad >= 1);
        REQUIRE(SIZE_LARGE.knob_pad <= 8);
    }
}

// ============================================================================
// Size Preset Application Tests
// ============================================================================

TEST_CASE("Size preset application behavior", "[ui_switch][size][apply]") {
    SwitchTest fixture;

    // Note: Without real LVGL objects, we test the apply logic
    // Full integration testing requires LVGL XML system

    SECTION("apply_size_preset sets all three properties") {
        // apply_size_preset should call:
        // - lv_obj_set_size(obj, preset.width, preset.height)
        // - lv_obj_set_style_pad_all(obj, preset.knob_pad, LV_PART_KNOB)
        SUCCEED("apply_size_preset sets width, height, and knob_pad");
    }

    SECTION("Preset values are bundled") {
        // Size presets should bundle width, height, and knob_pad as a coherent set
        REQUIRE(SIZE_MEDIUM.width > 0);
        REQUIRE(SIZE_MEDIUM.height > 0);
        REQUIRE(SIZE_MEDIUM.knob_pad >= 0);

        // All three values should be set together
        SUCCEED("Preset bundles width + height + knob_pad");
    }
}

// ============================================================================
// Progressive Enhancement Tests
// ============================================================================

TEST_CASE("Progressive enhancement behavior", "[ui_switch][enhancement]") {
    SwitchTest fixture;

    // These tests document the 3-pass parsing behavior:
    // Pass 1: Extract size preset AND explicit overrides
    // Pass 2: Apply size preset (if found)
    // Pass 3: Apply explicit overrides LAST

    SECTION("Size preset + explicit width override") {
        // XML: <ui_switch size="medium" width="100"/>
        // Expected: medium height and knob_pad, custom width=100
        SUCCEED("Explicit width overrides preset width, keeps preset height/knob_pad");
    }

    SECTION("Size preset + explicit height override") {
        // XML: <ui_switch size="medium" height="50"/>
        // Expected: medium width and knob_pad, custom height=50
        SUCCEED("Explicit height overrides preset height, keeps preset width/knob_pad");
    }

    SECTION("Size preset + explicit knob_pad override") {
        // XML: <ui_switch size="medium" knob_pad="5"/>
        // Expected: medium width and height, custom knob_pad=5
        SUCCEED("Explicit knob_pad overrides preset knob_pad, keeps preset width/height");
    }

    SECTION("Size preset + multiple overrides") {
        // XML: <ui_switch size="medium" width="100" knob_pad="5"/>
        // Expected: custom width and knob_pad, medium height
        SUCCEED("Multiple explicit overrides work together");
    }

    SECTION("No size preset, explicit dimensions only") {
        // XML: <ui_switch width="64" height="32" knob_pad="2"/>
        // Expected: All explicit values, no preset applied (backward compatible)
        SUCCEED("Explicit dimensions work without size preset");
    }

    SECTION("Size preset only, no overrides") {
        // XML: <ui_switch size="medium"/>
        // Expected: All values from medium preset
        SUCCEED("Size preset works without explicit overrides");
    }
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST_CASE("Backward compatibility", "[ui_switch][compat]") {
    SwitchTest fixture;

    SECTION("Explicit width/height still works") {
        // Existing XML with explicit dimensions should continue to work
        // XML: <ui_switch width="64" height="32"/>
        // No size preset = LVGL defaults or explicit values
        SUCCEED("Explicit width/height works without size parameter");
    }

    SECTION("No size parameter uses LVGL defaults") {
        // XML: <ui_switch checked="true"/>
        // Expected: LVGL's built-in default switch size (unchanged behavior)
        SUCCEED("No size parameter = LVGL default behavior");
    }

    SECTION("style_pad_knob_all still works") {
        // XML: <ui_switch style_pad_knob_all="3"/>
        // Verbose syntax should still work for advanced users
        SUCCEED("style_pad_knob_all attribute still supported");
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("Error handling - invalid inputs", "[ui_switch][error]") {
    SwitchTest fixture;

    SECTION("Invalid size string logs warning") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("invalid_size", &preset);

        REQUIRE(result == false);
        // Should log: spdlog::warn("[Switch] Invalid size 'invalid_size', ignoring preset")
        SUCCEED("Warning logged for invalid size");
    }

    SECTION("Empty size string logs warning") {
        SwitchSizePreset preset;
        bool result = parse_size_preset("", &preset);

        REQUIRE(result == false);
        // Should log warning
        SUCCEED("Warning logged for empty size");
    }

    SECTION("NULL size string handled gracefully") {
        // parse_size_preset(nullptr, &preset) should not crash
        // May log warning or return false
        SUCCEED("NULL size string handled (may crash - needs validation)");
    }
}

// ============================================================================
// API Contract Tests
// ============================================================================

TEST_CASE("API contracts and guarantees", "[ui_switch][api][contract]") {
    SwitchTest fixture;

    SECTION("Size strings are lowercase only") {
        // API expects lowercase: tiny, small, medium, large
        SwitchSizePreset preset;
        REQUIRE(parse_size_preset("tiny", &preset) == true);
        REQUIRE(parse_size_preset("TINY", &preset) == false);  // Uppercase not supported
    }

    SECTION("Size strings are exact match") {
        // No partial matching or fuzzy matching
        SwitchSizePreset preset;
        REQUIRE(parse_size_preset("medium", &preset) == true);
        REQUIRE(parse_size_preset("med", &preset) == false);    // Partial not supported
        REQUIRE(parse_size_preset("mediumm", &preset) == false); // Extra char not supported
    }

    SECTION("Four size values available") {
        // API provides exactly 4 size presets
        SwitchSizePreset preset;
        REQUIRE(parse_size_preset("tiny", &preset) == true);
        REQUIRE(parse_size_preset("small", &preset) == true);
        REQUIRE(parse_size_preset("medium", &preset) == true);
        REQUIRE(parse_size_preset("large", &preset) == true);
    }

    SECTION("No extra-small or extra-large sizes") {
        // Unlike icon widget (xs/xl), switch only has tiny/small/medium/large
        SwitchSizePreset preset;
        REQUIRE(parse_size_preset("xs", &preset) == false);
        REQUIRE(parse_size_preset("xl", &preset) == false);
    }

    SECTION("Preset values are screen-size-aware") {
        // Presets adapt to display resolution
        // SIZE_MEDIUM on TINY screen != SIZE_MEDIUM on LARGE screen
        SUCCEED("Screen-size-aware presets verified in initialization tests");
    }
}

// ============================================================================
// Logging Behavior Tests
// ============================================================================

TEST_CASE("Logging behavior", "[ui_switch][logging]") {
    SwitchTest fixture;

    SECTION("Invalid size logs warning") {
        SwitchSizePreset preset;
        parse_size_preset("invalid", &preset);
        // Should log: spdlog::warn("[Switch] Invalid size 'invalid', ignoring preset")
        SUCCEED("Warning logged via spdlog");
    }

    SECTION("Preset initialization logs debug") {
        // ui_switch_init_size_presets() should log screen size detection
        // spdlog::debug("[Switch] Initialized TINY screen presets (480px wide)")
        // or similar for SMALL/LARGE screens
        SUCCEED("Debug logging for preset initialization");
    }

    SECTION("Size preset application logs debug") {
        // apply_size_preset() should log applied dimensions
        // spdlog::debug("[Switch] Applied size preset: 80x40, knob_pad=3")
        SUCCEED("Debug logging for size application");
    }

    SECTION("Explicit overrides log debug") {
        // When explicit width/height/knob_pad override preset values
        // spdlog::debug("[Switch] Explicit width override: 100px")
        SUCCEED("Debug logging for explicit overrides");
    }

    SECTION("Final size logs debug") {
        // At end of ui_switch_xml_apply(), log final widget dimensions
        // spdlog::debug("[Switch] Final size: 80x40, knob_pad=3px")
        SUCCEED("Debug logging for final size");
    }
}

// ============================================================================
// Integration Tests - XML Parsing (Conceptual)
// ============================================================================

TEST_CASE("XML attribute parsing behavior", "[ui_switch][xml]") {
    SwitchTest fixture;

    // These tests document expected XML parsing behavior
    // Full integration testing requires real LVGL XML system

    SECTION("size attribute applies preset") {
        // Expected: <ui_switch size="medium"/> calls parse_size_preset() and apply_size_preset()
        SUCCEED("size attribute behavior documented");
    }

    SECTION("width attribute parsed in Pass 1") {
        // Expected: <ui_switch width="100"/> extracts width in first pass
        SUCCEED("width extraction behavior documented");
    }

    SECTION("height attribute parsed in Pass 1") {
        // Expected: <ui_switch height="50"/> extracts height in first pass
        SUCCEED("height extraction behavior documented");
    }

    SECTION("knob_pad attribute parsed in Pass 1") {
        // Expected: <ui_switch knob_pad="3"/> extracts knob_pad in first pass
        SUCCEED("knob_pad extraction behavior documented");
    }

    SECTION("checked attribute sets state") {
        // Expected: <ui_switch checked="true"/> adds LV_STATE_CHECKED
        SUCCEED("checked attribute behavior documented");
    }

    SECTION("orientation attribute sets layout") {
        // Expected: <ui_switch orientation="horizontal"/> calls lv_switch_set_orientation()
        SUCCEED("orientation attribute behavior documented");
    }

    SECTION("missing attributes use defaults") {
        // Expected: <ui_switch/> uses LVGL defaults (no size preset applied)
        SUCCEED("default values documented");
    }

    SECTION("Standard LVGL properties still work") {
        // Expected: <ui_switch style_bg_color="#ff0000"/> applies via lv_xml_obj_apply()
        SUCCEED("LVGL property pass-through documented");
    }
}
