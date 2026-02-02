# Plan: DRY Refactoring

## Problem

Codebase audit identified ~810 lines of duplicated code across 6 functional areas:
- UI panels (event trampolines, lazy overlay creation, widget lookups)
- State management (subject initialization boilerplate)
- API layer (error construction, HTTP error handling, JSON extraction)
- Temperature formatting (inconsistent patterns, duplicated display logic)
- Observer patterns (existing bundles underutilized)

## Solution

12 incremental refactoring chunks using TDD, worktrees, and code review. Each chunk is independently committable and testable.

## Progress

| # | Chunk | Status | Commit |
|---|-------|--------|--------|
| 1 | Temperature Display Helper | ✅ | c9d33cd3 |
| 2 | MoonrakerError Factories | ✅ | 40d8ba07 |
| 3 | HTTP Error Handler | ✅ | 95de214d |
| 4 | Trampoline Macro | ✅ | 8c5cef96 |
| 5 | Lazy Overlay Template | ✅ | 2b1c29c9 |
| 6 | Subject Init Macro | ✅ | 14fbcadb |
| 7 | Widget Lookup Macro | ✅ | cb233928 |
| 8 | Temp Formatting Consistency | ✅ | 0ee54af5 |
| 9 | Cleanup RAII Helper | ✅ | 17bbe771 |
| 10 | Observer Bundle Migration | ✅ | ff54633e |
| 11 | JSON Helper Move | ✅ | 3207dff1 |
| 12 | Macro Button Loop | ✅ | dad5577f |

**Legend:** ⬜ Pending | 🔄 In Progress | ✅ Complete

---

## Chunks

### Chunk 1: Temperature Display Helper (~80 LOC)

**Problem:** Same current/target/percentage/status calc repeated in 4+ panels.

**Tests:** `tests/test_format_utils.cpp`
```cpp
TEST_CASE("format_heater_display", "[format][temperature]") {
    SECTION("cold heater") { /* 25°C, no target -> "25°C", "Off", 0% */ }
    SECTION("heating") { /* 150/200°C -> "150 / 200°C", "Heating...", 75% */ }
    SECTION("at temp") { /* 198/200°C -> "Ready", 99% */ }
    SECTION("clamp pct") { /* over target -> 100% */ }
}
```

**Implementation:** `format_utils.h` - `HeaterDisplayResult format_heater_display(int current_centi, int target_centi)`

**Migration:**
- `ui_panel_controls.cpp:474-534`
- `ui_panel_filament.cpp`
- `ui_panel_extrusion.cpp:272-293`

---

### Chunk 2: MoonrakerError Factories (~100 LOC)

**Problem:** 50+ instances of identical error construction.

**Tests:** `tests/test_moonraker_api.cpp`
```cpp
TEST_CASE("MoonrakerError factories", "[api][error]") {
    SECTION("report_error sets fields") { /* type, method, message */ }
    SECTION("null callback safe") { /* no crash */ }
}
```

**Implementation:** `moonraker_api_internal.h`
```cpp
inline void report_error(ErrorCallback on_error, MoonrakerErrorType type,
                         std::string_view method, std::string_view message, int code = 0);
```

**Migration:** `moonraker_api_files.cpp`, `moonraker_api_motion.cpp`, `moonraker_api_config.cpp`

---

### Chunk 3: HTTP Error Handler (~60 LOC)

**Problem:** Same response handling in 6 HTTP methods.

**Tests:**
```cpp
TEST_CASE("handle_http_response", "[api][http]") {
    SECTION("null -> CONNECTION_LOST") {}
    SECTION("404 -> FILE_NOT_FOUND") {}
    SECTION("200 -> true, no error") {}
}
```

**Implementation:** `moonraker_api_internal.h`
```cpp
inline bool handle_http_response(const HttpResponsePtr& resp, std::string_view method,
                                 ErrorCallback on_error, int expected = 200);
```

**Migration:** `moonraker_api_files.cpp` (download_file, download_file_partial, download_thumbnail, upload_*)

---

### Chunk 4: Trampoline Macro (~150 LOC)

**Problem:** 46+ identical 4-line callback trampolines.

**Implementation:** `include/ui/ui_event_helpers.h`
```cpp
#define PANEL_TRAMPOLINE(PanelClass, getter_func, method_name) \
void PanelClass::on_##method_name(lv_event_t* e) { \
    LVGL_SAFE_EVENT_CB_BEGIN("[" #PanelClass "] on_" #method_name); \
    (void)e; \
    getter_func().handle_##method_name(); \
    LVGL_SAFE_EVENT_CB_END(); \
}
```

**Migration:**
- `ui_panel_controls.cpp:1374-1607` (30 trampolines)
- `ui_panel_home.cpp:859-916` (8 trampolines)
- `ui_panel_filament.cpp` (8+ trampolines)

---

### Chunk 5: Lazy Overlay Template (~80 LOC)

**Problem:** Same 12-line init pattern repeated 8+ times.

**Implementation:** `include/ui/ui_panel_helpers.h`
```cpp
template<typename PanelT>
bool lazy_push_overlay(lv_obj_t*& cache, PanelT& (*getter)(), lv_obj_t* parent,
                       const char* error_msg = "Failed to create overlay");
```

**Migration:**
- `ui_panel_controls.cpp:841-984` (5 overlays)
- `ui_panel_home.cpp:613-674` (3 overlays)

---

### Chunk 6: Subject Init Macro (~100 LOC)

**Problem:** init/register/xml_register triplets repeated 127 times.

**Implementation:** `include/state/subject_macros.h`
```cpp
#define INIT_SUBJECT_INT(name, default_val, subjects, register_xml) \
    lv_subject_init_int(&name##_, default_val); \
    subjects.register_subject(&name##_); \
    if (register_xml) lv_xml_register_subject(NULL, #name, &name##_)
```

**Migration:** All 13 `printer_*_state.cpp` files

---

### Chunk 7: Widget Lookup Macro (~50 LOC)

**Problem:** lookup + null-check + warn repeated 74 times.

**Implementation:** `include/ui/ui_widget_helpers.h`
```cpp
#define FIND_WIDGET(var, parent, name) \
    var = lv_obj_find_by_name(parent, name); \
    if (!var) spdlog::warn("[{}] Widget '{}' not found", get_name(), name)
```

**Migration:** `ui_panel_home.cpp`, `ui_panel_controls.cpp`, `ui_panel_motion.cpp`

---

### Chunk 8: Temp Formatting Consistency (~30 LOC)

**Problem:** 25+ snprintf calls with inconsistent `°C` formatting.

**Implementation:** `ui_temperature_utils.h`
```cpp
void format_temp_single(char* buf, size_t size, int temp_c);
void format_temp_pair(char* buf, size_t size, int current_c, int target_c);
```

**Migration:** Audit and replace all `"%d°C"`, `"%d °C"`, `"%dC"` patterns

---

### Chunk 9: Cleanup RAII Helper (~40 LOC)

**Problem:** if-delete-null pattern repeated 7+ times per panel.

**Implementation:** `include/ui/ui_cleanup_helpers.h`
```cpp
inline void safe_delete_obj(lv_obj_t*& obj);
inline void safe_delete_timer(lv_timer_t*& timer);
```

**Migration:** `ui_panel_controls.cpp:65-105`, `ui_panel_home.cpp`, `ui_panel_print_select.cpp`

---

### Chunk 10: Observer Bundle Migration (~60 LOC)

**Problem:** `TemperatureObserverBundle<T>` exists but underutilized.

**Implementation:** None - migrate to existing bundle.

**Migration:**
- `ui_panel_filament.cpp:100-135` (4 `observe_int_async` -> bundle)
- `ui_panel_extrusion.cpp`

---

### Chunk 11: JSON Helper Move (~20 LOC)

**Problem:** `json_number_or<T>()` exists in history.cpp but not shared.

**Implementation:** Move to `moonraker_api_internal.h`
```cpp
template <typename T>
inline T json_number_or(const nlohmann::json& j, const char* key, T default_val);
```

**Migration:** `moonraker_api_history.cpp`, `moonraker_api_print.cpp`, `moonraker_api_advanced.cpp`

---

### Chunk 12: Macro Button Loop (~40 LOC)

**Problem:** Button 1-4 update logic 80% identical.

**Implementation:** Refactor to array + loop in `ui_panel_controls.cpp:550-620`

---

## Workflow

For each chunk:
1. Create worktree: `git worktree add ../dry-chunk-N -b dry/chunk-N`
2. Write failing tests
3. Implement minimal code to pass
4. Migrate existing code
5. `make -j && make test-run`
6. Commit: `refactor(dry): [description]`
7. Code review via agent
8. Merge to main

## Verification

```bash
make -j                              # Build
make test-run                        # Tests
./build/bin/helix-screen --test -vv  # Manual
```

---

## Resume Prompt

Copy this to continue in a new session:

```
Continue DRY refactoring from docs/plans/2026-01-28-dry-refactoring.md

Check the Progress table to see which chunks are done. For the next pending chunk:
1. Use /strict-execute with worktree isolation
2. Write failing tests first
3. Implement, migrate, commit
4. Code review
5. Merge and update the Progress table

Current status: Planning complete, ready to start Chunk 1.
```
