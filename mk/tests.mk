# Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen UI Prototype - Test Module
# Handles all test compilation and execution targets

# ============================================================================
# Parallel Test Execution Infrastructure
# ============================================================================
# Uses Catch2 sharding (--shard-count N --shard-index M) to split tests across
# multiple processes. Each shard runs in its own process with its own LVGL
# instance, avoiding thread-safety issues entirely.
#
# Per Catch2 best practices, use more shards than cores to avoid long-tailed
# execution from uneven test distribution.

# Detect CPU count for parallel sharding (2x cores recommended by Catch2)
# Supports: Linux (nproc), macOS (sysctl), fallback to 4 cores
NPROCS := $(shell echo $$(( $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) * 2 )))

# Run tests in parallel using Catch2 sharding
# Args: $(1) = test filter (e.g., "~[.] ~[slow]")
# Collects PIDs and waits for all, failing if any shard fails
# Output is prefixed with [shard N] for clarity
define run_tests_parallel
	echo "$(CYAN)Running $(NPROCS) test shards in parallel...$(RESET)"; \
	pids=""; \
	for i in $$(seq 0 $$(($(NPROCS)-1))); do \
		$(TEST_BIN) $(1) --shard-count $(NPROCS) --shard-index $$i 2>&1 | sed "s/^/[shard $$i] /" & \
		pids="$$pids $$!"; \
	done; \
	failed=0; \
	for pid in $$pids; do \
		wait $$pid || failed=1; \
	done; \
	if [ $$failed -eq 1 ]; then \
		echo "$(RED)$(BOLD)✗ One or more test shards failed!$(RESET)"; \
		exit 1; \
	fi
endef

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
TEST_CORE_DEPS := $(TEST_MAIN_OBJ) $(CATCH2_OBJ) $(UI_TEST_UTILS_OBJ) $(LVGL_TEST_FIXTURE_OBJ) $(TEST_FIXTURES_OBJ) $(LVGL_UI_TEST_FIXTURE_OBJ) $(TEST_OBJS) $(TEST_APP_OBJS_EXTRA)

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
    $(OBJ_DIR)/system/cli_args.o \
    $(OBJ_DIR)/ui/ui_notification.o \
    $(OBJ_DIR)/ui/ui_toast.o \
    $(OBJ_DIR)/ui/ui_toast_manager.o \
    $(OBJ_DIR)/ui/ui_status_bar_manager.o \
    $(OBJ_DIR)/ui/ui_text_input.o \
    $(OBJ_DIR)/ui/ui_emergency_stop.o \
    $(OBJ_DIR)/ui/ui_switch.o \
    $(OBJ_DIR)/application/application.o \
    $(OBJ_DIR)/application/lvgl_initializer.o \
    $(OBJ_DIR)/application/subject_initializer.o \
    $(OBJ_DIR)/application/moonraker_manager.o \
    $(OBJ_DIR)/application/panel_factory.o \
    ,$(APP_OBJS) $(APP_C_OBJS))

# ============================================================================
# Test Targets
# ============================================================================

# Clean test artifacts
clean-tests:
	$(ECHO) "$(YELLOW)Cleaning test artifacts...$(RESET)"
	$(Q)rm -f $(TEST_BIN) $(TEST_MAIN_OBJ) $(CATCH2_OBJ) $(UI_TEST_UTILS_OBJ) $(LVGL_TEST_FIXTURE_OBJ) $(TEST_FIXTURES_OBJ) $(LVGL_UI_TEST_FIXTURE_OBJ) $(TEST_OBJS)
	$(ECHO) "$(GREEN)✓ Test artifacts cleaned$(RESET)"

# Build tests in parallel (auto-detects core count like main build target)
test-build:
	$(ECHO) "$(CYAN)$(BOLD)Building tests with parallel compilation...$(RESET)"
	@START_TIME=$$(date +%s); \
	NPROC=$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); \
	$(MAKE) -j$$NPROC $(TEST_BIN) && \
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
# Delegates to test-build for parallel compilation, then shows usage hint
test: test-build
	$(ECHO) "$(CYAN)Run tests with: make test-run$(RESET)"

# Run unit tests in PARALLEL (excludes hidden and slow tests for fast iteration)
# Uses Catch2 sharding across multiple processes for ~4-8x speedup
# Use 'make test-serial' for sequential execution (debugging, clean output)
# Use 'make test-all' to run everything including slow tests
test-run: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running unit tests in parallel (excluding slow)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(call run_tests_parallel,"~[.] ~[slow]"); \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Tests passed in $${DURATION}s$(RESET)"

# Run unit tests SEQUENTIALLY (for debugging or clean output)
# Slower but useful when you need to see exact test ordering or debug failures
test-serial: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running unit tests sequentially (excluding slow)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "~[.] ~[slow]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Tests passed in $${DURATION}s$(RESET)"

# Run ALL tests including slow ones in PARALLEL (for thorough validation)
test-all: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running ALL tests in parallel (including slow)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(call run_tests_parallel,"~[.]"); \
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

# Run only fast tests in PARALLEL (skip hidden and slow tests) - for quick iteration
# Target: <15s total runtime for rapid development feedback with parallelism
test-fast: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running fast tests in parallel (skipping [slow] and hidden)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(call run_tests_parallel,"~[.] ~[slow]"); \
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

# Run only eventloop tests - hv::EventLoop network tests (very slow, 5-10 min)
# These are the slowest tests due to WebSocket connection/disconnection cycles
test-eventloop: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running eventloop tests only (this will take 5-10 minutes)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[eventloop]" "~[.]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ EventLoop tests passed in $${DURATION}s$(RESET)"

# Smoke test - minimal critical tests for quick validation (<30s)
# Use during rapid iteration to catch obvious regressions
test-smoke: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running smoke tests (minimal critical subset)...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[config],[navigation],[ui_theme],[parser]" "~[slow]" "~[.]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Smoke tests passed in $${DURATION}s$(RESET)"

# Show test coverage summary by tag area
test-summary: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)=== Test Coverage by Tag (top 25) ===$(RESET)"
	@$(TEST_BIN) --list-tags 2>&1 | grep -E "^\s+[0-9]+" | sort -rn | head -25
	$(ECHO) ""
	$(ECHO) "$(CYAN)$(BOLD)=== Test Count ===$(RESET)"
	@echo -n "  "; $(TEST_BIN) --list-tests 2>&1 | grep -c "^  " || echo "0"

# Generate timing report for major test categories
# Updates docs/TEST_TIMING.md with current timings
test-timing-report: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Generating test timing report...$(RESET)"
	@echo "| Tag | Tests | Time |" > /tmp/test_timing.md
	@echo "|-----|-------|------|" >> /tmp/test_timing.md
	@for tag in moonraker gcode printer_detector config parser navigation ui_theme security afc wizard mock; do \
		result=$$($(TEST_BIN) "[$$tag]" "~[.]" "~[slow]" --durations yes 2>&1); \
		count=$$(echo "$$result" | grep "test cases" | grep -o "[0-9]* passed" | head -1 || echo "0"); \
		time=$$(echo "$$result" | tail -1); \
		echo "| [\$$tag] | $$count | - |" >> /tmp/test_timing.md; \
	done
	@cat /tmp/test_timing.md
	$(ECHO) "$(GREEN)See docs/TEST_TIMING.md for full documentation$(RESET)"

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

# ============================================================================
# Feature-Based Test Targets (New Taxonomy)
# ============================================================================
# Tests are now tagged by FEATURE/IMPORTANCE rather than layer/speed.
# Use these targets to test specific functional areas.

# CORE tests - Critical functionality that MUST work (<15s, ~18 tests)
# If these fail, the app is fundamentally broken.
test-core: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running core tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[core]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)$(BOLD)✓ Core tests passed in $${DURATION}s$(RESET)"

# CONNECTION tests - Moonraker connection lifecycle, retry, robustness
test-connection: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running connection tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[connection]" "~[slow]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Connection tests passed in $${DURATION}s$(RESET)"

# STATE tests - PrinterState, subjects, observers
test-state: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running state tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[state]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ State tests passed in $${DURATION}s$(RESET)"

# PRINT tests - Print workflow, start/pause/cancel, exclude object
test-print: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running print tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[print]" "~[slow]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Print tests passed in $${DURATION}s$(RESET)"

# CALIBRATION tests - Bed mesh, input shaper
test-calibration: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running calibration tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[calibration]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Calibration tests passed in $${DURATION}s$(RESET)"

# PRINTER tests - Printer detection, capabilities, hardware
test-printer: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running printer tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[printer]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Printer tests passed in $${DURATION}s$(RESET)"

# AMS tests - All AMS/MMU backends (includes [afc], [valgace])
test-ams: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running AMS tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[ams]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ AMS tests passed in $${DURATION}s$(RESET)"

# FILAMENT tests - Spoolman, filament sensors
test-filament: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running filament tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[filament]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Filament tests passed in $${DURATION}s$(RESET)"

# ASSETS tests - Thumbnails, prerendered images
test-assets: $(TEST_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running assets tests...$(RESET)"
	@START_TIME=$$(date +%s); \
	$(TEST_BIN) "[assets]" && \
	END_TIME=$$(date +%s); \
	DURATION=$$((END_TIME - START_TIME)); \
	echo "$(GREEN)✓ Assets tests passed in $${DURATION}s$(RESET)"

# Unified test binary - uses automatic app object discovery
# No more manual dependency lists! New source files are automatically included.
# If you get linker errors, check if the file needs to be excluded (TEST_APP_OBJS filter).
# Two-phase build for $(TEST_BIN) to handle unlimited -j detection
# Phase 1: No deps - check for unlimited -j and re-invoke if needed
# Phase 2: Normal deps and linking (when _PARALLEL_GUARD is set)
ifndef _PARALLEL_GUARD
$(TEST_BIN): FORCE
	@if echo "$(MAKEFLAGS)" | grep -q 'j' && ! echo "$(MAKEFLAGS)" | grep -q 'jobserver'; then \
		NPROC=$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); \
		echo ""; \
		printf '\033[1;33m⚠️  make -j (unlimited) detected - auto-fixing to -j%s\033[0m\n' "$$NPROC"; \
		echo ""; \
		exec $(MAKE) _PARALLEL_GUARD=1 -j$$NPROC $@; \
	else \
		exec $(MAKE) _PARALLEL_GUARD=1 $@; \
	fi
else
$(TEST_BIN): $(TEST_CORE_DEPS) \
             $(TEST_LVGL_DEPS) \
             $(TEST_APP_OBJS) \
             $(MOCK_OBJS) \
             $(FONT_OBJS) \
             $(OBJCPP_OBJS) \
             $(TEST_PLATFORM_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(ECHO) "$(MAGENTA)$(BOLD)[LD]$(RESET) helix-tests"
	$(Q)$(CXX) $(CXXFLAGS) $(sort $^) -o $@ $(LDFLAGS) || { \
		echo "$(RED)$(BOLD)✗ Test linking failed!$(RESET)"; \
		exit 1; \
	}
	$(ECHO) "$(GREEN)✓ Unit test binary ready$(RESET)"
endif

# FORCE pattern - ensures Phase 1 always runs for dependency re-evaluation
# Without this, make would skip Phase 1 entirely when the binary exists,
# never reaching Phase 2 where real dependencies (.d files) are checked.
.PHONY: FORCE
FORCE:

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

# Compile LVGL UI test fixture (full UI integration test fixture)
# Uses DEPFLAGS to track header dependencies
$(LVGL_UI_TEST_FIXTURE_OBJ): $(TEST_DIR)/lvgl_ui_test_fixture.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(CYAN)[UI-FIXTURE]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile test sources
# Uses DEPFLAGS to track header dependencies for incremental rebuilds
$(OBJ_DIR)/tests/%.o: $(TEST_UNIT_DIR)/%.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) $(INCLUDES) -c $< -o $@

# Compile application subdirectory test sources
$(OBJ_DIR)/tests/application/%.o: $(TEST_UNIT_DIR)/application/%.cpp
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(BLUE)[TEST-APP]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(TEST_DIR) -I$(TEST_UNIT_DIR)/application $(INCLUDES) -c $< -o $@

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
# Sanitizer Targets (Memory Safety Testing)
# ============================================================================
# These targets rebuild the test binary with sanitizers enabled for detecting:
# - ASAN: Memory leaks, use-after-free, buffer overflows
# - TSAN: Data races, deadlocks, thread safety issues
#
# Note: Sanitizer builds are slower and use more memory.
# Results are printed to stderr with detailed stack traces.

# Sanitizer flags
ASAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer -g
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g

# AddressSanitizer test binary
TEST_ASAN_BIN := $(BIN_DIR)/helix-tests-asan

# ThreadSanitizer test binary
TEST_TSAN_BIN := $(BIN_DIR)/helix-tests-tsan

# Build and run tests with AddressSanitizer
test-asan: clean-tests
	$(ECHO) "$(CYAN)$(BOLD)Building tests with AddressSanitizer...$(RESET)"
	@$(MAKE) CXXFLAGS="$(CXXFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" TEST_BIN=$(TEST_ASAN_BIN) $(TEST_ASAN_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running tests with AddressSanitizer...$(RESET)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 $(TEST_ASAN_BIN) "~[.]" 2>&1 | tee /tmp/asan_output.txt
	$(ECHO) "$(GREEN)✓ ASAN test complete - check /tmp/asan_output.txt for issues$(RESET)"

# Build and run tests with ThreadSanitizer
test-tsan: clean-tests
	$(ECHO) "$(CYAN)$(BOLD)Building tests with ThreadSanitizer...$(RESET)"
	@$(MAKE) CXXFLAGS="$(CXXFLAGS) $(TSAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(TSAN_FLAGS)" TEST_BIN=$(TEST_TSAN_BIN) $(TEST_TSAN_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running tests with ThreadSanitizer...$(RESET)"
	@TSAN_OPTIONS=halt_on_error=0 $(TEST_TSAN_BIN) "~[.]" 2>&1 | tee /tmp/tsan_output.txt
	$(ECHO) "$(GREEN)✓ TSAN test complete - check /tmp/tsan_output.txt for issues$(RESET)"

# Run specific test with ASAN (usage: make test-asan-one TEST="[streaming]")
test-asan-one: clean-tests
	$(ECHO) "$(CYAN)$(BOLD)Building tests with AddressSanitizer...$(RESET)"
	@$(MAKE) CXXFLAGS="$(CXXFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" TEST_BIN=$(TEST_ASAN_BIN) $(TEST_ASAN_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running test '$(TEST)' with AddressSanitizer...$(RESET)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 $(TEST_ASAN_BIN) "$(TEST)" 2>&1 | tee /tmp/asan_output.txt

# Run specific test with TSAN (usage: make test-tsan-one TEST="[streaming]")
test-tsan-one: clean-tests
	$(ECHO) "$(CYAN)$(BOLD)Building tests with ThreadSanitizer...$(RESET)"
	@$(MAKE) CXXFLAGS="$(CXXFLAGS) $(TSAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(TSAN_FLAGS)" TEST_BIN=$(TEST_TSAN_BIN) $(TEST_TSAN_BIN)
	$(ECHO) "$(CYAN)$(BOLD)Running test '$(TEST)' with ThreadSanitizer...$(RESET)"
	@TSAN_OPTIONS=halt_on_error=0 $(TEST_TSAN_BIN) "$(TEST)" 2>&1 | tee /tmp/tsan_output.txt

# Clean sanitizer binaries
clean-sanitizers:
	$(ECHO) "$(YELLOW)Cleaning sanitizer binaries...$(RESET)"
	$(Q)rm -f $(TEST_ASAN_BIN) $(TEST_TSAN_BIN)
	$(ECHO) "$(GREEN)✓ Sanitizer binaries cleaned$(RESET)"

# ============================================================================
# Test Help
# ============================================================================

.PHONY: help-test test-serial test-asan test-tsan test-asan-one test-tsan-one clean-sanitizers
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
	echo "  $${G}test-run$${X}             - Run tests in PARALLEL (default, ~4-8x faster)"; \
	echo "  $${G}test-serial$${X}          - Run tests sequentially (for debugging)"; \
	echo "  $${G}test-smoke$${X}           - Quick smoke test (~30s) for rapid iteration"; \
	echo "  $${G}test-all$${X}             - Run ALL tests in parallel (including slow)"; \
	echo "  $${G}test-slow$${X}            - Run only [slow] tagged tests"; \
	echo "  $${G}test-eventloop$${X}       - Run only [eventloop] tests (5-10 min)"; \
	echo "  $${G}test-verbose$${X}         - Run with per-test timing (sequential)"; \
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
	echo "$${C}Sanitizers (Memory/Thread Safety):$${X}"; \
	echo "  $${G}test-asan$${X}            - Run all tests with AddressSanitizer"; \
	echo "  $${G}test-tsan$${X}            - Run all tests with ThreadSanitizer"; \
	echo "  $${G}test-asan-one TEST=X$${X} - Run specific test with ASAN"; \
	echo "  $${G}test-tsan-one TEST=X$${X} - Run specific test with TSAN"; \
	echo ""; \
	echo "$${C}Cleanup:$${X}"; \
	echo "  $${G}clean-tests$${X}          - Remove test build artifacts"; \
	echo "  $${G}clean-tinygl-tests$${X}   - Remove TinyGL test artifacts"; \
	echo "  $${G}clean-sanitizers$${X}     - Remove sanitizer test binaries"
