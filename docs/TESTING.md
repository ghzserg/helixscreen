# Testing Infrastructure

**Status:** Active
**Last Updated:** 2025-11-20

---

## Overview

HelixScreen uses a multi-layered testing strategy with Catch2 v3 as the primary test framework. Tests are organized into unit tests, integration tests, and experimental test binaries for rapid prototyping.

## Quick Start

```bash
# Build tests (does not run)
make test

# Run all unit tests
make test-run

# Run fast tests only (skip slow tests)
make test-fast

# Run integration tests (with mocks)
make test-integration

# Build specific test binary
cd experimental && make test_multicolor_gcode
```

## Test Organization

```
tests/
├── catch_amalgamated.hpp       # Catch2 v3 amalgamated header
├── catch_amalgamated.cpp       # Catch2 v3 implementation
├── test_main.cpp               # Main test runner entry point
├── ui_test_utils.cpp           # UI testing utilities
├── ui_test_utils.h
├── unit/                       # Unit tests (use real LVGL)
│   ├── test_config.cpp
│   ├── test_gcode_parser.cpp
│   ├── test_multicolor_gcode.cpp
│   ├── test_moonraker_*.cpp
│   └── ...
├── integration/                # Integration tests (use mocks)
│   └── test_mock_example.cpp
└── mocks/                      # Mock implementations
    ├── mock_lvgl.cpp
    ├── mock_moonraker_client.cpp
    └── ...

experimental/
└── src/
    ├── test_gcode_geometry.cpp     # G-code geometry tests
    ├── test_gcode_analysis.cpp     # G-code analysis
    ├── test_multicolor_gcode.cpp   # Multi-color rendering
    └── ...
```

## Framework: Catch2 v3

HelixScreen uses **Catch2 v3** (amalgamated build) for all tests.

### Including Catch2

```cpp
// tests/unit/test_example.cpp
#include "your_module.h"
#include "../catch_amalgamated.hpp"  // Catch2 v3

using Catch::Approx;  // For floating-point comparisons

TEST_CASE("Module - Feature description", "[module][feature]") {
    SECTION("Test scenario") {
        REQUIRE(result == expected);
    }
}
```

### Common Patterns

**Basic assertions:**
```cpp
REQUIRE(condition);                // Must be true
CHECK(condition);                  // Continue on failure
REQUIRE_FALSE(condition);          // Must be false
```

**Floating-point comparisons:**
```cpp
using Catch::Approx;

REQUIRE(value == Approx(3.14159).epsilon(0.01));
REQUIRE(layer.z_height == Approx(0.2f));
```

**Test organization:**
```cpp
TEST_CASE("Component - Feature", "[component][feature][optional-tag]") {
    // Setup common to all sections

    SECTION("First scenario") {
        // Test first scenario
    }

    SECTION("Second scenario") {
        // Test second scenario
    }
}
```

**Skipping tests:**
```cpp
if (!file_exists("test_data.gcode")) {
    SKIP("Test data not found");
}
```

**Info logging:**
```cpp
INFO("Parsed " << line_count << " lines");
REQUIRE(result.layers.size() > 0);
```

## Unit Tests

Unit tests use **real LVGL** and test individual components in isolation.

### Writing Unit Tests

**Location:** `tests/unit/test_<module>.cpp`

**Example:** Multi-color G-code parser test

```cpp
// tests/unit/test_multicolor_gcode.cpp
#include "gcode_parser.h"
#include "../catch_amalgamated.hpp"

using namespace gcode;

TEST_CASE("MultiColor - Parse extruder_colour", "[gcode][multicolor][parser]") {
    GCodeParser parser;

    SECTION("Parse 4-color OrcaSlicer format") {
        parser.parse_line("; extruder_colour = #ED1C24;#00C1AE;#F4E2C1;#000000");

        const auto& palette = parser.get_tool_color_palette();

        REQUIRE(palette.size() == 4);
        REQUIRE(palette[0] == "#ED1C24");  // Red
        REQUIRE(palette[1] == "#00C1AE");  // Teal
        REQUIRE(palette[2] == "#F4E2C1");  // Beige
        REQUIRE(palette[3] == "#000000");  // Black
    }
}
```

### Test Tags

Use tags to organize and filter tests:

- `[gcode]` - G-code parsing/rendering
- `[parser]` - Parser-specific tests
- `[geometry]` - Geometry building
- `[multicolor]` - Multi-color features
- `[integration]` - End-to-end tests
- `[file]` - Tests requiring external files
- `[moonraker]` - Moonraker API tests
- `[wifi]` - WiFi/networking tests

Run specific tags:
```bash
./build/bin/run_tests "[multicolor]"
./build/bin/run_tests "[gcode][parser]"
```

## Integration Tests

Integration tests use **mocked dependencies** instead of real LVGL.

### Writing Integration Tests

**Location:** `tests/integration/test_<feature>.cpp`

Integration tests use mocks from `tests/mocks/` to simulate UI components,
Moonraker clients, and other external dependencies without requiring real
hardware or network connections.

**Example:**
```cpp
#include "mock_moonraker_client.h"
#include "../catch_amalgamated.hpp"

TEST_CASE("Moonraker - Connection handling", "[moonraker][integration]") {
    MockMoonrakerClient client;
    client.set_mock_response("printer.info", R"({"state": "ready"})");

    auto result = client.send_request("printer.info");
    REQUIRE(result.success);
}
```

## Experimental Tests

Experimental tests are standalone binaries for rapid prototyping and analysis.

### Building Experimental Tests

**Location:** `experimental/src/test_<feature>.cpp`

```bash
cd experimental
make                              # Build all experimental tests
make test_multicolor_gcode        # Build specific test
./bin/test_multicolor_gcode       # Run test
```

### Example: G-code Geometry Analysis

```cpp
// experimental/src/test_gcode_geometry.cpp
#include "gcode_geometry_builder.h"
#include <iostream>

int main() {
    // Load G-code
    auto gcode = load_gcode_file("model.gcode");

    // Build geometry
    GeometryBuilder builder;
    auto geometry = builder.build(gcode);

    // Analyze
    std::cout << "Vertices: " << geometry.vertices.size() << "\n";
    std::cout << "Triangles: " << geometry.indices.size() << "\n";

    return 0;
}
```

## Test Data

### G-code Test Files

Test G-code files are located in `assets/gcode/`:

- `assets/gcode/OrcaCube_ABS_Multicolor.gcode` - 4-color multi-extruder test (5.8MB, 51 tool changes)
- `assets/gcode/OrcaCube AD5M.gcode` - Single-color test
- Additional test files as needed

### Using Test Files in Tests

```cpp
TEST_CASE("MultiColor - Real file parsing", "[gcode][multicolor][file]") {
    const char* filename = "assets/gcode/OrcaCube_ABS_Multicolor.gcode";

    std::ifstream file(filename);
    if (!file.is_open()) {
        SKIP("Test file not found: " << filename);
    }

    GCodeParser parser;
    std::string line;
    while (std::getline(file, line)) {
        parser.parse_line(line);
    }

    auto result = parser.finalize();

    REQUIRE(result.tool_color_palette.size() == 4);
    REQUIRE(result.total_segments > 0);
}
```

## Build System Integration

### Makefile Targets

```bash
make test              # Build and run unit tests
make test-integration  # Build and run integration tests
make test-cards        # Test dynamic card instantiation
```

### Test Binary Locations

- Unit tests: `build/bin/run_tests`
- Integration tests: `build/bin/run_integration_tests`
- Experimental: `experimental/bin/test_<name>`

### Adding New Tests

1. **Create test file:**
   ```bash
   # Unit test
   touch tests/unit/test_new_feature.cpp

   # Integration test
   touch tests/integration/test_new_feature.cpp
   ```

2. **Write test using Catch2:**
   ```cpp
   #include "your_module.h"
   #include "../catch_amalgamated.hpp"

   TEST_CASE("NewFeature - Description", "[module][feature]") {
       REQUIRE(true);
   }
   ```

3. **Build and run:**
   ```bash
   make test
   ```

The Makefile automatically discovers test files in `tests/unit/` and `tests/integration/`.

## UI Testing Utilities

HelixScreen provides UI testing utilities in `tests/ui_test_utils.h`:

```cpp
#include "../ui_test_utils.h"

// Initialize LVGL for UI tests
void setup_lvgl_for_testing();

// Create mock display
lv_display_t* create_test_display(int width, int height);

// Simulate user interactions
void simulate_click(lv_obj_t* obj);
void simulate_swipe(lv_obj_t* obj, lv_dir_t direction);
```

**Example:**
```cpp
TEST_CASE("UI - Button click", "[ui]") {
    setup_lvgl_for_testing();

    lv_obj_t* btn = lv_button_create(lv_screen_active());
    bool clicked = false;

    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        bool* flag = (bool*)lv_event_get_user_data(e);
        *flag = true;
    }, LV_EVENT_CLICKED, &clicked);

    simulate_click(btn);

    REQUIRE(clicked == true);
}
```

## Mocking Infrastructure

### Available Mocks

- **MockMoonrakerClient:** Simulates Moonraker API responses
- **MockLVGL:** Minimal LVGL stubs for integration tests
- **MockPrintFiles:** Simulates filesystem operations

### Creating New Mocks

**Location:** `tests/mocks/mock_<component>.cpp`

```cpp
// tests/mocks/mock_wifi.h
#pragma once

class MockWiFiManager {
public:
    void connect(const std::string& ssid, const std::string& password);
    bool is_connected() const { return connected_; }

    // Test control
    void set_should_fail(bool fail) { should_fail_ = fail; }

private:
    bool connected_ = false;
    bool should_fail_ = false;
};
```

## Best Practices

### 1. Test Isolation
- Each test should be independent
- Use `SECTION` for related scenarios
- Clean up resources in destructors

### 2. Descriptive Names
```cpp
// Good
TEST_CASE("GCodeParser - Parse multi-color metadata", "[gcode][parser]")

// Bad
TEST_CASE("Test 1", "[test]")
```

### 3. Test One Thing
```cpp
// Good - focused test
TEST_CASE("Parser - Tool changes", "[parser]") {
    // Test only tool change detection
}

// Bad - testing multiple unrelated features
TEST_CASE("Parser - Everything", "[parser]") {
    // Tests colors, layers, moves, objects...
}
```

### 4. Use Meaningful Assertions
```cpp
// Good
REQUIRE(palette.size() == 4);  // Expect 4-color palette

// Bad
REQUIRE(palette.size() > 0);  // Any size is okay?
```

### 5. Document Test Intent
```cpp
TEST_CASE("MultiColor - Backward compatibility", "[multicolor][compatibility]") {
    // Verify single-color files still work without palette metadata

    SECTION("No color metadata") {
        // Test should pass even without any color info
    }
}
```

## Debugging Tests

### Run Specific Tests
```bash
# Run all multicolor tests
./build/bin/run_tests "[multicolor]"

# Run specific test case
./build/bin/run_tests "MultiColor - Parse extruder_colour"

# List all tests
./build/bin/run_tests --list-tests
```

### Verbose Output
```bash
# Show successful assertions
./build/bin/run_tests -s

# Show all output
./build/bin/run_tests -s -v high
```

### Debugging in IDE

Set breakpoints in test code and run:
```bash
lldb build/bin/run_tests
(lldb) run "[multicolor]"
```

## Continuous Integration

Tests run automatically on push via GitHub Actions (see `.github/workflows/test.yml`).

CI runs:
1. Unit tests (`make test`)
2. Integration tests (`make test-integration`)
3. Code quality checks

## Coverage

Generate code coverage reports:
```bash
# TODO: Add coverage target
make coverage
```

## LVGL Observer Testing Gotcha

**CRITICAL:** `lv_subject_add_observer()` immediately fires the callback with the current value (LVGL 9.4 behavior).

Tests must account for this auto-notification:

```cpp
lv_subject_add_observer(subject, callback, &count);
REQUIRE(count == 1);  // Fired immediately!

state.set_value(new_value);
REQUIRE(count == 2);  // Fired again on change
```

**Source:** `lib/lvgl/src/core/lv_observer.c:459` - `observer->cb(observer, subject);`

**Reference:** `tests/unit/test_printer_state.cpp` observer tests

---

## Common Issues

### Issue: "Catch2 header not found"
**Solution:** Use `#include "../catch_amalgamated.hpp"` not `<catch2/...>`

### Issue: "Approx not found"
**Solution:** Add `using Catch::Approx;` after includes

### Issue: "Test binary won't link"
**Solution:** Check that required .o files are included in `Makefile` test link command

### Issue: "LVGL functions undefined in integration tests"
**Solution:** Integration tests should use mocks, not real LVGL. Move to unit tests if LVGL needed.

## Future Enhancements

- [ ] Add performance benchmarks
- [ ] Integrate coverage reporting
- [ ] Add mutation testing
- [ ] Create UI snapshot testing
- [ ] Add stress tests for large G-code files

## Related Documentation

- **ARCHITECTURE.md:** System design and component relationships
- **BUILD_SYSTEM.md:** Build configuration and compilation
- **CONTRIBUTING.md:** Code standards and contribution workflow
- **docs/MOCK_INFRASTRUCTURE_SUMMARY.md:** Mock system details

---

**Questions?** See `tests/README.md` for additional test examples.
