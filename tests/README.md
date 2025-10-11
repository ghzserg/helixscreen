# GuppyScreen UI Prototype - Testing Framework

This directory contains the testing infrastructure for the LVGL 9 UI prototype.

## Directory Structure

```
tests/
├── framework/           # Test framework dependencies
│   └── catch.hpp       # Catch2 v2.x single-header (vendored)
├── unit/               # Unit tests (no UI rendering required)
│   ├── test_main.cpp   # Test runner entry point
│   └── test_*.cpp      # Individual test files
├── integration/        # Integration tests (full UI simulation)
│   └── test-navigation.sh  # Manual navigation testing script
└── README.md           # This file
```

## Test Framework: Catch2 v2.x

We use **Catch2 v2.x** as a single-header vendored dependency. This is the recommended approach for Catch2 v2.x - no submodule or external build system required.

**Why Catch2 v2.x single-header?**
- Simple: Just `#include "../framework/catch.hpp"` in test files
- Lightweight: 642KB, no build complexity
- Stable: v2.x is mature and won't require frequent updates
- Standard practice: Most C++ projects vendor this header

## Running Tests

### Quick Start

```bash
# Build and run all unit tests
make test

# Build tests without running
make build/bin/run_tests

# Run tests manually
./build/bin/run_tests

# Run specific test cases
./build/bin/run_tests "[navigation]"

# List all available test cases
./build/bin/run_tests --list-tests

# Show verbose output
./build/bin/run_tests -s
```

### Clean Build

```bash
make clean
make test
```

## Writing Unit Tests

### Basic Test Structure

Create a new file in `tests/unit/test_<feature>.cpp`:

```cpp
#include "../framework/catch.hpp"
#include "../../include/your_header.h"

TEST_CASE("Feature description", "[tag]") {
    SECTION("Specific scenario") {
        // Arrange
        int expected = 42;

        // Act
        int result = your_function();

        // Assert
        REQUIRE(result == expected);
    }
}
```

### Using Test Fixtures

For tests requiring setup/teardown:

```cpp
class MyTestFixture {
public:
    MyTestFixture() {
        // Setup code
        lv_init();
    }

    ~MyTestFixture() {
        // Teardown code (LVGL handles its own cleanup)
    }
};

TEST_CASE_METHOD(MyTestFixture, "Test with fixture", "[tag]") {
    // Test code with access to fixture members
}
```

### Testing LVGL Components

Navigation tests demonstrate LVGL testing patterns:

```cpp
class NavigationTestFixture {
public:
    NavigationTestFixture() {
        // Initialize LVGL
        lv_init();

        // Create headless display for testing
        lv_display_t* disp = lv_display_create(800, 480);
        static lv_color_t buf[800 * 10];
        lv_display_set_buffers(disp, buf, NULL, sizeof(buf),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);

        // Initialize component under test
        ui_nav_init();
    }
};

TEST_CASE_METHOD(NavigationTestFixture, "Panel switching", "[navigation]") {
    ui_nav_set_active(UI_PANEL_CONTROLS);
    REQUIRE(ui_nav_get_active() == UI_PANEL_CONTROLS);
}
```

**Key Points:**
- LVGL must be initialized with `lv_init()`
- Create a headless display for testing (no SDL window needed)
- Static buffers ensure they persist for display lifetime
- LVGL handles cleanup automatically

### Catch2 Assertions

```cpp
// Basic comparisons
REQUIRE(value == expected);
REQUIRE(value != unexpected);
REQUIRE(value < max);

// Boolean checks
REQUIRE(is_valid);
REQUIRE_FALSE(is_invalid);

// Floating point (with epsilon)
REQUIRE(value == Approx(3.14).epsilon(0.01));

// Exceptions
REQUIRE_THROWS(dangerous_function());
REQUIRE_NOTHROW(safe_function());

// String matching
REQUIRE_THAT(str, Catch::Contains("substring"));
REQUIRE_THAT(str, Catch::StartsWith("prefix"));
```

### Test Organization

**Tags** help organize and filter tests:

```cpp
TEST_CASE("Fast computation", "[unit][math][fast]") { }
TEST_CASE("Slow rendering", "[ui][rendering][slow]") { }

// Run only fast tests:
./build/bin/run_tests "[fast]"

// Run all UI tests:
./build/bin/run_tests "[ui]"

// Exclude slow tests:
./build/bin/run_tests "~[slow]"
```

**Sections** group related assertions:

```cpp
TEST_CASE("Navigation system", "[navigation]") {
    SECTION("Default state") {
        REQUIRE(ui_nav_get_active() == UI_PANEL_HOME);
    }

    SECTION("Panel switching") {
        ui_nav_set_active(UI_PANEL_CONTROLS);
        REQUIRE(ui_nav_get_active() == UI_PANEL_CONTROLS);
    }
}
```

Each `SECTION` runs independently with a fresh fixture.

## Integration Tests

### Manual UI Testing

The `integration/test-navigation.sh` script provides manual integration testing:

```bash
# Run from project root
./tests/integration/test-navigation.sh

# Or directly
cd tests/integration && ./test-navigation.sh
```

This script:
1. Builds the UI prototype
2. Launches the app for 10 seconds
3. Captures console output for navigation events
4. Takes a screenshot
5. Reports results

**Manual test checklist:**
- [ ] Click each navigation icon (Home, Controls, Filament, Settings, Advanced)
- [ ] Verify active icon turns red, inactive icons are white
- [ ] Verify correct panel content displays
- [ ] Press 'S' to capture screenshots
- [ ] Verify no click event errors in console

### Writing Integration Tests

Future integration tests should:
- Test full XML component rendering
- Verify Subject-Observer data binding
- Test event handling (clicks, gestures)
- Validate screenshot output matches expected UI

## Testing Best Practices

### What to Test

**Unit tests:**
- Pure functions (no LVGL dependencies)
- State management logic (navigation, data models)
- Subject-Observer bindings
- Utility functions

**Integration tests:**
- Full UI rendering from XML
- User interactions (clicks, touch events)
- Panel transitions and animations
- Visual regression (screenshot comparison)

### What NOT to Test

- LVGL library internals (already tested by LVGL)
- SDL2 driver functionality
- Operating system behavior

### Test-Driven Development

1. **Red** - Write failing test first:
   ```cpp
   TEST_CASE("New feature") {
       REQUIRE(new_feature() == expected_result);
   }
   ```

2. **Green** - Implement minimal code to pass:
   ```cpp
   int new_feature() {
       return expected_result;
   }
   ```

3. **Refactor** - Clean up with tests passing

### Continuous Testing

```bash
# Watch for changes and rebuild (using entr or similar)
ls src/*.cpp include/*.h tests/unit/*.cpp | entr -c make test

# Or manually during development
make && make test
```

## Common Testing Patterns

### Testing Subject Updates

```cpp
TEST_CASE("Subject reactivity", "[subjects]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    lv_subject_set_int(&subject, 42);
    REQUIRE(lv_subject_get_int(&subject) == 42);
}
```

### Testing Panel State

```cpp
TEST_CASE("Panel visibility", "[panels]") {
    lv_obj_t* panel = lv_obj_create(lv_screen_active());
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

    REQUIRE(lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));

    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    REQUIRE_FALSE(lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
}
```

### Testing Event Handlers

```cpp
static bool event_triggered = false;

void test_event_cb(lv_event_t* e) {
    event_triggered = true;
}

TEST_CASE("Event handling", "[events]") {
    event_triggered = false;
    lv_obj_t* btn = lv_button_create(lv_screen_active());
    lv_obj_add_event_cb(btn, test_event_cb, LV_EVENT_CLICKED, NULL);

    // Simulate click
    lv_event_send(btn, LV_EVENT_CLICKED, NULL);

    REQUIRE(event_triggered);
}
```

## Debugging Failed Tests

### Verbose Output

```bash
# Show all test details
./build/bin/run_tests -s

# Show successful assertions too
./build/bin/run_tests -s --success
```

### Running Single Test

```bash
# By test name
./build/bin/run_tests "Navigation initialization"

# By section name
./build/bin/run_tests "Default active panel is HOME"

# By tag
./build/bin/run_tests "[navigation]"
```

### Adding Debug Output

```cpp
TEST_CASE("Debug example", "[debug]") {
    int value = compute_value();

    // Use INFO for context (only shown on failure)
    INFO("Computed value: " << value);
    INFO("Expected range: 0-100");

    REQUIRE(value >= 0);
    REQUIRE(value <= 100);
}
```

## Extending the Test Suite

### Adding a New Test File

1. Create `tests/unit/test_<feature>.cpp`
2. Include Catch2: `#include "../framework/catch.hpp"`
3. Include headers under test
4. Write test cases with tags
5. Run `make test` - Makefile auto-detects new files

### Adding Integration Tests

1. Create script in `tests/integration/`
2. Make executable: `chmod +x tests/integration/test-*.sh`
3. Document expected behavior in script comments
4. Update this README with usage instructions

### Updating Catch2

If you need to update Catch2 v2.x (rare):

```bash
# Download latest v2.x release
curl -L -o tests/framework/catch.hpp \
  https://raw.githubusercontent.com/catchorg/Catch2/v2.x/single_include/catch2/catch.hpp

# Rebuild tests
make clean && make test
```

**Note:** Catch2 v3.x is a breaking change - stick with v2.x unless you have a compelling reason to upgrade.

## Resources

- **Catch2 Documentation:** https://github.com/catchorg/Catch2/blob/v2.x/docs/Readme.md
- **LVGL Testing Guide:** https://docs.lvgl.io/master/others/testing.html
- **Test-Driven Development:** https://martinfowler.com/bliki/TestDrivenDevelopment.html

## Troubleshooting

### "undefined reference to lv_*" errors

Make sure test target includes LVGL objects:
```makefile
$(TEST_BIN): $(TEST_OBJS) $(LVGL_OBJS) $(OBJ_DIR)/ui_nav.o
```

### "catch.hpp: No such file or directory"

Ensure framework directory is in include path:
```makefile
$(CXX) $(CXXFLAGS) -I$(TEST_FRAMEWORK_DIR) $(INCLUDES) ...
```

### Tests pass but feature broken in UI

Unit tests may not catch integration issues. Add integration test with full XML rendering.

### Segfault in tests

Ensure LVGL is initialized before creating objects:
```cpp
lv_init();  // MUST be first
lv_display_create(800, 480);  // Create display
// Now create objects
```

---

**Happy Testing!** 🧪
