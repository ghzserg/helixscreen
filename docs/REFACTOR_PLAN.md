# Codebase Refactor Plan

This document outlines remaining refactoring work identified during the December 2025 codebase audit. The audit analyzed RAII patterns, design tokens, event callbacks, architecture, and code style.

## Status

- **Phase 1: COMPLETED** (commit df72783) - SubscriptionGuard RAII wrapper, AMS backend migration, initial design token fixes
- **Phase 2-5: TODO** - Outlined below

---

## Phase 2: Remaining RAII Migrations (HIGH PRIORITY)

### 2.1 Global Observers Without Cleanup

These files add observers via `lv_subject_add_observer()` without storing handles for cleanup. They should store observer handles as static `ObserverGuard` instances.

| File | Lines | Issue |
|------|-------|-------|
| `src/ui_status_bar.cpp` | 260, 266, 272 | 3 observers for network, printer connection, klippy state |
| `src/ui_nav.cpp` | 220, 291 | Active panel observer, connection state observer |
| `src/ui_notification.cpp` | 180 | Notification subject observer |
| `src/ui_text_input.cpp` | 168 | Widget-attached observer (may need widget cleanup callback) |

**Fix Pattern:**
```cpp
// Before:
lv_subject_add_observer(subject, callback, nullptr);

// After:
static ObserverGuard s_my_observer;
s_my_observer = ObserverGuard(subject, callback, nullptr);
```

### 2.2 Recommended New RAII Wrappers

Consider creating these additional RAII wrappers for common patterns:

1. **TimerGuard** - For `lv_timer_t*` handles (auto-delete on destruction)
2. **AnimGuard** - For `lv_anim_t` handles (auto-stop on destruction)

---

## Phase 3: Design Token Migration (HIGH PRIORITY)

### 3.1 Color Token Violations

**41 instances** of `lv_color_hex()` in 18 files should use `ui_theme_get_color()` or `ui_theme_parse_color()`.

#### Files to Fix (priority order):

| File | Violations | Colors Used |
|------|------------|-------------|
| `src/ui_step_progress.cpp` | 6 | Line colors, completion state |
| `src/ui_temp_graph.cpp` | 4 | Graph line colors |
| `src/ui_bed_mesh.cpp` | 3+ | Mesh visualization colors |
| `src/ui_keyboard.cpp` | 4+ | Key highlights, alternatives popup |
| `src/ui_panel_print_select.cpp` | 3+ | Selection highlight, card borders |
| `src/ui_panel_history_dashboard.cpp` | 2+ | Chart colors |
| `src/ui_panel_screws_tilt.cpp` | 2+ | Screw indicator colors |
| `src/ui_wizard_printer_identify.cpp` | 2+ | Selection states |
| `src/ui_spool_canvas.cpp` | 2+ | Spool visualization |
| `src/ui_panel_temp_control.cpp` | 2+ | Temperature indicators |
| `src/ui_panel_ams.cpp` | 2+ | Slot state colors |
| `src/ui_filament_path_canvas.cpp` | 2+ | Path visualization |
| `src/ui_ams_slot.cpp` | 2+ | Slot state colors |
| `src/ui_jog_pad.cpp` | 1+ | Jog button states |
| `src/ui_card.cpp` | 1 | Card styling |
| `src/ui_theme.cpp` | N/A | Defines theme colors (acceptable) |

**Note:** `ui_fatal_error.cpp` is intentionally exempt (bootstrap code).

#### Required New Color Tokens

Add to `ui_xml/globals.xml`:

```xml
<!-- Graph/Visualization colors -->
<color name="graph_line_primary" value="#AD2724"/>
<color name="graph_line_secondary" value="#D4A84B"/>
<color name="graph_line_target" value="#2196F3"/>

<!-- Mesh visualization gradient -->
<color name="mesh_low" value="#2196F3"/>
<color name="mesh_mid" value="#4CAF50"/>
<color name="mesh_high" value="#FF4444"/>

<!-- Selection/highlight states -->
<color name="selection_highlight_light" value="#E3F2FD"/>
<color name="selection_highlight_dark" value="#1E3A5F"/>
```

### 3.2 Padding/Spacing Token Violations

**445+ instances** of hardcoded numeric padding in XML files. Priority files:

- `ui_xml/wizard_*.xml` - Wizard steps with hardcoded padding
- `ui_xml/settings_panel.xml` - Settings rows
- `ui_xml/*_modal.xml` - Modal layouts

**Fix Pattern:**
```xml
<!-- Before -->
<lv_obj style_pad_top="6" style_pad_bottom="20"/>

<!-- After -->
<lv_obj style_pad_top="#space_xs" style_pad_bottom="#space_xl"/>
```

---

## Phase 4: Event Callback Migration (MEDIUM PRIORITY)

### 4.1 Rule 12 Violations

**200+ instances** of `lv_obj_add_event_cb()` in C++ that should be XML `<event_cb>` declarations.

#### High-Priority Files:

| File | Violations | Description |
|------|------------|-------------|
| `src/ui_panel_settings.cpp` | 21 | Toggle switches, button clicks |
| `src/ui_panel_print_select.cpp` | 16 | Scroll events, card clicks |
| `src/ui_keyboard.cpp` | 11 | Key events, long-press handling |
| `src/ui_panel_print_status.cpp` | 8 | Card clicks, control buttons |
| `src/ui_bed_mesh.cpp` | 7 | Custom draw, touch events |
| `src/ui_modal_base.cpp` | 4 | Backdrop, ESC key, buttons |
| `src/ui_panel_ams.cpp` | 5 | Slot clicks, context menu |

#### Acceptable Exceptions (do NOT migrate):

- `LV_EVENT_DRAW_POST` - Custom draw handlers for programmatic widgets
- `LV_EVENT_DELETE` - Cleanup handlers for dynamically created widgets
- Events on programmatically-created widgets (pools, dynamic lists)
- Runtime-conditional event wiring

**Migration Pattern:**
```cpp
// Before (in C++):
lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, this);

// After:
// 1. In XML:
<lv_btn name="my_btn">
  <event_cb trigger="clicked" callback="on_my_btn_click"/>
</lv_btn>

// 2. In C++ init:
lv_xml_register_event_cb(nullptr, "on_my_btn_click", on_click_static);
```

---

## Phase 5: Architecture Refactoring (MEDIUM PRIORITY)

### 5.1 Function-to-Class Migration

These global function-based UI systems should become class-based managers:

| System | File | Global Variables | Recommended Class |
|--------|------|-----------------|-------------------|
| Keyboard | `src/ui_keyboard.cpp` | 15+ globals (g_keyboard, g_mode, etc.) | `KeyboardManager` |
| Navigation | `src/ui_nav.cpp` | 8+ globals (panel_stack, active_panel, etc.) | `NavigationManager` |
| Modal | `src/ui_modal.cpp` | 6+ globals (modal_stack, subjects) | `ModalManager` |
| Toast | `src/ui_toast.cpp` | 5+ globals (active_toast, timer) | `ToastManager` |
| Status Bar | `src/ui_status_bar.cpp` | Multiple static subjects | `StatusBarManager` |

**Benefits:**
- Encapsulated state (no global pollution)
- Proper RAII lifecycle
- Testable (can mock/inject)
- Clear initialization order

**Reference Implementation:** See `HomePanel`, `PrintStatusPanel`, `TempControlPanel` for the correct PanelBase + ObserverGuard pattern.

### 5.2 Refactoring Order

1. **KeyboardManager** - Most complex, most global state
2. **NavigationManager** + **ModalManager** - Interconnected
3. **ToastManager** + **StatusBarManager** - Simpler

---

## Phase 6: Code Style Cleanup (LOW PRIORITY)

### 6.1 Copyright Header Cleanup

**133 files** have verbose 20-line GPL boilerplate in addition to SPDX headers. Per CLAUDE.md Rule 8, only the SPDX line is needed:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
```

**Affected directories:**
- `src/` - 59 files
- `include/` - 74 files

**Automation Script:**
```bash
#!/bin/bash
# Strip verbose GPL boilerplate, keep only SPDX
for f in src/*.cpp include/*.h; do
  if head -1 "$f" | grep -q "^// SPDX"; then
    # Keep first line (SPDX) and everything after the closing */
    (head -1 "$f"; sed -n '/^\*\//,$p' "$f" | tail -n +2) > "$f.tmp"
    mv "$f.tmp" "$f"
  fi
done
```

### 6.2 Minor Logging Fix

| File | Line | Issue |
|------|------|-------|
| `src/config.cpp` | 124, 153 | `std::endl` → `'\n'` |

---

## Prioritization Summary

| Phase | Priority | Effort | Impact |
|-------|----------|--------|--------|
| 2. RAII Migrations | HIGH | 1-2 hours | Prevents memory leaks, use-after-free |
| 3. Design Tokens | HIGH | 1-2 days | Enables proper theming, consistency |
| 4. Event Callbacks | MEDIUM | 3-5 days | Cleaner architecture, XML-driven UI |
| 5. Architecture | MEDIUM | 1-2 weeks | Testability, maintainability |
| 6. Code Style | LOW | 2-4 hours | Consistency, reduced noise |

---

## Testing Checklist

After each phase, verify:

- [ ] `make -j` builds successfully
- [ ] `./build/bin/run_tests` passes (note: some pre-existing flaky tests)
- [ ] `./build/bin/helix-screen --test -p home` runs and displays correctly
- [ ] Light/dark theme toggle works (`settings` panel)
- [ ] AMS panel shows slot states correctly (if AMS backend changes)

---

## References

- `docs/ARCHITECTURE.md` - System design patterns
- `docs/LVGL9_XML_GUIDE.md` - XML component reference
- `CLAUDE.md` - Project coding standards
- `include/ui_observer_guard.h` - ObserverGuard implementation
- `include/ui_subscription_guard.h` - SubscriptionGuard implementation (Phase 1)
