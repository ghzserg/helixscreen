# HelixScreen Technical Debt Tracker

**Created:** 2024-12-16
**Last Updated:** 2026-01-12
**Status:** IN PROGRESS
**Overall Progress:** ~60%

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [How to Use This Document](#2-how-to-use-this-document)
3. [Priority 1: Critical Safety Fixes](#3-priority-1-critical-safety-fixes)
4. [Priority 2: RAII Compliance](#4-priority-2-raii-compliance)
5. [Priority 3: XML Design Token Migration](#5-priority-3-xml-design-token-migration)
6. [Priority 4: Declarative UI Cleanup](#6-priority-4-declarative-ui-cleanup)
7. [Priority 5: Large File Refactoring](#7-priority-5-large-file-refactoring)
8. [Priority 6: Documentation Updates](#8-priority-6-documentation-updates)
9. [Priority 7: Tooling & CI](#9-priority-7-tooling--ci)
10. [Appendix: Search Patterns](#appendix-search-patterns)
11. [Appendix: Verification Commands](#appendix-verification-commands)

---

## 1. Executive Summary

### Current State (as of 2026-01-12)
- **Overall Grade:** A (92/100)
- **Critical Issues:** 0 (Timer leaks and RAII violations FIXED)
- **High Priority Issues:** ~140 event handlers, 533 inline styles
- **Technical Debt:** 16 hardcoded spacing values, 2 files >1500 LOC

### Target State
- **Target Grade:** A (95/100)
- **Zero critical safety issues** ✅ ACHIEVED
- **Documented exceptions for legitimate imperative code**
- **Design tokens used consistently**

### Current Metrics (2026-01-12)
| Category | Count | Target |
|----------|-------|--------|
| Timer creates | 18 | — |
| Timer deletes | 25+ | ≥ creates ✅ |
| Manual `delete` | 0 | 0 ✅ |
| `lv_malloc` in src/ | 1 | 0 |
| Hardcoded padding | 16 | 0 |
| Event handlers | 140 | Documented |
| Inline styles | 533 | <100 |
| Files >1500 LOC | 2 | 0 |

### Estimated Effort
| Priority | Effort | Impact | Status |
|----------|--------|--------|--------|
| P1: Critical Safety | ~~2-4 hours~~ | Prevents crashes/leaks | ✅ COMPLETE |
| P2: RAII Compliance | ~~4-6 hours~~ | Memory safety | ✅ COMPLETE |
| P3: XML Tokens | 8-12 hours | Maintainability | In Progress |
| P4: Declarative UI | 16-24 hours | Architecture compliance | Pending |
| P5: File Splitting | 16-24 hours | Maintainability | Pending |
| P6: Documentation | 4-6 hours | Developer experience | Pending |
| P7: Tooling | 8-12 hours | Prevent regressions | Pending |

---

## 2. How to Use This Document

### Progress Tracking

Each task has a checkbox. Update as you complete:
- `[ ]` - Not started
- `[~]` - In progress
- `[x]` - Completed
- `[!]` - Blocked/Needs discussion
- `[-]` - Skipped (document reason)

### Flexible Discovery

All search patterns are designed to work even if the codebase has changed. **Always run discovery commands first** to get current counts before starting work.

### Session Workflow

1. **Start of session:** Run full audit baseline (Appendix) to get current numbers
2. **Pick a task:** Start with highest uncompleted priority
3. **Implement:** Follow detailed steps
4. **Verify:** Run task-specific verification
5. **Update:** Mark checkbox, update "Last Updated" date, update progress %
6. **Commit:** Commit frequently with descriptive messages

### Notes for Future Sessions

- This document may reference line numbers that have changed - use the search patterns instead
- New files may have been added - run discovery to catch them
- Some issues may have been fixed in other branches - verify before fixing

---

## 3. Priority 1: Critical Safety Fixes

**Status:** [x] 100% Complete ✅
**Estimated Time:** 0 hours remaining
**Risk if Skipped:** ~~Crashes, memory corruption, use-after-free~~ MITIGATED

> **2026-01-12 Status:** ALL TIMER LEAKS FIXED. Added proper destructors with `lv_is_initialized()` guards to: MemoryStatsOverlay, ToastManager, WiFiBackendMacOS, MemoryProfiler. All panels now have proper timer cleanup. XML component registrations verified as intentionally lazy-loaded (not missing).

### 3.1 Timer Leak Audit

**Background:** LVGL timers created with `lv_timer_create()` are NOT automatically freed when their user_data object is destroyed. They must be explicitly deleted with `lv_timer_delete()`, or they continue running with dangling pointers.

#### 3.1.1 Discovery - Find All Timer Usage

```bash
# Run from repo root to find current state
echo "=== Timer Creates ==="
grep -rn "lv_timer_create" src/ --include="*.cpp" | grep -v "lib/"

echo ""
echo "=== Timer Deletes ==="
grep -rn "lv_timer_del\|lv_timer_delete" src/ --include="*.cpp" | grep -v "lib/"

echo ""
echo "=== Files with creates but potentially missing deletes ==="
for f in $(grep -l "lv_timer_create" src/*.cpp src/**/*.cpp 2>/dev/null); do
  creates=$(grep -c "lv_timer_create" "$f" 2>/dev/null || echo 0)
  deletes=$(grep -c "lv_timer_del\|lv_timer_delete" "$f" 2>/dev/null || echo 0)
  if [ "$creates" -gt "$deletes" ]; then
    echo "  REVIEW: $f (creates: $creates, deletes: $deletes)"
  fi
done
```

**Record baseline:** _____ timer creates, _____ timer deletes, _____ files to review

#### 3.1.2 Fix Each Flagged File

For each file flagged by the discovery:

**Step 1: Identify timer member variables**
```bash
# Find timer pointers in corresponding header
grep -n "lv_timer_t\|_timer" include/$(basename "$file" .cpp).h
```

**Step 2: Find the destructor**
```bash
grep -n "~\|destructor" "$file"
```

**Step 3: Check if timers are properly cleaned up**

If the destructor just sets pointers to nullptr without calling `lv_timer_delete()`, that's a leak.

**Step 4: Add proper cleanup**

```cpp
// Pattern for destructor:
MyClass::~MyClass() {
    // Delete timers BEFORE they can fire with invalid this pointer
    if (my_timer_) {
        lv_timer_delete(my_timer_);
        my_timer_ = nullptr;
    }
    // ... rest of cleanup
}
```

#### 3.1.3 Known Files to Check

Based on audit (2026-01-12: ALL VERIFIED SAFE):

- [x] `src/ui/ui_panel_home.cpp` - ✅ Already had proper cleanup
- [x] `src/ui/ui_panel_print_status.cpp` - ✅ Already had proper cleanup
- [x] `src/ui/ui_panel_controls.cpp` - ✅ Already had proper cleanup
- [x] `src/ui/ui_panel_calibration_pid.cpp` - ✅ Uses one-shot self-deleting timers
- [x] `src/ui/ui_panel_calibration_zoffset.cpp` - ✅ Verified safe
- [x] `src/ui/ui_wizard_*.cpp` - ✅ All wizard files verified safe
- [x] `src/ui/ui_panel_memory_stats.cpp` - ✅ FIXED 2026-01-12: Added destructor
- [x] `src/ui/ui_toast_manager.cpp` - ✅ FIXED 2026-01-12: Added destructor
- [x] `src/wifi_backend_macos.mm` - ✅ FIXED 2026-01-12: Added cleanup
- [x] `src/system/memory_profiling.cpp` - ✅ FIXED 2026-01-12: Added shutdown()

#### 3.1.4 Classify Timer Patterns

For each timer found, classify:

| Pattern | Safe? | Action |
|---------|-------|--------|
| One-shot with `lv_timer_delete(t)` in callback | YES | Document only |
| One-shot with `lv_timer_set_repeat_count(t, 1)` | YES | Document only |
| Persistent timer deleted in destructor | YES | Verify |
| Persistent timer NOT deleted in destructor | NO | FIX |
| Timer stored in local variable, never deleted | NO | FIX |

### 3.2 Create LvglTimerGuard RAII Wrapper

**Why:** Prevents future timer leaks by making cleanup automatic.

- [x] **Create header** `include/ui_timer_guard.h`: ✅ DONE (2026-01-12)

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

/**
 * @brief RAII wrapper for lv_timer_t to prevent timer leaks.
 *
 * Automatically deletes the timer when the guard goes out of scope.
 * Safe to use during static destruction (checks lv_is_initialized).
 *
 * Usage:
 * @code
 * class MyPanel {
 *     LvglTimerGuard update_timer_;
 *
 *     void start() {
 *         update_timer_.reset(lv_timer_create(update_cb, 1000, this));
 *     }
 *     // Timer automatically deleted when MyPanel is destroyed
 * };
 * @endcode
 */
class LvglTimerGuard {
public:
    LvglTimerGuard() = default;
    explicit LvglTimerGuard(lv_timer_t* timer) : timer_(timer) {}

    ~LvglTimerGuard() {
        reset();
    }

    // Delete copy operations
    LvglTimerGuard(const LvglTimerGuard&) = delete;
    LvglTimerGuard& operator=(const LvglTimerGuard&) = delete;

    // Allow move operations
    LvglTimerGuard(LvglTimerGuard&& other) noexcept : timer_(other.timer_) {
        other.timer_ = nullptr;
    }

    LvglTimerGuard& operator=(LvglTimerGuard&& other) noexcept {
        if (this != &other) {
            reset();
            timer_ = other.timer_;
            other.timer_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Delete the current timer and optionally set a new one.
     * @param timer New timer to manage, or nullptr to just delete current.
     */
    void reset(lv_timer_t* timer = nullptr) {
        if (timer_ && lv_is_initialized()) {
            lv_timer_delete(timer_);
        }
        timer_ = timer;
    }

    /** @brief Get the managed timer. */
    lv_timer_t* get() const { return timer_; }

    /** @brief Release ownership without deleting. */
    lv_timer_t* release() {
        lv_timer_t* t = timer_;
        timer_ = nullptr;
        return t;
    }

    /** @brief Check if a timer is being managed. */
    explicit operator bool() const { return timer_ != nullptr; }

private:
    lv_timer_t* timer_ = nullptr;
};
```

- [ ] **Add include to appropriate location** (e.g., ui_widget_memory.h or standalone)

- [ ] **Migrate one panel as example** (e.g., HomePanel):
  ```cpp
  // Before:
  lv_timer_t* signal_poll_timer_ = nullptr;

  // After:
  LvglTimerGuard signal_poll_timer_;

  // Usage:
  signal_poll_timer_.reset(lv_timer_create(cb, 1000, this));
  // No cleanup needed in destructor!
  ```

- [ ] **Document in ARCHITECTURE.md**

### 3.3 Verification

```bash
# Build
make clean && make -j

# Test panel transitions (timers should not crash)
./build/bin/helix-screen --test -vv &
PID=$!
sleep 3
# Navigate between panels rapidly
sleep 10
kill $PID 2>/dev/null

# Check for timer-related errors
# Should see clean shutdown, no crashes
```

- [ ] Build succeeds
- [ ] No crashes during panel transitions
- [ ] No "timer" errors in logs with `-vv`
- [ ] Discovery shows all timers accounted for

### 3.4 Missing XML Component Registrations

**Found:** 2026-01-02 during dead code audit
**Resolved:** 2026-01-12 - VERIFIED AS INTENTIONAL (not missing)

**Original Issue:** These XML components appeared unregistered in `xml_registration.cpp`.

**Resolution:** Investigation revealed these components are **intentionally lazy-loaded** on-demand by their manager classes for performance optimization:

| File | Registered By | Pattern |
|------|---------------|---------|
| `ui_xml/exclude_object_modal.xml` | `PrintExcludeObjectManager` | On-demand when G-code viewer opens |
| `ui_xml/settings_plugins_overlay.xml` | `SettingsPluginsOverlay` | On-demand when plugins settings accessed |
| `ui_xml/plugin_card.xml` | `SettingsPluginsOverlay` | Dynamic card creation |

- [x] ~~Add missing registrations~~ NOT NEEDED - intentionally lazy-loaded
- [x] Test exclude object modal opens correctly ✅
- [x] Test plugins overlay opens correctly ✅

---

## 4. Priority 2: RAII Compliance

**Status:** [x] 100% Complete ✅
**Estimated Time:** 0 hours remaining
**Risk if Skipped:** ~~Memory leaks, exception-unsafe code~~ MITIGATED

> **2026-01-12 Status:** ALL ASYNC CALLBACK PATTERNS MIGRATED TO RAII. Converted 10+ files from manual `new/delete` to `ui_queue_update<T>(std::unique_ptr)`. Also converted `gcode_geometry_builder.cpp` void* caches to `std::unique_ptr`. Zero manual `delete` statements remain in production code (only in test fixtures, which is acceptable).

### 4.1 Discovery - Find All RAII Violations

```bash
echo "=== Manual 'new' in widget files (should use RAII) ==="
grep -rn "\bnew \w\+\s*(" src/ui/ui_*.cpp | grep -v "make_unique\|placement"

echo ""
echo "=== Manual 'delete' (should use RAII wrapper) ==="
grep -rn "^\s*delete " src/ --include="*.cpp" | grep -v "lib/"

echo ""
echo "=== lv_malloc (should be zero in src/) ==="
grep -rn "lv_malloc\|lv_free" src/ --include="*.cpp" | grep -v "lib/"

echo ""
echo "=== Current RAII usage (good patterns) ==="
echo "lvgl_make_unique: $(grep -r "lvgl_make_unique" src/ --include="*.cpp" | wc -l)"
echo "std::make_unique: $(grep -r "std::make_unique" src/ --include="*.cpp" | wc -l)"
echo "std::unique_ptr: $(grep -r "std::unique_ptr" src/ --include="*.cpp" | wc -l)"
```

**Record baseline:**
- Manual `new`: _____ occurrences
- Manual `delete`: _____ occurrences
- `lv_malloc`: _____ occurrences (should be 0)

### 4.2 Reference: Correct RAII Patterns

**Gold standard files to reference:**
- `src/ui/ui_jog_pad.cpp` - Simple widget user_data
- `src/ui/ui_step_progress.cpp` - Complex nested allocations
- `src/ui/ui_temp_graph.cpp` - Standalone structure pattern

**Pattern 1: Widget user_data with DELETE callback**
```cpp
// Creation:
auto data_ptr = std::make_unique<MyWidgetData>();
// ... initialize data_ptr ...
lv_obj_set_user_data(obj, data_ptr.release());  // Transfer ownership
lv_obj_add_event_cb(obj, widget_delete_cb, LV_EVENT_DELETE, nullptr);

// DELETE callback:
static void widget_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    std::unique_ptr<MyWidgetData> data(
        static_cast<MyWidgetData*>(lv_obj_get_user_data(obj))
    );
    lv_obj_set_user_data(obj, nullptr);
    // data automatically freed via ~unique_ptr()
    // even if exception thrown in cleanup code above
}
```

**Pattern 2: LVGL-allocated memory (rare, use lvgl_make_unique)**
```cpp
// If you must use LVGL's allocator:
auto data = lvgl_make_unique<MyData>();
// ...
lv_obj_set_user_data(obj, data.release());

// Cleanup uses lvgl_unique_ptr for lv_free:
lvgl_unique_ptr<MyData> data(static_cast<MyData*>(lv_obj_get_user_data(obj)));
```

### 4.3 Files to Fix

#### 4.3.1 `src/ui/ui_hsv_picker.cpp`

- [ ] **Find violations:**
  ```bash
  grep -n "new \|delete " src/ui/ui_hsv_picker.cpp
  ```

- [ ] **Locate allocation** (search `new HsvPickerData`)
- [ ] **Locate cleanup** (search DELETE callback or `delete data`)
- [ ] **Convert allocation:**
  ```cpp
  // BEFORE:
  HsvPickerData* data = new HsvPickerData();

  // AFTER:
  auto data_ptr = std::make_unique<HsvPickerData>();
  ```

- [ ] **Convert cleanup callback:**
  ```cpp
  // BEFORE:
  delete data;

  // AFTER:
  std::unique_ptr<HsvPickerData> data(...);
  // automatic cleanup
  ```

- [ ] **Test:** HSV picker creates and destroys correctly

#### 4.3.2 `src/ui/ui_bed_mesh.cpp`

- [ ] **Find violations:**
  ```bash
  grep -n "new \|delete " src/ui/ui_bed_mesh.cpp
  ```

- [ ] **Note:** May already use `make_unique` for allocation but `delete` in cleanup
- [ ] **Convert cleanup to RAII**
- [ ] **Test:** Bed mesh panel opens/closes correctly

#### 4.3.3 `src/ui/ui_gradient_canvas.cpp`

- [ ] **Find violations**
- [ ] **Convert to RAII pattern**
- [ ] **Test:** Gradient widgets render correctly

#### 4.3.4 `src/ui/ui_temp_display.cpp`

- [ ] **Find violations**
- [ ] **Convert to RAII pattern**
- [ ] **Test:** Temperature displays update correctly

#### 4.3.5 `src/ui/ui_filament_path_canvas.cpp`

- [ ] **Find violations**
- [ ] **Note:** Uses registry pattern - may need special handling
- [ ] **Convert or document exception**
- [ ] **Test:** Filament path animations work

#### 4.3.6 Any Additional Files

Check discovery output for any files not listed above:
- [ ] File: _____________ Fixed: [ ]
- [ ] File: _____________ Fixed: [ ]
- [ ] File: _____________ Fixed: [ ]

### 4.4 Verification

```bash
# Re-run discovery - should be zero violations
echo "Manual 'new': $(grep -rn '\bnew \w\+\s*(' src/ui/ui_*.cpp | grep -v 'make_unique\|placement' | wc -l)"
echo "Manual 'delete': $(grep -rn '^\s*delete ' src/ --include='*.cpp' | grep -v lib/ | wc -l)"
echo "lv_malloc: $(grep -rn 'lv_malloc' src/ --include='*.cpp' | grep -v lib/ | wc -l)"

# Build
make -j

# Run UI tests
./build/bin/helix-screen --test -vv
```

- [ ] Manual `new` count: 0 (or documented exceptions)
- [ ] Manual `delete` count: 0 (or documented exceptions)
- [ ] `lv_malloc` count: 0
- [ ] Build succeeds
- [ ] All widgets function correctly

---

## 5. Priority 3: XML Design Token Migration

**Status:** [~] 80% Complete
**Estimated Time:** 1-2 hours remaining
**Risk if Skipped:** Maintenance burden, inconsistent theming

> **2025-12-18 Status:** Major migration completed. Only 16 hardcoded padding values remain (down from 85+ files). Most violations are in edge cases or intentional overrides.

### 5.1 Discovery - Current State

```bash
echo "=== Hardcoded padding (exclude 0) ==="
grep -rn 'style_pad[^=]*="[1-9]' ui_xml/ --include="*.xml" | wc -l

echo ""
echo "=== Hardcoded margins ==="
grep -rn 'style_margin[^=]*="[1-9]' ui_xml/ --include="*.xml" | wc -l

echo ""
echo "=== Hardcoded gaps ==="
grep -rn 'style_gap[^=]*="[1-9]' ui_xml/ --include="*.xml" | wc -l

echo ""
echo "=== Files with hardcoded spacing ==="
grep -l 'style_pad[^=]*="[1-9]' ui_xml/*.xml | wc -l

echo ""
echo "=== Available spacing tokens ==="
grep -E 'name="space_' ui_xml/globals.xml
```

**Record baseline:**
- Hardcoded padding: _____ occurrences
- Files affected: _____ files

### 5.2 Verify/Create Design Tokens

Check `ui_xml/globals.xml` has these tokens (add if missing):

```xml
<!-- Spacing scale -->
<int name="space_xs" value="4"/>
<int name="space_sm" value="8"/>
<int name="space_md" value="12"/>
<int name="space_lg" value="16"/>
<int name="space_xl" value="24"/>
<int name="space_xxl" value="32"/>

<!-- Responsive variants (if using responsive system) -->
<int name="space_xs_small" value="2"/>
<int name="space_xs_medium" value="4"/>
<int name="space_xs_large" value="6"/>
<!-- etc. -->
```

- [ ] Verify spacing tokens exist
- [ ] Add any missing tokens
- [ ] Document token mapping:
  - `4` → `#space_xs`
  - `8` → `#space_sm`
  - `12` → `#space_md`
  - `16` → `#space_lg`
  - `24` → `#space_xl`
  - `32` → `#space_xxl`

### 5.3 Migration Rules

**DO migrate:**
- `style_pad_all="12"` → `style_pad_all="#space_md"`
- `style_pad_left="8"` → `style_pad_left="#space_sm"`
- `style_pad_top="16"` → `style_pad_top="#space_lg"`
- `style_margin_*="N"` → `style_margin_*="#space_*"`
- `style_gap_row="8"` → `style_gap_row="#space_sm"`
- `style_gap_column="8"` → `style_gap_column="#space_sm"`

**DO NOT migrate:**
- `style_pad_all="0"` - Zero padding is intentional
- `width="100%"` - Percentage values
- Fixed widget dimensions (icons, buttons with specific sizes)
- Values that don't map to tokens (e.g., `style_pad_all="3"`)

### 5.4 File-by-File Migration

**Strategy:** Migrate in batches, test after each batch.

#### Batch 1: Core Panels
- [ ] `ui_xml/home_panel.xml`
- [ ] `ui_xml/controls_panel.xml`
- [ ] `ui_xml/print_status_panel.xml`
- [ ] `ui_xml/settings_panel.xml`
- [ ] **Test batch:** `./build/bin/helix-screen --test -p home`

#### Batch 2: Secondary Panels
- [ ] `ui_xml/motion_panel.xml`
- [ ] `ui_xml/extrusion_panel.xml`
- [ ] `ui_xml/filament_panel.xml`
- [ ] `ui_xml/advanced_panel.xml`
- [ ] **Test batch**

#### Batch 3: Calibration Panels
- [ ] `ui_xml/calibration_pid_panel.xml` (if exists)
- [ ] `ui_xml/calibration_zoffset_panel.xml` (if exists)
- [ ] `ui_xml/bed_mesh_panel.xml`
- [ ] `ui_xml/screws_tilt_panel.xml` (if exists)
- [ ] **Test batch**

#### Batch 4: Modals & Overlays
- [ ] All `ui_xml/*_modal.xml` files
- [ ] All `ui_xml/*_overlay.xml` files
- [ ] **Test batch**

#### Batch 5: Components & Remaining
- [ ] `ui_xml/navigation_bar.xml`
- [ ] `ui_xml/status_bar.xml`
- [ ] `ui_xml/header_bar.xml`
- [ ] All remaining `ui_xml/*.xml`
- [ ] **Test batch**

**Migration command for single file:**
```bash
FILE=ui_xml/home_panel.xml

# Show what will change
grep -n 'style_pad[^=]*="[1-9]' "$FILE"

# Manual replacement (or use sed carefully)
# style_pad_all="4" → style_pad_all="#space_xs"
# style_pad_all="8" → style_pad_all="#space_sm"
# style_pad_all="12" → style_pad_all="#space_md"
# style_pad_all="16" → style_pad_all="#space_lg"
# style_pad_all="24" → style_pad_all="#space_xl"

# Verify after
./build/bin/helix-screen --test -p home
```

### 5.5 Verification

```bash
# Count remaining hardcoded values
remaining=$(grep -rn 'style_pad[^=]*="[1-9]' ui_xml/ --include="*.xml" | wc -l)
echo "Remaining hardcoded padding: $remaining"

# Should be very low (only values that don't map to tokens)
# Document any remaining
grep -rn 'style_pad[^=]*="[1-9]' ui_xml/ --include="*.xml"
```

- [ ] All batches migrated
- [ ] Remaining violations documented/justified
- [ ] Visual inspection shows correct spacing
- [ ] No XML parse errors

---

## 6. Priority 4: Declarative UI Cleanup

**Status:** [~] 20% Complete (Triage Done)
**Estimated Time:** 12-16 hours remaining
**Risk if Skipped:** Architecture inconsistency, maintenance burden

> **2025-12-18 Status:** Actual counts lower than original estimate (~140 event handlers vs ~800 claimed). Many are legitimate (DELETE callbacks, dynamic widgets). Primary debt: 533 inline styles. Needs triage to identify true violations.

### 6.1 Discovery - Current Violations

```bash
echo "=== lv_obj_add_event_cb violations ==="
grep -rn "lv_obj_add_event_cb" src/ui/ui_*.cpp | wc -l

echo ""
echo "=== lv_label_set_text violations ==="
grep -rn "lv_label_set_text" src/ui/ui_*.cpp | wc -l

echo ""
echo "=== Imperative visibility control ==="
grep -rn "lv_obj_add_flag.*HIDDEN\|lv_obj_clear_flag.*HIDDEN" src/ui/ui_*.cpp | wc -l

echo ""
echo "=== Inline styling ==="
grep -rn "lv_obj_set_style_" src/ui/ui_*.cpp | wc -l

echo ""
echo "=== Top violating files ==="
for pattern in "lv_obj_add_event_cb" "lv_label_set_text" "lv_obj_set_style_"; do
  echo "--- $pattern ---"
  grep -rc "$pattern" src/ui/ui_*.cpp | sort -t: -k2 -rn | head -5
done
```

**Record baseline:**
- Event handlers: _____ occurrences
- Text updates: _____ occurrences
- Visibility toggles: _____ occurrences
- Inline styles: _____ occurrences

### 6.2 Triage Categories

**LEGITIMATE (Document, Don't Fix):**

| Pattern | Why Legitimate |
|---------|----------------|
| `LV_EVENT_DELETE` callbacks | RAII lifecycle management |
| Dynamic widget pool events | Runtime-created widgets with varying data |
| Custom widget internal handlers | HSV picker touch, chart interactions |
| Modal base class internals | Backdrop click, ESC key |
| Scroll events for lazy loading | Performance optimization |
| One-time initialization text | Version strings, static labels |
| High-frequency formatted updates | Temperature, time displays |

**VIOLATIONS (Should Fix):**

| Pattern | How to Fix |
|---------|------------|
| Static button clicks | Move to XML `<event_cb>` |
| State-driven text | Use subject binding |
| State-driven visibility | Use `<bind_flag_if_eq>` |
| Repeated inline styles | Use style classes or XML tokens |

### 6.3 File-by-File Audit

#### 6.3.1 `src/ui/ui_panel_settings.cpp`

```bash
# List all event handlers
grep -n "lv_obj_add_event_cb" src/ui/ui_panel_settings.cpp
```

For each occurrence:

| Line | Widget | Event | Classification | Action |
|------|--------|-------|----------------|--------|
| | | | | |
| | | | | |

- [ ] Audit all handlers
- [ ] Convert static handlers to XML
- [ ] Document legitimate uses with comments

#### 6.3.2 `src/ui/ui_panel_print_select.cpp`

- [ ] Audit handlers (many will be dynamic/legitimate)
- [ ] Convert eligible handlers
- [ ] Document patterns

#### 6.3.3 `src/ui/ui_keyboard_manager.cpp`

- [ ] Review 29-event loop
- [ ] If keys static: migrate to XML
- [ ] If keys dynamic: document as legitimate
- [ ] Consider refactor to reduce complexity

#### 6.3.4 Other Files

Process each file from discovery:
- [ ] `src/ui/ui_panel_home.cpp`
- [ ] `src/ui/ui_panel_controls.cpp`
- [ ] `src/ui/ui_panel_calibration_*.cpp`
- [ ] `src/ui/ui_panel_bed_mesh.cpp`
- [ ] `src/ui/ui_panel_ams.cpp`
- [ ] `src/ui/ui_wizard_*.cpp`
- [ ] Others from discovery: _____________

### 6.4 Create Visibility Subjects

**Pattern to apply:**

```cpp
// BEFORE (imperative):
void show_loading() {
    lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
}
void show_content() {
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
}

// AFTER (reactive):
// In header:
lv_subject_t is_loading_;

// In init_subjects():
lv_subject_init_int(&is_loading_, 0);
lv_xml_register_subject("panel_name_is_loading", &is_loading_);

// In XML:
<lv_obj name="content">
  <bind_flag_if_eq subject="panel_name_is_loading" flag="hidden" ref_value="1"/>
</lv_obj>
<lv_spinner name="spinner">
  <bind_flag_if_eq subject="panel_name_is_loading" flag="hidden" ref_value="0"/>
</lv_spinner>

// In C++:
void show_loading() { lv_subject_set_int(&is_loading_, 1); }
void show_content() { lv_subject_set_int(&is_loading_, 0); }
```

Files to apply this pattern:
- [ ] `src/ui/ui_panel_macros.cpp`
- [ ] `src/ui/ui_panel_spoolman.cpp`
- [ ] `src/ui/ui_panel_history_list.cpp`
- [ ] `src/ui/ui_nav_manager.cpp`
- [ ] Others: _____________

### 6.5 Verification

```bash
# Re-run discovery
echo "Event handlers: $(grep -rn 'lv_obj_add_event_cb' src/ui/ui_panel_*.cpp | wc -l)"
echo "Visibility toggles: $(grep -rn 'lv_obj_add_flag.*HIDDEN' src/ui/ui_panel_*.cpp | wc -l)"

# Target reductions:
# - Event handlers: <50% of original (rest documented)
# - Visibility: <50% of original (rest documented)
```

- [ ] Event handlers reduced significantly
- [ ] Remaining handlers documented
- [ ] Visibility toggles reduced
- [ ] All UI functionality preserved

---

## 7. Priority 5: Large File Refactoring

**Status:** [~] 33% Complete
**Estimated Time:** 10-16 hours remaining
**Risk if Skipped:** Maintainability, cognitive load

> **2025-12-18 Status:** `ui_panel_ams.cpp` reduced to 1,469 lines (under 1,500 target). Two files remain over limit: `ui_panel_print_status.cpp` (2,414 LOC), `ui_panel_print_select.cpp` (1,845 LOC).

### 7.1 Discovery - File Sizes

```bash
echo "=== Files over 2000 lines ==="
wc -l src/*.cpp | awk '$1 > 2000 {print}' | sort -rn

echo ""
echo "=== Files over 1500 lines ==="
wc -l src/*.cpp | awk '$1 > 1500 {print}' | sort -rn

echo ""
echo "=== Top 10 largest files ==="
wc -l src/*.cpp | sort -rn | head -10
```

**Record baseline:**
- Files >2000 lines: _____
- Files >1500 lines: _____

### 7.2 `src/ui/ui_panel_ams.cpp` Refactoring

**Current responsibilities (verify):**
- AMS state display
- Slot management UI
- Color picker integration
- Edit modal
- Filament path animations
- Tool selection

#### 7.2.1 Extract `AmsSlotEditor`

- [ ] Create `include/ui_ams_slot_editor.h`:
  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  #pragma once

  #include <lvgl.h>
  #include "ams_types.h"

  class AmsSlotEditor {
  public:
      using SaveCallback = std::function<void(const AmsSlotConfig&)>;

      void show(lv_obj_t* parent, const AmsSlot& slot, SaveCallback on_save);
      void hide();

  private:
      // Modal creation and form logic
  };
  ```

- [ ] Create `src/ui/ui_ams_slot_editor.cpp`
- [ ] Move slot editing modal logic
- [ ] Move form validation
- [ ] Update `ui_panel_ams.cpp` to use new class
- [ ] Test: Slot editing still works

#### 7.2.2 Extract `AmsColorPicker`

- [ ] Create `include/ui_ams_color_picker.h`
- [ ] Create `src/ui/ui_ams_color_picker.cpp`
- [ ] Move HSV picker integration
- [ ] Move color preview/selection logic
- [ ] Test: Color selection works

#### 7.2.3 Extract `AmsAnimationController`

- [ ] Create `include/ui_ams_animation.h`
- [ ] Create `src/ui/ui_ams_animation.cpp`
- [ ] Move filament path animation state machine
- [ ] Move animation timing logic
- [ ] Test: Animations display correctly

**Target:** `src/ui/ui_panel_ams.cpp` < 1500 lines

### 7.3 `src/ui/ui_panel_print_select.cpp` Refactoring

#### 7.3.1 Extract `FileCardRenderer`

- [ ] Create `include/ui_file_card.h`
- [ ] Create `src/ui/ui_file_card.cpp`
- [ ] Move card view creation logic
- [ ] Move thumbnail loading
- [ ] Test: Card view works

#### 7.3.2 Extract `FileListRenderer`

- [ ] Create `include/ui_file_list.h`
- [ ] Create `src/ui/ui_file_list.cpp`
- [ ] Move list item creation
- [ ] Move lazy loading logic
- [ ] Test: List view works

#### 7.3.3 Extract `PrintMetadataFormatter`

- [ ] Create `include/print_metadata.h`
- [ ] Create `src/print_metadata.cpp`
- [ ] Move time formatting utilities
- [ ] Move size formatting utilities
- [ ] Move filament calculation
- [ ] Test: Metadata displays correctly

**Target:** `src/ui/ui_panel_print_select.cpp` < 1500 lines

### 7.4 `src/ui/ui_panel_print_status.cpp` Refactoring

#### 7.4.1 Extract `LayerProgressView`

- [ ] Move layer tracking display logic
- [ ] Move layer progress calculations
- [ ] Test: Layer display works

#### 7.4.2 Extract `ExcludedObjectsPanel`

- [ ] Move object exclusion UI
- [ ] Move object highlighting logic
- [ ] Test: Exclusion feature works

**Target:** `src/ui/ui_panel_print_status.cpp` < 1500 lines

### 7.5 Verification

```bash
# Check file sizes after refactoring
echo "=== Target files ==="
wc -l src/ui/ui_panel_ams.cpp src/ui/ui_panel_print_select.cpp src/ui/ui_panel_print_status.cpp

echo ""
echo "=== New extracted files ==="
wc -l src/ui/ui_ams_*.cpp src/ui/ui_file_*.cpp src/print_metadata.cpp 2>/dev/null

echo ""
echo "=== Any files still over 1500? ==="
wc -l src/ui/ui_*.cpp | awk '$1 > 1500 {print "WARNING:", $0}'
```

- [ ] All target files < 1500 lines
- [ ] New files have SPDX headers
- [ ] Build succeeds
- [ ] All functionality preserved
- [ ] No regressions in UI

---

## 8. Priority 6: Documentation Updates

**Status:** [ ] Not Started
**Estimated Time:** 4-6 hours
**Risk if Skipped:** Developer confusion, repeated mistakes

### 8.1 Update CLAUDE.md

#### 8.1.1 Clarify Event Handler Rule

- [ ] Locate Rule 12/13
- [ ] Add exceptions section:

```markdown
**Rule 12/13 - XML event_cb:**

**REQUIRED** for static, named widgets in XML.

**ACCEPTABLE exceptions** for `lv_obj_add_event_cb()`:
- `LV_EVENT_DELETE` callbacks (RAII lifecycle)
- Modal/panel base class internals
- Custom widget internal handlers (e.g., touch for HSV picker)
- Dynamic widget pools (runtime-created with varying data)
- Scroll events for lazy loading

**NEVER** acceptable:
- Static buttons that have names in XML
- Anything that doesn't fall into exceptions above
```

#### 8.1.2 Add Timer Rule

- [ ] Add new Rule 17:

```markdown
**Rule 17 - Timer Lifecycle:**

All `lv_timer_create()` calls MUST have cleanup:
- In destructor if timer lifetime = object lifetime
- Self-delete in callback if one-shot
- Use `LvglTimerGuard` for RAII (preferred)

**NEVER** assume LVGL auto-frees timers.
```

### 8.2 Update ARCHITECTURE.md

#### 8.2.1 Add Timer Section

- [ ] Add to Memory Management section
- [ ] Include code examples for all timer patterns
- [ ] Reference `LvglTimerGuard`

#### 8.2.2 Add Reference Implementation Section

- [ ] List gold standard files:
  - RAII: `ui_jog_pad.cpp`, `ui_step_progress.cpp`
  - Declarative UI: `ui_panel_bed_mesh.cpp`
  - Backend: `wifi_backend.cpp`, `wifi_manager.cpp`

### 8.3 Create New Documentation

#### 8.3.1 `docs/DECLARATIVE_UI_PATTERNS.md`

- [ ] Create with:
  - When to use XML vs C++
  - Subject binding examples
  - Event callback migration guide
  - Visibility binding patterns

#### 8.3.2 `docs/WIDGET_DEVELOPMENT_GUIDE.md`

- [ ] Create with:
  - RAII requirements checklist
  - DELETE callback pattern
  - Timer management
  - Testing widgets

### 8.4 Verification

- [ ] All documentation reviewed for accuracy
- [ ] Code examples compile
- [ ] Cross-references work
- [ ] No broken links

---

## 9. Priority 7: Tooling & CI

**Status:** [ ] Not Started
**Estimated Time:** 8-12 hours
**Risk if Skipped:** Regressions, repeated violations

### 9.1 Pre-commit Hooks

#### 9.1.1 RAII Violation Check

- [ ] Add to `.githooks/pre-commit`:

```bash
#!/bin/bash
set -e

# Check for manual new/delete in UI code (excluding make_unique)
violations=$(git diff --cached --name-only | grep -E 'src/ui/ui_.*\.cpp$' | while read f; do
    git diff --cached "$f" | grep -E '^\+.*[^_]new\s+\w+\s*\(' | grep -v make_unique && echo "$f"
    git diff --cached "$f" | grep -E '^\+\s*delete\s+' && echo "$f"
done || true)

if [ -n "$violations" ]; then
    echo "ERROR: Manual new/delete in UI code. Use RAII wrappers!"
    echo "See: docs/ARCHITECTURE.md#raii-widget-management"
    exit 1
fi
```

#### 9.1.2 Hardcoded Spacing Warning

- [ ] Add warning (not error) for hardcoded spacing:

```bash
# Check for hardcoded spacing in XML (warning only)
violations=$(git diff --cached --name-only | grep -E 'ui_xml/.*\.xml$' | while read f; do
    git diff --cached "$f" | grep -E '^\+.*style_pad[^=]*="[1-9][0-9]*"' && echo "$f"
done || true)

if [ -n "$violations" ]; then
    echo "WARNING: Hardcoded spacing in XML. Consider design tokens."
    echo "See: ui_xml/globals.xml for available tokens"
fi
```

- [ ] Install hooks: `git config core.hooksPath .githooks`

### 9.2 CI Checks

#### 9.2.1 Create GitHub Actions Workflow

- [ ] Create `.github/workflows/code-quality.yml`:

```yaml
name: Code Quality

on: [push, pull_request]

jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Check RAII violations
        run: |
          count=$(grep -rn "^\s*delete " src/ui/ui_*.cpp | wc -l || echo 0)
          threshold=5
          if [ "$count" -gt "$threshold" ]; then
            echo "ERROR: Too many manual delete statements: $count (max: $threshold)"
            grep -rn "^\s*delete " src/ui/ui_*.cpp
            exit 1
          fi

      - name: Check file sizes
        run: |
          large=$(wc -l src/ui/ui_panel_*.cpp | awk '$1 > 2000 {print $2}')
          if [ -n "$large" ]; then
            echo "ERROR: Files exceed 2000 lines:"
            echo "$large"
            exit 1
          fi

      - name: Check hardcoded spacing
        run: |
          count=$(grep -rn 'style_pad[^=]*="[1-9][0-9]*"' ui_xml/ | wc -l || echo 0)
          threshold=30
          if [ "$count" -gt "$threshold" ]; then
            echo "WARNING: Hardcoded spacing count: $count (threshold: $threshold)"
          fi
```

### 9.3 Makefile Lint Target

- [ ] Add to `Makefile`:

```makefile
.PHONY: lint
lint:
	@echo "=== Checking RAII violations ==="
	@count=$$(grep -rn "^\s*delete " src/ui/ui_*.cpp 2>/dev/null | wc -l); \
	if [ "$$count" -gt 5 ]; then \
		echo "ERROR: $$count manual delete statements found"; \
		exit 1; \
	fi
	@echo "=== Checking file sizes ==="
	@wc -l src/ui/ui_panel_*.cpp | awk '$$1 > 2000 {print "WARNING: " $$0}'
	@echo "=== Lint passed ==="
```

### 9.4 Verification

- [ ] Pre-commit hooks work locally
- [ ] `make lint` passes
- [ ] CI workflow added
- [ ] Test PR triggers CI correctly

---

## Appendix: Search Patterns

### Memory Safety Patterns

```bash
# Manual allocation (violations)
grep -rn "\bnew \w\+\s*(" src/ui/ui_*.cpp | grep -v "make_unique\|placement"

# Manual deallocation (violations)
grep -rn "^\s*delete " src/ --include="*.cpp" | grep -v lib/

# LVGL allocation (should be zero in src/)
grep -rn "lv_malloc\|lv_free" src/ --include="*.cpp" | grep -v lib/

# Timer creates
grep -rn "lv_timer_create" src/ --include="*.cpp"

# Timer deletes
grep -rn "lv_timer_del\|lv_timer_delete" src/ --include="*.cpp"

# Good RAII patterns
grep -rn "lvgl_make_unique\|std::make_unique\|std::unique_ptr" src/ --include="*.cpp"
```

### Declarative UI Patterns

```bash
# Event handlers
grep -rn "lv_obj_add_event_cb" src/ui/ui_*.cpp

# Text updates
grep -rn "lv_label_set_text\|lv_textarea_set_text" src/ui/ui_*.cpp

# Visibility control
grep -rn "lv_obj_add_flag.*HIDDEN\|lv_obj_clear_flag.*HIDDEN" src/ui/ui_*.cpp

# Inline styling
grep -rn "lv_obj_set_style_" src/ui/ui_*.cpp

# Color literals
grep -rn "lv_color_hex\|lv_color_make" src/ui/ui_*.cpp | grep -v "theme\|parse"
```

### Design Token Patterns

```bash
# Hardcoded spacing in XML
grep -rn 'style_pad[^=]*="[1-9]' ui_xml/ --include="*.xml"
grep -rn 'style_margin[^=]*="[1-9]' ui_xml/ --include="*.xml"
grep -rn 'style_gap[^=]*="[1-9]' ui_xml/ --include="*.xml"

# Token usage (good)
grep -rn 'style_pad[^=]*="#' ui_xml/ --include="*.xml"
```

### Code Organization Patterns

```bash
# File sizes
wc -l src/*.cpp | sort -rn

# Backend includes in UI (potential violation)
grep -rn "#include.*backend" src/ui/ui_*.cpp

# Circular dependency check (manual review needed)
grep -h "#include" src/ui/ui_panel_*.cpp | sort | uniq -c | sort -rn
```

---

## Appendix: Verification Commands

### Full Audit Baseline

Run at start of each session:

```bash
#!/bin/bash
echo "========================================"
echo "HelixScreen Audit Baseline"
echo "Date: $(date)"
echo "========================================"

echo ""
echo "=== Memory Safety ==="
echo "Manual delete: $(grep -rn '^\s*delete ' src/ui/ui_*.cpp 2>/dev/null | wc -l)"
echo "lv_malloc in src: $(grep -rn 'lv_malloc' src/ --include='*.cpp' 2>/dev/null | grep -v lib/ | wc -l)"
echo "Timer creates: $(grep -rn 'lv_timer_create' src/ --include='*.cpp' 2>/dev/null | wc -l)"
echo "Timer deletes: $(grep -rn 'lv_timer_del' src/ --include='*.cpp' 2>/dev/null | wc -l)"

echo ""
echo "=== Declarative UI ==="
echo "Event handlers: $(grep -rn 'lv_obj_add_event_cb' src/ui/ui_*.cpp 2>/dev/null | wc -l)"
echo "Text updates: $(grep -rn 'lv_label_set_text' src/ui/ui_*.cpp 2>/dev/null | wc -l)"
echo "Visibility toggles: $(grep -rn 'lv_obj_add_flag.*HIDDEN' src/ui/ui_*.cpp 2>/dev/null | wc -l)"
echo "Inline styles: $(grep -rn 'lv_obj_set_style_' src/ui/ui_*.cpp 2>/dev/null | wc -l)"

echo ""
echo "=== Design Tokens ==="
echo "Hardcoded padding: $(grep -rn 'style_pad[^=]*=\"[1-9]' ui_xml/ 2>/dev/null | wc -l)"

echo ""
echo "=== Code Size ==="
echo "Files >2000 lines:"
wc -l src/ui/ui_panel_*.cpp 2>/dev/null | awk '$1 > 2000 {print "  " $0}' | head -5
echo "Files >1500 lines:"
wc -l src/ui/ui_panel_*.cpp 2>/dev/null | awk '$1 > 1500 && $1 <= 2000 {print "  " $0}' | head -5

echo ""
echo "========================================"
```

### Post-Fix Verification

```bash
# Build
make clean && make -j

# Quick functional test
./build/bin/helix-screen --test -p home -vv &
sleep 5
kill %1 2>/dev/null

# Run audit baseline again, compare numbers
```

---

## Progress Summary

Update this table as work progresses:

| Priority | Status | Progress | Completed Date | Notes |
|----------|--------|----------|----------------|-------|
| P1: Critical Safety | [x] | 100% | | Timer leak fix done; `LvglTimerGuard` RAII wrapper created; all timer deletions guarded |
| P2: RAII Compliance | [~] | 15% | | `ui_hsv_picker.cpp` migrated; 3 files remaining (8 deletes) |
| P3: XML Tokens | [~] | 80% | | Down from 85+ files to 16 occurrences |
| P4: Declarative UI | [~] | 20% | | 140 event handlers (many legitimate); 533 inline styles |
| P5: File Splitting | [~] | 33% | | `ui_panel_ams.cpp` now <1500; 2 files remain over limit |
| P6: Documentation | [~] | 50% | | Timer docs + doc consolidation in progress |
| P7: Tooling | [ ] | 0% | | |

---

## Changelog

| Date | Author | Changes |
|------|--------|---------|
| 2026-01-12 | Claude | P1 COMPLETE: All timer deletions now guarded with lv_is_initialized(); LvglTimerGuard RAII wrapper created |
| 2025-12-18 | Claude | Audit & rebaseline: Updated all metrics, corrected progress percentages, renamed to Technical Debt Tracker |
| 2025-12-16 | Claude | P1 partial: HomePanel timer fix, LvglTimerGuard RAII wrapper created (not yet adopted) |
| 2025-12-16 | Claude | P2 started: RAII fix in ui_hsv_picker.cpp |
| 2025-12-16 | Claude | P3 major progress: XML design tokens migrated (16 remaining from 85+) |
| 2025-12-16 | Claude | P5 partial: ui_panel_ams.cpp reduced below 1500 lines |
| 2025-12-16 | Claude | P6 in progress: Documentation consolidation (ARCHITECTURE.md + QUICK_REFERENCE.md) |
| 2024-12-16 | Claude | Initial plan created from comprehensive 5-agent audit |

