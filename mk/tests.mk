# Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen UI Prototype - Test Module
# Handles all test compilation and execution targets

# ============================================================================
# Test Dependency System - AUTOMATIC DISCOVERY
# ============================================================================
# Instead of manually listing every .o file (error-prone, causes CI failures),
# we use automatic discovery: include ALL app objects except a small exclude list.
#
# Benefits:
# - New source files are automatically included in tests
# - No more "undefined symbol" CI failures from forgetting to add files
# - Exclude list is much smaller and easier to maintain
#
# What we keep:
# - TEST_CORE_DEPS: Test infrastructure (Catch2, test utilities)
# - TEST_LVGL_DEPS: LVGL library objects (from submodule)
# - TEST_PLATFORM_DEPS: Platform-specific (wpa_supplicant on Linux)
# - FONT_OBJS, OBJCPP_OBJS: Assets and platform code
#
# What we replace with automatic discovery:
# - All the manual TEST_*_DEPS groups → single TEST_APP_OBJS

# Core test infrastructure (always required)
TEST_CORE_DEPS := $(TEST_MAIN_OBJ) $(CATCH2_OBJ) $(UI_TEST_UTILS_OBJ) $(LVGL_TEST_FIXTURE_OBJ) $(TEST_FIXTURES_OBJ) $(TEST_OBJS)

# LVGL + Graphics stack (required for all UI tests)
TEST_LVGL_DEPS := $(LVGL_OBJS) $(THORVG_OBJS)

# Platform-specific dependencies (Linux wpa_supplicant, macOS frameworks via LDFLAGS)
TEST_PLATFORM_DEPS := $(WPA_DEPS)

# ============================================================================
# AUTOMATIC APP OBJECT DISCOVERY
# ============================================================================
# Include ALL application objects except those that conflict with tests.
# This replaces ~150 lines of manual object listings that needed constant updates.
#
# Exclusions (add files here ONLY if they cause linker conflicts):
#
# Group 1: App entry point and globals
# - main.o: Has main() function, tests have their own entry point
# - app_globals.o: Contains global subjects/state, ui_test_utils.o provides stubs
# - cli_args.o: References globals from app_globals.o, not needed for tests
#
# Group 2: Files where ui_test_utils.o provides stub implementations
# - ui_notification.o: Needs get_notification_subject() from app_globals.o
# - ui_toast.o, ui_toast_manager.o: ui_test_utils.o provides stub toast functions
# - ui_status_bar_manager.o: ui_test_utils.o stubs ui_status_bar_set_backdrop_visible()
# - ui_text_input.o: ui_test_utils.o stubs ui_text_input_get_keyboard_hint()
# - ui_emergency_stop.o: ui_test_utils.o stubs EmergencyStopOverlay
#
# Group 3: Test-specific conflicts
# - ui_switch.o: test_ui_switch.cpp includes the .cpp directly for unit testing
#
# Everything else is automatically included - new files just work!

TEST_APP_OBJS := $(filter-out \
    $(OBJ_DIR)/main.o \
    $(OBJ_DIR)/app_globals.o \
    $(OBJ_DIR)/cli_args.o \
    $(OBJ_DIR)/ui_notification.o \
    $(OBJ_DIR)/ui_toast.o \
    $(OBJ_DIR)/ui_toast_manager.o \
    $(OBJ_DIR)/ui_status_bar_manager.o \
    $(OBJ_DIR)/ui_text_input.o \
    $(OBJ_DIR)/ui_emergency_stop.o \
    $(OBJ_DIR)/ui_switch.o \
    ,$(APP_OBJS) $(APP_C_OBJS))

# ============================================================================
# Test Targets
# ============================================================================

# Clean test artifacts
clean-tests:
	$(ECHO) "$(YELLOW)Cleaning test artifacts...$(RESET)"
	$(Q)rm -f $(TEST_BIN) $(TEST_MAIN_OBJ) $(CATCH2_OBJ) $(UI_TEST_UTILS_OBJ) $(LVGL_TEST_FIXTURE_OBJ) $(TEST_FIXTURES_OBJ) $(TEST_OBJS)
	$(ECHO) "$(GREEN)✓ Test artifacts cleaned$(RESET)"

# Build tests in parallel
test-build:
	$(ECHO) "$(CYAN)$(BOLD)Building tests (use -j for parallel builds)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(MAKE) $(TEST_BIN) && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Tests built in $${DURATION}s$(RESET)"

# ============================================================================
# Main Test Targets
# ============================================================================
# IMPORTANT: The ~[.] filter excludes tests with tags starting with '.'
# including: [.ui_integration], [.disabled], [.benchmark], [.slow], etc.
# This is Catch2's hidden test convention.

# Build tests only (does not run)
# Use 'make test-run' to actually execute the tests
test: $(TEST_BIN)
	$(ECHO) "$(GREEN)✓ Test binary ready: $(TEST_BIN)$(RESET)"
	$(ECHO) "$(CYAN)Run tests with: make test-run$(RESET)"

# Run all non-hidden unit tests
test-run: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running unit tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "~[.]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ All tests passed in $${DURATION}s$(RESET)"

# Alias that rebuilds and runs tests (useful for development)
tests: test-run

# ============================================================================
# Convenience Test Targets - Run tests by component
# ============================================================================

# Run tests with per-test timing (shows slow tests)
test-verbose: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running tests with timing...$(RESET)"
	$(Q)$(TEST_BIN) --durations yes --use-colour yes

# Run G-code related tests
test-gcode: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running G-code tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[gcode]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ G-code tests passed in $${DURATION}s$(RESET)"

# Run UI-related tests
test-ui: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running UI tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[navigation],[theme],[wizard]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ UI tests passed in $${DURATION}s$(RESET)"

# Run Moonraker/mock-related tests
test-moonraker: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running Moonraker tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[mock],[sequencer],[capabilities]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Moonraker tests passed in $${DURATION}s$(RESET)"

# Run network-related tests (WiFi, Ethernet)
test-network: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running network tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[network],[scan],[connect]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Network tests passed in $${DURATION}s$(RESET)"

# Run security-related tests
test-security: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running security tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[security],[injection],[safety]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Security tests passed in $${DURATION}s$(RESET)"

# List all available test tags
test-list-tags: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Available test tags:$(RESET)"
	$(Q)$(TEST_BIN) --list-tags

# List all test cases
test-list: $(TEST_BIN)
	$(Q)$(TEST_BIN) --list-tests

# Backwards compatibility alias
test-wizard: test

# ============================================================================
# Test Timing and Performance Targets
# ============================================================================
# Use these targets to identify slow tests and optimize test runtime.
# Tests tagged [slow] are excluded from test-fast for quick iteration.

# Show slowest tests (top 20) - useful for identifying optimization targets
test-timing: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Slowest tests (top 20):$(RESET)"
	@$(TEST_BIN) "~[.]" --durations yes 2>&1 | grep -E "^[0-9]+\.[0-9]+ s:" | sort -rn | head -20

# Run only fast tests (skip hidden and slow tests) - for quick iteration
# Target: <30s total runtime for rapid development feedback
test-fast: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running fast tests only (skipping [slow] and hidden)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "~[.] ~[slow]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Fast tests passed in $${DURATION}s$(RESET)"

# Run only slow tests - for thorough validation before commit
test-slow: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running slow tests only...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[slow]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Slow tests passed in $${DURATION}s$(RESET)"

# Show test coverage summary by tag area
test-summary: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)=== Test Coverage by Tag (top 25) ===$(RESET)"
	@$(TEST_BIN) --list-tags 2>&1 | grep -E "^\s+[0-9]+" | sort -rn | head -25
	$(ECHO) ""
	$(ECHO) "$(CYAN)$(BOLD)=== Test Count ===$(RESET)"
	@echo -n "  "; $(TEST_BIN) --list-tests 2>&1 | grep -c "^  " || echo "0"

# ============================================================================
# Slow Test Candidates (>500ms) - Tag with [slow] incrementally
# ============================================================================
# Based on timing analysis, these tests are candidates for [slow] tagging:
#
# Connection retry tests (~5s each):
#   - test_moonraker_client.cpp: "Multiple rapid retries all work correctly"
#   - test_moonraker_client.cpp: "Moonraker connection retries work correctly"
#
# Mock print simulation tests (~2-4s each):
#   - test_mock_print_simulation.cpp: All phase behavior tests
#   - test_mock_print_simulation.cpp: Progress and layer tracking tests
#
# To tag a test as slow, add [slow] to its tags:
#   TEST_CASE("My slow test", "[feature][slow]") { ... }
#
# ============================================================================

# Run only config tests
test-config: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running config tests...$(RESET)"
	$(Q)$(TEST_BIN) "[config]" || { \
		echo "$(RED)$(BOLD)✗ Config tests failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)$(BOLD)✓ Config tests passed!$(RESET)"

# Unified test binary - uses automatic app object discovery
# No more manual dependency lists! New source files are automatically included.
# If you get linker errors, check if the file needs to be excluded (TEST_APP_OBJS filter).
$(TEST_BIN): $(TEST_CORE_DEPS) \
             $(TEST_LVGL_DEPS) \
             $(TEST_APP_OBJS) \
             $(FONT_OBJS) \
             $(OBJCPP_OBJS) \
             $(TEST_PLATFORM_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)$(BOLD)[LD]$(RESET) run_tests"
	$(Q)$(CXX) $(CXXFLAGS) $(sort $^) -o $@ $(LDFLAGS) || { \
		echo "$(RED)$(BOLD)✗ Test linking failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)✓ Unit test binary ready$(RESET)"

# Integration test binary (uses mocks instead of real LVGL)
$(TEST_INTEGRATION_BIN): $(TEST_MAIN_OBJ) $(CATCH2_OBJ) $(TEST_INTEGRATION_OBJS) $(MOCK_OBJS)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)$(BOLD)[LD]$(RESET) run_integration_tests"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) || { \
		echo "$(RED)$(BOLD)✗ Integration test linking failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)✓ Integration test binary ready$(RESET)"

# Run integration tests
test-integration: $(TEST_INTEGRATION_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running integration tests (with mocks)...$(RESET)"
	$(Q)$(TEST_INTEGRATION_BIN) || { \
		echo "$(RED)$(BOLD)✗ Integration tests failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)$(BOLD)✓ All integration tests passed!$(RESET)"

# Compile test main (Catch2 runner)
# Note: No DEPFLAGS for Catch2 infrastructure - rarely changes
$(TEST_MAIN_OBJ): $(TEST_DIR)/test_main.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST-MAIN]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -c $< -o $@

# Compile Catch2 amalgamated source
# Note: No DEPFLAGS - this is third-party code that never changes
$(CATCH2_OBJ): $(TEST_DIR)/catch_amalgamated.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[CATCH2]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile UI test utilities
# Uses DEPFLAGS to track header dependencies
$(UI_TEST_UTILS_OBJ): $(TEST_DIR)/ui_test_utils.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(CYAN)[UI-TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile LVGL test fixture (shared base class for UI tests)
# Uses DEPFLAGS to track header dependencies
$(LVGL_TEST_FIXTURE_OBJ): $(TEST_DIR)/lvgl_test_fixture.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(CYAN)[LVGL-FIXTURE]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile test fixtures (reusable fixtures with mock initialization helpers)
# Uses DEPFLAGS to track header dependencies
$(TEST_FIXTURES_OBJ): $(TEST_DIR)/test_fixtures.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(CYAN)[TEST-FIXTURE]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile test sources
# Uses DEPFLAGS to track header dependencies for incremental rebuilds
$(OBJ_DIR)/tests/%.o: $(TEST_UNIT_DIR)/%.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile mock sources
# Uses DEPFLAGS to track header dependencies
$(OBJ_DIR)/tests/mocks/%.o: $(TEST_MOCK_DIR)/%.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(YELLOW)[MOCK]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_MOCK_DIR) $(INCLUDES) -c $< -o $@

# Dynamic card instantiation test
TEST_CARDS_BIN := $(BIN_DIR)/test_dynamic_cards
TEST_CARDS_OBJ := $(OBJ_DIR)/test_dynamic_cards.o

test-cards: $(TEST_CARDS_BIN)
	$(ECHO) "$(CYAN)Running dynamic card test...$(RESET)"
	$(Q)$(TEST_CARDS_BIN)

$(TEST_CARDS_BIN): $(TEST_CARDS_OBJ) $(LVGL_OBJS) $(FONT_OBJS)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_dynamic_cards"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ Test binary ready$(RESET)"

$(TEST_CARDS_OBJ): $(SRC_DIR)/test_dynamic_cards.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# LV_SIZE_CONTENT behavior test
# Tests nested flex containers with SIZE_CONTENT - LVGL handles this natively
TEST_SIZE_CONTENT_BIN := $(BIN_DIR)/test_size_content
TEST_SIZE_CONTENT_OBJ := $(OBJ_DIR)/tests/test_size_content.o

test-size-content: $(TEST_SIZE_CONTENT_BIN)
	$(ECHO) "$(CYAN)Running LV_SIZE_CONTENT behavior test...$(RESET)"
	$(Q)$(TEST_SIZE_CONTENT_BIN)

$(TEST_SIZE_CONTENT_BIN): $(TEST_SIZE_CONTENT_OBJ) $(CATCH2_OBJ) $(TEST_MAIN_OBJ) $(LVGL_OBJS) $(THORVG_OBJS)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_size_content"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ Test binary ready: $@$(RESET)"

$(TEST_SIZE_CONTENT_OBJ): $(TEST_UNIT_DIR)/test_size_content.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) -I$(TEST_DIR) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Responsive theme and breakpoint test
TEST_RESPONSIVE_THEME_BIN := $(BIN_DIR)/test_responsive_theme
TEST_RESPONSIVE_THEME_OBJ := $(OBJ_DIR)/test_responsive_theme.o

test-responsive-theme: $(TEST_RESPONSIVE_THEME_BIN)
	$(ECHO) "$(CYAN)Running responsive theme and breakpoint tests...$(RESET)"
	$(Q)$(TEST_RESPONSIVE_THEME_BIN)

$(TEST_RESPONSIVE_THEME_BIN): $(TEST_RESPONSIVE_THEME_OBJ) $(LVGL_OBJS) $(THORVG_OBJS) $(OBJ_DIR)/ui_theme.o
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_responsive_theme"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ Responsive theme test binary ready$(RESET)"

$(TEST_RESPONSIVE_THEME_OBJ): $(SRC_DIR)/test_responsive_theme.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# ============================================================================
# TinyGL 3D Rendering Tests
# ============================================================================

# TinyGL Hello Triangle test (only if ENABLE_TINYGL_3D=yes)
ifeq ($(ENABLE_TINYGL_3D),yes)
TEST_TINYGL_TRIANGLE_BIN := $(BIN_DIR)/test_tinygl_triangle
TEST_TINYGL_TRIANGLE_OBJ := $(OBJ_DIR)/test_tinygl_triangle.o

test-tinygl-triangle: $(TEST_TINYGL_TRIANGLE_BIN)
	$(ECHO) "$(CYAN)Running TinyGL triangle test...$(RESET)"
	$(Q)$(TEST_TINYGL_TRIANGLE_BIN)
	$(ECHO) ""

$(TEST_TINYGL_TRIANGLE_BIN): $(TEST_TINYGL_TRIANGLE_OBJ) $(TINYGL_LIB)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_tinygl_triangle"
	$(Q)$(CXX) $(CXXFLAGS) $< -o $@ $(TINYGL_LIB) -lm
	$(ECHO) "$(GREEN)✓ TinyGL triangle test binary ready$(RESET)"

$(TEST_TINYGL_TRIANGLE_OBJ): $(SRC_DIR)/test_tinygl_triangle.cpp $(TINYGL_LIB)
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(TINYGL_INC) -c $< -o $@

# G-Code Geometry Builder test (only if ENABLE_TINYGL_3D=yes)
TEST_GCODE_GEOMETRY_BIN := $(BIN_DIR)/test_gcode_geometry
TEST_GCODE_GEOMETRY_OBJ := $(OBJ_DIR)/test_gcode_geometry.o

test-gcode-geometry: $(TEST_GCODE_GEOMETRY_BIN)
	$(ECHO) "$(CYAN)Running G-code geometry test...$(RESET)"
	$(Q)$(TEST_GCODE_GEOMETRY_BIN)
	$(ECHO) ""

$(TEST_GCODE_GEOMETRY_BIN): $(TEST_GCODE_GEOMETRY_OBJ) $(OBJ_DIR)/gcode_parser.o $(OBJ_DIR)/gcode_geometry_builder.o $(TINYGL_LIB)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_gcode_geometry"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(TINYGL_LIB) -lm $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ G-code geometry test binary ready$(RESET)"

$(TEST_GCODE_GEOMETRY_OBJ): $(SRC_DIR)/test_gcode_geometry.cpp $(TINYGL_LIB)
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(TINYGL_INC) -c $< -o $@

# G-Code SDF Reconstruction test (only if ENABLE_TINYGL_3D=yes)
TEST_SDF_RECONSTRUCTION_BIN := $(BIN_DIR)/test_sdf_reconstruction
TEST_SDF_RECONSTRUCTION_OBJ := $(OBJ_DIR)/test_sdf_reconstruction.o

test-sdf-reconstruction: $(TEST_SDF_RECONSTRUCTION_BIN)
	$(ECHO) "$(CYAN)Running SDF reconstruction test...$(RESET)"
	$(Q)$(TEST_SDF_RECONSTRUCTION_BIN)
	$(ECHO) ""

$(TEST_SDF_RECONSTRUCTION_BIN): $(TEST_SDF_RECONSTRUCTION_OBJ) $(OBJ_DIR)/gcode_parser.o $(OBJ_DIR)/gcode_sdf_builder.o $(TINYGL_LIB)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_sdf_reconstruction"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ $(TINYGL_LIB) -lm $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ SDF reconstruction test binary ready$(RESET)"

$(TEST_SDF_RECONSTRUCTION_OBJ): $(SRC_DIR)/test_sdf_reconstruction.cpp $(TINYGL_LIB)
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(TINYGL_INC) -c $< -o $@

# Sparse Grid test (validates NanoVDB integration)
TEST_SPARSE_GRID_BIN := $(BIN_DIR)/test_sparse_grid
TEST_SPARSE_GRID_OBJ := $(OBJ_DIR)/test_sparse_grid.o

test-sparse-grid: $(TEST_SPARSE_GRID_BIN)
	$(ECHO) "$(CYAN)Running sparse grid test...$(RESET)"
	$(Q)$(TEST_SPARSE_GRID_BIN)
	$(ECHO) ""

$(TEST_SPARSE_GRID_BIN): $(TEST_SPARSE_GRID_OBJ) $(OBJ_DIR)/gcode_parser.o $(OBJ_DIR)/gcode_sdf_builder.o $(OBJ_DIR)/gcode_sparse_grid.o
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_sparse_grid"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ -lm $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ Sparse grid test binary ready$(RESET)"

$(TEST_SPARSE_GRID_OBJ): $(SRC_DIR)/test_sparse_grid.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Partial mesh extraction test
TEST_PARTIAL_EXTRACTION_BIN := $(BIN_DIR)/test_partial_extraction
TEST_PARTIAL_EXTRACTION_OBJ := $(OBJ_DIR)/test_partial_extraction.o

test-partial-extraction: $(TEST_PARTIAL_EXTRACTION_BIN)
	$(ECHO) "$(CYAN)Running partial extraction test...$(RESET)"
	$(Q)$(TEST_PARTIAL_EXTRACTION_BIN)
	$(ECHO) ""

$(TEST_PARTIAL_EXTRACTION_BIN): $(TEST_PARTIAL_EXTRACTION_OBJ) $(OBJ_DIR)/gcode_parser.o $(OBJ_DIR)/gcode_sdf_builder.o
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) test_partial_extraction"
	$(Q)$(CXX) $(CXXFLAGS) $^ -o $@ -lm $(LDFLAGS)
	$(ECHO) "$(GREEN)✓ Partial extraction test binary ready$(RESET)"

$(TEST_PARTIAL_EXTRACTION_OBJ): $(SRC_DIR)/test_partial_extraction.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

else
test-tinygl-triangle:
	$(ECHO) "$(YELLOW)⚠ TinyGL test skipped (ENABLE_TINYGL_3D=no)$(RESET)"
	$(ECHO) "  Rebuild with: make ENABLE_TINYGL_3D=yes test-tinygl-triangle"

test-gcode-geometry:
	$(ECHO) "$(YELLOW)⚠ G-code geometry test skipped (ENABLE_TINYGL_3D=no)$(RESET)"
	$(ECHO) "  Rebuild with: make ENABLE_TINYGL_3D=yes test-gcode-geometry"

test-sdf-reconstruction:
	$(ECHO) "$(YELLOW)⚠ SDF reconstruction test skipped (ENABLE_TINYGL_3D=no)$(RESET)"
	$(ECHO) "  Rebuild with: make ENABLE_TINYGL_3D=yes test-sdf-reconstruction"
endif

# ============================================================================
# TinyGL Test Framework - Comprehensive quality and performance testing
# ============================================================================

TINYGL_TEST_DIR := tests/tinygl
TINYGL_TEST_FRAMEWORK_BIN := $(BIN_DIR)/tinygl_test_runner
TINYGL_TEST_FRAMEWORK_OBJS := \
	$(OBJ_DIR)/tinygl_test_framework.o \
	$(OBJ_DIR)/tinygl_test_runner.o

# TinyGL test framework targets
test-tinygl-framework: $(TINYGL_TEST_FRAMEWORK_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running TinyGL comprehensive test framework...$(RESET)"
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) all

test-tinygl-quality: $(TINYGL_TEST_FRAMEWORK_BIN)
	$(ECHO) "$(CYAN)Testing TinyGL rendering quality...$(RESET)"
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) gouraud
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) banding
	$(ECHO) "$(GREEN)✓ Quality tests complete$(RESET)"

test-tinygl-performance: $(TINYGL_TEST_FRAMEWORK_BIN)
	$(ECHO) "$(CYAN)Benchmarking TinyGL performance...$(RESET)"
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) performance
	$(ECHO) "$(GREEN)✓ Performance benchmarks complete$(RESET)"

test-tinygl-reference: $(TINYGL_TEST_FRAMEWORK_BIN)
	$(ECHO) "$(CYAN)Generating TinyGL reference images...$(RESET)"
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) reference
	$(ECHO) "$(GREEN)✓ Reference images generated$(RESET)"

test-tinygl-verify: $(TINYGL_TEST_FRAMEWORK_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Verifying TinyGL rendering against references...$(RESET)"
	$(Q)$(TINYGL_TEST_FRAMEWORK_BIN) --verify || { \
		echo "$(RED)$(BOLD)✗ Verification failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)$(BOLD)✓ All verification tests passed!$(RESET)"

# Build TinyGL test framework
$(TINYGL_TEST_FRAMEWORK_BIN): $(TINYGL_TEST_FRAMEWORK_OBJS) $(TINYGL_LIB)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)[LD]$(RESET) tinygl_test_runner"
	$(Q)$(CXX) $(CXXFLAGS) $(TINYGL_TEST_FRAMEWORK_OBJS) -o $@ $(TINYGL_LIB) $(LDFLAGS) -lm
	$(ECHO) "$(GREEN)✓ TinyGL test framework ready$(RESET)"

# Compile test framework
$(OBJ_DIR)/tinygl_test_framework.o: $(TINYGL_TEST_DIR)/tinygl_test_framework.cpp $(TINYGL_TEST_DIR)/tinygl_test_framework.h
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) tinygl_test_framework.cpp"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(TINYGL_INC) -I$(TINYGL_TEST_DIR) -c $< -o $@

$(OBJ_DIR)/tinygl_test_runner.o: $(TINYGL_TEST_DIR)/test_runner.cpp $(TINYGL_TEST_DIR)/tinygl_test_framework.h
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) test_runner.cpp"
	$(Q)$(CXX) $(CXXFLAGS) $(INCLUDES) $(TINYGL_INC) -I$(TINYGL_TEST_DIR) -c $< -o $@

# Clean TinyGL test artifacts
clean-tinygl-tests:
	$(ECHO) "$(YELLOW)Cleaning TinyGL test artifacts...$(RESET)"
	$(Q)rm -f $(TINYGL_TEST_FRAMEWORK_BIN) $(TINYGL_TEST_FRAMEWORK_OBJS)
	$(Q)rm -rf $(TINYGL_TEST_DIR)/output $(TINYGL_TEST_DIR)/reference
	$(ECHO) "$(GREEN)✓ TinyGL test artifacts cleaned$(RESET)"

# ============================================================================
# Test Help
# ============================================================================

.PHONY: help-test
help-test:
	@if [ -t 1 ] && [ -n "$(TERM)" ] && [ "$(TERM)" != "dumb" ]; then \
		B='$(BOLD)'; G='$(GREEN)'; Y='$(YELLOW)'; C='$(CYAN)'; X='$(RESET)'; \
	else \
		B=''; G=''; Y=''; C=''; X=''; \
	fi; \
	echo "$${B}Test Targets$${X}"; \
	echo ""; \
	echo "$${C}Main Test Targets:$${X}"; \
	echo "  $${G}test$${X}                 - Build tests (does not run)"; \
	echo "  $${G}test-run$${X}             - Run all unit tests (excludes hidden/slow)"; \
	echo "  $${G}test-fast$${X}            - Run fast tests only (skip [slow] tagged)"; \
	echo "  $${G}test-slow$${X}            - Run only slow tests"; \
	echo "  $${G}test-verbose$${X}         - Run with per-test timing"; \
	echo "  $${G}test-build$${X}           - Build tests without running (alias for test)"; \
	echo ""; \
	echo "$${C}Component Tests:$${X}"; \
	echo "  $${G}test-gcode$${X}           - G-code parsing and geometry tests"; \
	echo "  $${G}test-ui$${X}              - UI navigation, theme, wizard tests"; \
	echo "  $${G}test-moonraker$${X}       - Moonraker client and mock tests"; \
	echo "  $${G}test-network$${X}         - WiFi and Ethernet tests"; \
	echo "  $${G}test-security$${X}        - Security and injection tests"; \
	echo "  $${G}test-config$${X}          - Configuration tests"; \
	echo "  $${G}test-integration$${X}     - Integration tests (with mocks)"; \
	echo ""; \
	echo "$${C}TinyGL 3D Tests:$${X}"; \
	echo "  $${G}test-tinygl-triangle$${X} - Basic TinyGL rendering test"; \
	echo "  $${G}test-gcode-geometry$${X}  - G-code to 3D geometry test"; \
	echo "  $${G}test-tinygl-framework$${X} - Comprehensive TinyGL test suite"; \
	echo "  $${G}test-tinygl-quality$${X}  - Rendering quality tests"; \
	echo "  $${G}test-tinygl-performance$${X} - Performance benchmarks"; \
	echo ""; \
	echo "$${C}Discovery:$${X}"; \
	echo "  $${G}test-list$${X}            - List all test cases"; \
	echo "  $${G}test-list-tags$${X}       - List available test tags"; \
	echo "  $${G}test-timing$${X}          - Show slowest tests (top 20)"; \
	echo "  $${G}test-summary$${X}         - Test coverage by tag"; \
	echo ""; \
	echo "$${C}Cleanup:$${X}"; \
	echo "  $${G}clean-tests$${X}          - Remove test build artifacts"; \
	echo "  $${G}clean-tinygl-tests$${X}   - Remove TinyGL test artifacts"
