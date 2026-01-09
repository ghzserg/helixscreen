# Pre-Print Toggle Control: Refactor Plan

> **Last Updated**: 2026-01-09
> **Status**: Core functionality complete, enum consolidation complete

## Overview

The pre-print subsystem enables users to control operations like bed mesh calibration, QGL, Z-tilt adjustment, and nozzle cleaning before a print starts. These operations can be embedded in three places:

1. **G-code file** - Slicer-generated commands embedded directly in the file
2. **PRINT_START macro** - Printer-side macro that runs at print start
3. **Printer capability database** - Known capabilities for specific printer models

The subsystem detects these operations and presents unified checkboxes to the user. When the user unchecks an operation, the system either:
- Comments out the command in the G-code file (for file-embedded ops)
- Passes skip/perform parameters to PRINT_START (for macro-embedded ops)

### Current State

The core functionality works correctly after recent fixes. The system uses a priority order of **Database > Macro > File** for determining which parameter to use when controlling an operation. Key components are stable but have accumulated technical debt that makes maintenance difficult.

---

## Architecture Diagram

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                    UI Layer                             │
                    │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌───────┐ │
                    │  │  Bed Mesh  │ │    QGL     │ │  Z-Tilt    │ │ Clean │ │
                    │  │ Checkbox   │ │ Checkbox   │ │ Checkbox   │ │ CB    │ │
                    │  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘ └───┬───┘ │
                    └────────┼──────────────┼──────────────┼────────────┼─────┘
                             │              │              │            │
                             └──────────────┴──────────────┴────────────┘
                                            │
                                            ▼
                    ┌─────────────────────────────────────────────────────────┐
                    │             PrintPreparationManager                     │
                    │                   (Orchestrator)                        │
                    │                                                         │
                    │  start_print() ───┬─── collect_ops_to_disable()        │
                    │                   │                                     │
                    │                   └─── collect_macro_skip_params()     │
                    │                             │                           │
                    │                             ▼                           │
                    │                   PRIORITY ORDER:                       │
                    │                   1. Database (cached)                  │
                    │                   2. Macro analysis                     │
                    │                   3. File scan                          │
                    └─────────────────────────┬───────────────────────────────┘
                                              │
                    ┌─────────────────────────┼───────────────────────────────┐
                    │                         │                               │
          ┌─────────▼─────────┐    ┌─────────▼─────────┐    ┌────────▼───────┐
          │  GCodeOpsDetector │    │ PrintStartAnalyzer│    │ PrinterDetector│
          │  (File Scanning)  │    │ (Macro Analysis)  │    │ (Database)     │
          │                   │    │                   │    │                │
          │ • Scans G-code    │    │ • Fetches macro   │    │ • JSON lookup  │
          │   file content    │    │   from printer    │    │   by printer   │
          │ • Finds embedded  │    │ • Detects skip/   │    │   model name   │
          │   operations      │    │   perform params  │    │                │
          │ • Returns line #s │    │ • Determines      │    │ • Returns      │
          │   for commenting  │    │   controllability │    │   native param │
          │                   │    │                   │    │   names        │
          └───────────────────┘    └───────────────────┘    └────────────────┘
                    │                        │                       │
                    │                        │                       │
                    ▼                        ▼                       ▼
          ┌───────────────────────────────────────────────────────────────────┐
          │                      operation_patterns.h                         │
          │                    (Shared Pattern Definitions)                   │
          │                                                                   │
          │  • OPERATION_KEYWORDS[] - Commands to detect                      │
          │  • SKIP_PARAM_VARIATIONS[] - SKIP_* patterns                      │
          │  • PERFORM_PARAM_VARIATIONS[] - PERFORM_*/DO_* patterns           │
          └───────────────────────────────────────────────────────────────────┘
```

---

## Status Summary

| Category | Status | Details |
|----------|--------|---------|
| **Core Feature** | ✅ Complete | Pre-print toggles work for AD5M and generic printers |
| **PERFORM_* Support** | ✅ Complete | Opt-in parameters detected alongside SKIP_* |
| **Race Condition Fix** | ✅ Complete | Print button disabled during macro analysis |
| **Priority Order** | ✅ Complete | Database → Macro → File (unified UI + execution) |
| **Capability Caching** | ✅ Complete | Avoids repeated database lookups |
| **Test Coverage** | ✅ Complete | Added tests for async state, cache, priority order |
| **Enum Consolidation** | ✅ Complete | Single `OperationCategory` source of truth |
| **CapabilityMatrix** | ✅ Complete | Unified capability source management (MT2) |
| **Retry Logic** | ✅ Complete | Exponential backoff for macro analysis (MT3) |
| **Technical Debt** | 🟡 Reduced | Checkbox ambiguity addressed by LT2, LT3 remains |

---

## ✅ Completed Work

| Commit | Date | Description |
|--------|------|-------------|
| `4a589648` | 2026-01-09 | Wire up printer type from config to PrinterState (LT1 Phase 6) |
| `7ef347eb` | 2026-01-09 | Move printer capability cache to PrinterState (LT1 Phase 5) |
| `ca308c8f` | 2026-01-09 | Added retry logic for macro analysis with exponential backoff (MT3) |
| `6765aa3b` | 2026-01-09 | Added CapabilityMatrix for unified capability sources (MT2) |
| `2d4c98b0` | 2026-01-08 | Consolidated operation enums into single source of truth (MT1) |
| `653a84fc` | 2026-01-08 | Added this refactor plan document |
| `7a56b19d` | 2026-01-08 | Unified priority order (Database > Macro > File) and added capability caching |
| `95acc10e` | 2026-01-08 | Disabled Print button during macro analysis (race condition fix) |
| `6c0f3759` | 2026-01-07 | Fixed capability database key naming (bed_mesh not bed_leveling) |
| `9854ef19` | 2026-01-07 | Added PERFORM_* opt-in parameter support alongside SKIP_* |

### What's Working Now
- ✅ Bed mesh, QGL, Z-tilt, nozzle clean checkboxes in file detail view
- ✅ Checkboxes correctly disable operations in PRINT_START macro
- ✅ AD5M uses native `FORCE_LEVELING=false` parameter from database
- ✅ Generic printers use detected `SKIP_*` or `PERFORM_*` parameters
- ✅ Print button disabled until macro analysis completes (no race condition)
- ✅ File-embedded operations can be commented out
- ✅ Macro analysis retries on network failure (3 attempts, exponential backoff)
- ✅ User notification on persistent macro analysis failure
- ✅ CapabilityMatrix provides unified capability querying with priority

---

## 🟡 Remaining Issues

| Priority | Issue | Location | Effort |
|----------|-------|----------|--------|
| ~~High~~ | ~~Three operation enums with redundant definitions~~ | ~~Multiple files~~ | ✅ DONE |
| ~~Low~~ | ~~Mutable cache pattern lacks thread-safety documentation~~ | ~~`ui_print_preparation_manager.h`~~ | ✅ DONE |
| ~~Medium~~ | ~~No retry for macro analysis on network failure~~ | ~~`analyze_print_start_macro()`~~ | ✅ DONE |
| ~~Low~~ | ~~Silent macro analysis failure (no user notification)~~ | ~~`analyze_print_start_macro()`~~ | ✅ DONE |
| Medium | No priming checkbox in UI | `print_detail_panel.xml` | 1h |
| Low | Redundant detection in both analyzers | `GCodeOpsDetector` + `PrintStartAnalyzer` | 4h |
| ~~Low~~ | ~~PrinterState vs PrinterDetector capability divergence~~ | ~~Two independent capability sources~~ | ✅ DONE (LT1) |
| ~~Low~~ | ~~Checkbox semantic ambiguity~~ | ~~`PrePrintOptions` struct~~ | ✅ DONE (LT2) |

---

## 🟡 Medium-Term Refactors (2-4 hours each)

> **Status**: MT1, MT2, MT3 complete

### ✅ MT1: Consolidate Operation Enums (COMPLETED 2026-01-09)

**What was done:**
- Made `helix::OperationCategory` from `operation_patterns.h` the single source of truth
- `PrintStartOpCategory` is now a `using` alias to `OperationCategory`
- `gcode::OperationType` is now a `using` alias to `OperationCategory`
- Removed all conversion functions (`to_print_start_category()`, `to_operation_type()`)
- Added tests for capability cache invalidation and priority order consistency

**Result:**
Adding a new operation (e.g., input shaper) now requires changes in only 1 file (`operation_patterns.h`).

**Previous State (for reference):**

| File | Enum | Status |
|------|------|--------|
| `gcode_ops_detector.h` | `gcode::OperationType` | Now alias to `OperationCategory` |
| `print_start_analyzer.h` | `helix::PrintStartOpCategory` | Now alias to `OperationCategory` |
| `operation_patterns.h` | `helix::OperationCategory` | **Source of truth** |

---

### ✅ MT2: Create Capability Matrix Struct (COMPLETED 2026-01-09)

**What was done:**
- Created `CapabilityMatrix` class in `include/capability_matrix.h`
- Unified three capability sources: DATABASE, MACRO_ANALYSIS, FILE_SCAN
- Priority-aware querying via `get_best_source()` method
- Added 17 test cases (138 assertions) covering all edge cases
- Fixed bug in MACRO_PARAMETER embedding handling

**Key Implementation:**
```cpp
enum class CapabilityOrigin { DATABASE, MACRO_ANALYSIS, FILE_SCAN };

class CapabilityMatrix {
    void add_from_database(const PrintStartCapabilities& caps);
    void add_from_macro_analysis(const PrintStartAnalysis& analysis);
    void add_from_file_scan(const gcode::ScanResult& scan);

    bool is_controllable(OperationCategory op) const;
    std::optional<CapabilitySource> get_best_source(OperationCategory op) const;
    std::optional<std::pair<std::string, std::string>> get_skip_param(OperationCategory op) const;
};
```

**Benefit:** Single unified interface for capability queries. Future integration into `PrintPreparationManager::collect_macro_skip_params()` will simplify the priority logic.

---

### ✅ MT3: Add Retry Logic for Macro Analysis (COMPLETED 2026-01-09)

**What was done:**
- Added `macro_analysis_retry_count_` and `MAX_MACRO_ANALYSIS_RETRIES = 2`
- Split into `analyze_print_start_macro()` (public, resets counter) and `analyze_print_start_macro_internal()` (for retries)
- Exponential backoff: 1s, 2s delays between retries (via LVGL timer)
- `macro_analysis_in_progress_` stays true during retries
- `NOTIFY_ERROR()` shown only on final failure (all 3 attempts exhausted)
- Safe timer pattern with `RetryTimerData` capturing `alive_guard_` to prevent use-after-free

**Key Implementation:**
```cpp
// Schedule retry via LVGL timer with safe lifetime management
struct RetryTimerData {
    PrintPreparationManager* mgr;
    std::shared_ptr<bool> alive_guard;
};
auto* timer_data = new RetryTimerData{mgr, mgr->alive_guard_};
lv_timer_create([](lv_timer_t* timer) {
    auto* data = static_cast<RetryTimerData*>(lv_timer_get_user_data(timer));
    if (data && data->alive_guard && *data->alive_guard) {
        data->mgr->analyze_print_start_macro_internal();
    }
    delete data;
    lv_timer_delete(timer);
}, delay_ms, timer_data);
```

**Benefit:** Users now get retry + notification on persistent failures. Print button remains disabled during entire retry sequence.

---

## 🟠 Long-Term Refactors (6+ hours)

> **Status**: LT1 complete, LT2/LT3 not started

### ✅ LT1: Move Capabilities to PrinterState (COMPLETED 2026-01-09)

**What was done:**
- Added `printer_type_` and `print_start_capabilities_` storage to `PrinterState`
- Added `set_printer_type()` (async) and `set_printer_type_sync()` (main-thread) methods
- `PrinterState` fetches capabilities from `PrinterDetector` when type is set
- Refactored `UIPrintPreparationManager` to delegate to `PrinterState::get_print_start_capabilities()`
- Refactored `MacroModificationManager` to use `get_printer_state().get_print_start_capabilities()`
- Wired up `HomePanel::reload_from_config()` to call `set_printer_type_sync()` on startup/wizard completion
- Added comprehensive test suite (`test_printer_state_capabilities.cpp`, 20+ tests)

**Key commits:**
- `7ef347eb` - Phase 5: Move capability cache to PrinterState
- `4a589648` - Phase 6: Wire up printer type from config

**Result:**
`PrinterState` now owns printer type and capabilities. `PrinterDetector` is a database lookup utility only.
Capabilities refresh automatically when printer type changes via wizard or config reload.

---

### ✅ LT2: Observer Pattern for Checkbox State (COMPLETED 2026-01-09)

**What was done:**
- Added `event_cb` callbacks to XML switches (`on_preprint_*_toggled`)
- Added static callback handlers in PrintSelectDetailView that update subjects when toggles change
- Added subject getters to PrintSelectDetailView (`get_preprint_*_subject()`)
- Added visibility subject getters to PrinterState (`get_can_show_*_subject()`)
- Added `set_preprint_subjects()` and `set_preprint_visibility_subjects()` to PrintPreparationManager
- Implemented `read_options_from_subjects()` - reads checkbox state from subjects with visibility checking
- Wired up subjects in `PrintSelectDetailView::set_dependencies()`
- Added 46 test assertions across 4 test cases

**Key Implementation:**
```cpp
// Read from subjects instead of widgets - fully declarative
auto is_visible_and_checked = [](lv_subject_t* visibility, lv_subject_t* checked) -> bool {
    if (visibility && lv_subject_get_int(visibility) == 0) return false;  // Hidden
    return checked && lv_subject_get_int(checked) == 1;  // Checked
};
```

**Result:**
Checkbox state now flows through subjects:
1. User toggles switch → `event_cb` fires → subject updated
2. `read_options_from_subjects()` reads subject values (no widget tree traversal)
3. Easier testing, better separation of concerns

---

### LT3: Unify GCodeOpsDetector and PrintStartAnalyzer

**Current State:**
Both classes scan for the same operations using similar patterns. `GCodeOpsDetector` scans file content; `PrintStartAnalyzer` scans macro content.

**Target Architecture:**
- Extract common scanning logic into `OperationScanner` class
- `GCodeOpsDetector` becomes thin wrapper: `scan_file()` -> `OperationScanner::scan()`
- `PrintStartAnalyzer` becomes thin wrapper: `parse_macro()` -> `OperationScanner::scan()`
- Add context flag to differentiate file vs macro scanning

**Benefit:** Single implementation of pattern matching, easier to add new operations.

**Effort:** 6 hours

---

## ⚠️ Test Coverage Gaps

> **Status**: Tests needed before major refactors

### Missing Tests

| Test | File | Description |
|------|------|-------------|
| `is_macro_analysis_in_progress()` state transitions | `test_print_preparation_manager.cpp` | Verify flag is set/cleared correctly during async analysis |
| Capability cache invalidation on printer change | `test_print_preparation_manager.cpp` | Verify cache clears when printer type changes |
| Thread-safety of mutable cache access | `test_print_preparation_manager.cpp` | Verify concurrent reads don't cause races |
| Macro analysis retry behavior | `test_print_preparation_manager.cpp` | Verify retry count and backoff timing |
| Format consistency between UI and execution | `test_print_preparation_manager.cpp` | Verify `format_preprint_steps()` matches `collect_macro_skip_params()` order |

### Recommended Test Additions

```cpp
// Test: is_macro_analysis_in_progress() state machine
TEST_CASE("PrintPreparationManager: macro analysis progress tracking",
          "[print_preparation][async]") {
    PrintPreparationManager manager;
    MockMoonrakerAPI mock_api;
    MockPrinterState mock_state;

    manager.set_dependencies(&mock_api, &mock_state);

    SECTION("In-progress flag set during analysis") {
        REQUIRE_FALSE(manager.is_macro_analysis_in_progress());

        manager.analyze_print_start_macro();
        REQUIRE(manager.is_macro_analysis_in_progress());

        // Simulate completion
        mock_api.complete_pending_requests();
        drain_async_queue();

        REQUIRE_FALSE(manager.is_macro_analysis_in_progress());
    }
}

// Test: Capability cache invalidation
TEST_CASE("PrintPreparationManager: capability cache invalidation",
          "[print_preparation][cache]") {
    PrintPreparationManager manager;

    SECTION("Cache invalidates when printer type changes") {
        // Set printer type to AD5M
        Config::get_instance()->set("printer/type", "FlashForge Adventurer 5M Pro");

        // Access capabilities (populates cache)
        auto caps1 = manager.get_cached_capabilities();
        REQUIRE_FALSE(caps1.empty());

        // Change printer type
        Config::get_instance()->set("printer/type", "Voron 2.4");

        // Access should return different capabilities
        auto caps2 = manager.get_cached_capabilities();
        // Voron doesn't have FORCE_LEVELING
        REQUIRE(caps1.params != caps2.params);
    }
}
```

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-01 | Use Database > Macro > File priority | Database entries are curated and provide correct native parameter names. Macro analysis may detect wrong patterns. File operations are lowest priority since they're handled by commenting out. |
| 2025-01 | Add PERFORM_* alongside SKIP_* | Many printers use opt-in semantics (PERFORM_BED_MESH=1 means do it). Only supporting SKIP_* missed these configurations. |
| 2025-01 | Use mutable cache for capabilities | Avoid repeated JSON parsing. Thread-safety relies on LVGL's single-threaded model (all access from main thread). |
| 2025-01 | Disable Print button during macro analysis | Prevents race condition where user clicks Print before analysis completes, resulting in missing skip parameters. Better UX than post-print warning. |
| 2025-01 | Keep three detector classes separate | Each has different responsibilities: file modification (GCodeOps), macro parameters (PrintStart), static lookup (Printer). Consolidation deferred to LT3. |

---

## Threading Model Notes

The pre-print subsystem has specific threading requirements:

1. **Macro Analysis Callbacks**: Run on HTTP thread. Must use `ui_queue_update()` to defer shared state updates to main thread.

2. **Capability Cache**: Uses `mutable` for lazy initialization. Safe because all access occurs on main LVGL thread. If threading model changes, cache needs mutex protection.

3. **Print Start Flow**: `start_print()` must be called from main thread (reads checkbox widgets). Async file modification callbacks use `alive_guard_` pattern to detect if manager was destroyed.

```cpp
// Safe pattern for async callbacks:
auto alive = alive_guard_;  // Capture shared_ptr
api_->async_operation(
    [self, alive](...) {
        if (!alive || !*alive) {
            return;  // Manager destroyed, bail out
        }
        // Safe to access self->...
    }
);
```
