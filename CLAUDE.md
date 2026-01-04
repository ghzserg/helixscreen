# CLAUDE.md

## Quick Start

**HelixScreen**: LVGL 9.4 touchscreen UI for Klipper 3D printers. Pattern: XML → Subjects → C++.

```bash
make -j                              # Build (auto-detects cores - NEVER -j4/-j8)
./build/bin/helix-screen --test -vv  # Mock printer + DEBUG logs
# ALWAYS use verbosity: -v=INFO, -vv=DEBUG, -vvv=TRACE (default=WARN)

make test-run                        # Parallel tests
./build/bin/helix-tests "[tag]"      # Specific tests
make pi-test                         # Build on thelio + deploy + run
```

**Panels:** home, controls, motion, nozzle-temp, bed-temp, extrusion, filament, settings, advanced, print-select
**Screenshots:** Press 'S' in UI, or `./scripts/screenshot.sh helix-screen output-name [panel]`

---

## Lazy Docs (load when needed)

| Doc | When |
|-----|------|
| `docs/LVGL9_XML_GUIDE.md` | XML layouts, observer cleanup |
| `docs/ENVIRONMENT_VARIABLES.md` | Runtime env, mock config |
| `docs/BUILD_SYSTEM.md` | Build, Makefile |
| `docs/MOONRAKER_SECURITY_REVIEW.md` | Moonraker/security |

---

## Code Standards

| Rule | ❌ WRONG | ✅ CORRECT |
|------|----------|-----------|
| **spdlog only** | `printf()`, `cout`, `LV_LOG_*` | `spdlog::info("temp: {}", t)` |
| **SPDX headers** | 20-line GPL boilerplate | `// SPDX-License-Identifier: GPL-3.0-or-later` |
| **RAII widgets** | `lv_malloc()` / `lv_free()` | `lvgl_make_unique<T>()` + `release()` |
| **Class-based** | `ui_panel_*_init()` functions | Classes: `MotionPanel`, `WiFiManager` |
| **Icon sync** | Add icon, forget fonts | codepoints.h + `make regen-fonts` + rebuild |
| **Formatting** | Manual formatting | Let pre-commit hook (clang-format) fix |
| **No auto-mock** | `if(!start()) return Mock()` | Check `RuntimeConfig::should_mock_*()` |

**ALWAYS:** Search the SAME FILE you're editing for similar patterns before implementing.

---

## CRITICAL RULES - Declarative UI

**DATA in C++, APPEARANCE in XML, Subjects connect them.**

| # | Rule | ❌ NEVER | ✅ ALWAYS |
|---|------|----------|----------|
| 1 | **NO lv_obj_add_event_cb()** | `lv_obj_add_event_cb(btn, cb)` | XML `<event_cb trigger="clicked" callback="name"/>` + `lv_xml_register_event_cb()` |
| 2 | **NO imperative visibility** | `lv_obj_add_flag(obj, HIDDEN)` | XML `<bind_flag_if_eq subject="state" flag="hidden" ref_value="0"/>` |
| 3 | **NO lv_label_set_text** | `lv_label_set_text(lbl, val)` | Subject binding: `<text_body bind_text="my_subject"/>` |
| 4 | **NO C++ styling** | `lv_obj_set_style_bg_color()` | XML: `style_bg_color="#card_bg"` |
| 5 | **NO manual LVGL cleanup** | `lv_display_delete()`, `lv_group_delete()` | Just `lv_deinit()` - handles everything |
| 6 | **bind_style priority** | `style_bg_color` + `bind_style` | Inline attrs override - use TWO bind_styles |

**Exceptions:** DELETE cleanup, widget pool recycling, chart data, animations

---

## Design Tokens (MANDATORY)

| Category | ❌ WRONG | ✅ CORRECT |
|----------|----------|-----------|
| **Colors** | `lv_color_hex(0xE0E0E0)` | `ui_theme_get_color("card_bg")` |
| **Spacing** | `style_pad_all="12"` | `style_pad_all="#space_md"` |
| **Typography** | `<lv_label style_text_font="...">` | `<text_heading>`, `<text_body>`, `<text_small>` |

```cpp
ui_theme_get_color("card_bg");     // ✅ Token lookup (handles light/dark)
ui_theme_parse_color("#FF4444");   // ✅ Hex string only
// ui_theme_parse_color("#card_bg") → ❌ WRONG - doesn't lookup tokens!
```

**Macros:** `UI_COLOR_TEXT_PRIMARY`, `UI_FONT_SMALL`. Reference: `ui_icon.cpp`

---

## LVGL Lifecycle & Threading

### Threading
WebSocket/libhv callbacks = background thread. **NEVER** call `lv_subject_set_*()` directly.
Use `ui_async_call()` from `ui_update_queue.h`. Pattern: `printer_state.cpp` `set_*_internal()`

### Cleanup
- **Observer cleanup:** Save & remove observers BEFORE freeing struct with subjects (see `docs/LVGL9_XML_GUIDE.md`)
- **Shutdown guard:** `Application::shutdown()` needs `m_shutdown_complete` flag against double-call

### Deferred Dependencies
When `set_X()` updates member, also update child objects that cached old value.
Ex: `PrintSelectPanel::set_api()` must call `file_provider_->set_api()`

---

## Patterns

| Pattern | Key Point |
|---------|-----------|
| Subject init order | Register components → init subjects → create XML |
| Component tags | Always `name="..."` |
| Widget lookup | `lv_obj_find_by_name()` not `lv_obj_get_child()` |
| Image scaling | `lv_obj_update_layout()` before scaling |
| Overlays | `ui_nav_push_overlay()`/`ui_nav_go_back()` |
| LVGL API | Never `_lv_*()` private interfaces |
| Docs | Doxygen `@brief`/`@param`/`@return` |

---

## XML Gotchas

1. `hidden="true"` not `flag_hidden="true"`
2. Conditional bindings = child elements: `<lv_obj-bind_flag_if_eq>` not attributes
3. Three flex: `style_flex_main_place` + `style_flex_cross_place` + `style_flex_track_place`
4. No subjects in `globals.xml`
5. Component name = filename: `foo.xml` → `foo`
6. **9.4 API:** `lv_xml_register_component_from_file()`, `lv_xml_register_widget()`, `<event_cb trigger="clicked">`
7. **Align values:** `left_mid`, `right_mid`, `top_left`, `top_mid`, `top_right`, `bottom_left`, `bottom_mid`, `bottom_right`, `center`

---

## Debugging

**NEVER debug without flags!** Use `-vv` minimum.
Trust debug output. Impossible values = bug is UPSTREAM. Ask "what ELSE?" not "did first fix work?"

---

## MAJOR Criteria

Paths: PrinterState, WebSocket/threading, shutdown, DisplayManager, XML processing
Worktree: `git worktree add ../helixscreen-<feature> origin/main`
