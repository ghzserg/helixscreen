# CLAUDE.md

## Verbosity Flags (ALWAYS USE!)

**NEVER debug without flags!** Example: `./build/bin/helix-screen --test -p settings -vv`
Use `-v` (INFO), `-vv` (DEBUG), or `-vvv` (TRACE). Without flags = WARN only = you miss everything.

---

## Lazy Documentation Loading

**Load ONLY when actively working on the topic:**
| Doc | When |
|-----|------|
| `docs/LVGL9_XML_GUIDE.md` | Modifying XML layouts, debugging XML |
| `docs/ENVIRONMENT_VARIABLES.md` | Runtime env vars, mock config, automation |
| `docs/MOONRAKER_SECURITY_REVIEW.md` | Moonraker/security work |
| `docs/WIFI_WPA_SUPPLICANT_MIGRATION.md` | WiFi features |
| `docs/BUILD_SYSTEM.md` | Build troubleshooting, Makefile changes |
| `docs/DOXYGEN_GUIDE.md` | Documenting APIs |
| `docs/CI_CD_GUIDE.md` | GitHub Actions |

---

## CRITICAL RULES

**These are frequently violated. Check before coding.**

| # | Rule | ❌ Wrong | ✅ Correct |
|---|------|----------|-----------|
| 1 | **Use design tokens** | Hardcoded values | Responsive tokens from `globals.xml` |
| 2 | **Search SAME FILE first** | Inventing new approach | Grep the file you're editing for similar patterns before implementing |
| 3 | spdlog only | `printf()`, `cout`, `LV_LOG_*` | `spdlog::info("temp: {}", t)` |
| 4 | No auto-mock fallbacks | `if(!start()) return Mock()` | Check `RuntimeConfig::should_mock_*()` |
| 5 | Read docs BEFORE coding | Start coding immediately | Read relevant guide for the area first |
| 6 | `make -j` (no number) | `make -j4`, `make -j8` | `make -j` auto-detects cores |
| 7 | RAII for widgets | `lv_malloc()` / `lv_free()` | `lvgl_make_unique<T>()` + `release()` |
| 8 | SPDX headers | 20-line GPL boilerplate | `// SPDX-License-Identifier: GPL-3.0-or-later` |
| 9 | Class-based architecture | `ui_panel_*_init()` functions | Classes: `MotionPanel`, `WiFiManager` |
| 10 | Clang-format | Inconsistent formatting | Let pre-commit hook fix it |
| 11 | **Icon font sync** | Add icon, forget `make regen-fonts` | Add to codepoints.h + regen_mdi_fonts.sh, run `make regen-fonts`, rebuild |
| 12 | **XML event_cb** | `lv_obj_add_event_cb()` in C++ | `<event_cb trigger="clicked" callback="..."/>` in XML |
| 13 | **NO lv_obj_add_event_cb()** | `lv_obj_add_event_cb(btn, cb, ...)` | XML `<event_cb trigger="clicked" callback="name"/>` + `lv_xml_register_event_cb()` |
| 14 | **NO imperative visibility** | `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` | XML `<bind_flag_if_eq subject="state" flag="hidden" ref_value="0"/>` |
| 15 | **NO lv_label_set_text for data** | `lv_label_set_text(lbl, "value")` | Subject binding: `<text_body bind_text="my_subject"/>` |
| 16 | **NO C++ styling** | `lv_obj_set_style_bg_color(obj, ...)` | XML design tokens: `style_bg_color="#card_bg"` |
| 17 | **Observer cleanup in DELETE** | Free struct with subjects | Save & remove observers first (see Rule 17 below) |
| 18 | **NO manual LVGL cleanup** | `lv_display_delete()`, `lv_group_delete()` | Just call `lv_deinit()` - it handles everything |
| 19 | **bind_style priority** | `style_bg_color` + `bind_style` | Inline attrs override bind_style - use TWO bind_styles |

**Rule 1 - Design Tokens (MANDATORY):**

| Category | ❌ Wrong | ✅ Correct |
|----------|----------|-----------|
| **Colors** | `lv_color_hex(0xE0E0E0)` | `ui_theme_get_color("card_bg")` |
| **Spacing** | `style_pad_all="12"` | `style_pad_all="#space_md"` |
| **Typography** | `<lv_label style_text_font="...">` | `<text_heading>`, `<text_body>`, `<text_small>` |

**C++ Theme Color API:**
```cpp
// ✅ For theme tokens - auto-handles light/dark mode:
lv_color_t bg = ui_theme_get_color("card_bg");      // Looks up card_bg_light or card_bg_dark
lv_color_t ok = ui_theme_get_color("success_color");
lv_color_t err = ui_theme_get_color("error_color");

// ✅ For hex strings only:
lv_color_t custom = ui_theme_parse_color("#FF4444");  // Parses literal hex

// ❌ WRONG - parse_color does NOT look up tokens:
// lv_color_t bg = ui_theme_parse_color("#card_bg");  // Parses "card_bg" as hex → garbage!

// Pre-defined macros for common colors:
lv_color_t text = UI_COLOR_TEXT_PRIMARY;   // White text
lv_font_t* font = UI_FONT_SMALL;           // Responsive small font
```
**Reference:** See `ui_icon.cpp` for semantic color usage pattern.

**Rule 12 - XML event_cb:** Events ALWAYS in XML `<event_cb trigger="clicked" callback="name"/>`, register in C++ with `lv_xml_register_event_cb(nullptr, "name", func)`. **NEVER** `lv_obj_add_event_cb()`. See `hidden_network_modal.xml` + `ui_toast.cpp`.

**Rule 17 - Observer cleanup:** When using `lv_label_bind_text()` with subjects in DELETE-freed structs, save observers and remove them BEFORE freeing. See `docs/LVGL9_XML_GUIDE.md` § Observer Cleanup.

---

## Declarative UI Pattern

**DATA in C++, APPEARANCE in XML, Subjects connect them.** See Rules 12-16. Exceptions: DELETE cleanup, widget pool recycling, chart data, animations. Reference: `ui_panel_bed_mesh.cpp`.

---

## Debugging Principles

**Trust the debug output.** When logs show impossible values (e.g., a 26px font reporting 16px line height), the bug is UPSTREAM of where you're looking. Don't re-check the same fix - look for a second failure point.

**Example:** Font not rendering correctly?
1. First fix: Enable in `lv_conf.h` → Still broken
2. Don't re-check `lv_conf.h` - look for the SECOND requirement
3. Second fix: Register with `lv_xml_register_font()` in `main.cpp` → Fixed!

**When a fix doesn't work, ask:** "What ELSE could cause this?" not "Did I do the first fix wrong?"

---

## Quick Start

**HelixScreen**: LVGL 9.4 touchscreen UI for Klipper 3D printers. Pattern: XML → Subjects → C++.

```bash
make -j                              # Incremental build (native/SDL)
./build/bin/helix-screen           # Run (default: home panel, small screen)
./build/bin/helix-screen -p motion -s large
./build/bin/helix-screen --test    # Mock printer (REQUIRED without real printer!)

# Pi deployment workflow (PREFERRED):
make remote-pi                       # Build on thelio.local (~40s), fetch binaries
make deploy-pi                       # rsync binaries+assets to helixpi.local

# Shortcuts:
make deploy-pi-run                   # Deploy + run in foreground (see output)
make pi-test                         # Build + deploy + run (full cycle)
make pi-ssh                          # SSH into helixpi.local

# Local Docker build (slow, only if thelio unavailable):
make pi-docker                       # Build for Pi locally via Docker
make ad5m-docker                     # Build for Adventurer 5M locally

# Testing (runs in parallel by default):
make test-run                        # Run tests in parallel (~4-8x faster)
make test-serial                     # Run tests sequentially (for debugging)
./build/bin/helix-tests "[tag]"      # Run specific tests directly
```

**⚠️ IMPORTANT:** Always use `--test` when testing without a real printer. Without it, panels expecting printer data show nothing.

**Panels:** home, controls, motion, nozzle-temp, bed-temp, extrusion, filament, settings, advanced, print-select

**Screenshots:** Press 'S' in UI, or `./scripts/screenshot.sh helix-screen output-name [panel]`

---

## Plan Execution Protocol

### Work Classification

| If ANY of these apply... | Classification |
|--------------------------|----------------|
| User approves a plan | **MAJOR** |
| New feature (not a tweak) | **MAJOR** |
| Refactor touching 4+ files | **MAJOR** |
| Architectural/design change | **MAJOR** |
| Touches critical paths* | **MAJOR** |
| Otherwise | **MINOR** |

*Critical: PrinterState, WebSocket/threading, shutdown, DisplayManager, XML processing

### MAJOR Work Protocol

**Setup:**
1. Create worktree: `git worktree add ../helixscreen-<feature> origin/main`
2. Main session = orchestration only (spawn agents, evaluate, commit)

**Per Phase:**
1. Write failing tests FIRST (real tests that fail if feature removed)
2. Run NEW tests only: `./build/bin/helix-test "[new-tag]"` — full suite deferred to completion
3. Delegate implementation to agent
4. Agent implements until tests pass
5. Code review (spawn review agent)
6. Commit: `[phase-N] description`

**Completion:**
- Full test suite: `make test-run`
- Final code review (full scope)
- All phases marked complete in IMPLEMENTATION_PLAN.md
- Worktree mergeable to main

### MINOR Work Protocol

- No worktree required
- Tests optional (required if behavior changes)
- Agent delegation preferred but not mandatory
- Single commit when done

### Stop Criteria (Both)

**STOP after 3 failed attempts.** Provide: what you tried (with errors), why each failed, current hypothesis, what would unblock you.

### What Makes Tests "Real"

❌ `REQUIRE(true)`, unchecked `.has_value()`, mocking the thing under test, happy-path only
✅ Tests that FAIL if feature removed, edge cases, error conditions, boundary values

---

## Critical Patterns

| Pattern | Key Point |
|---------|-----------|
| Subject init order | Register components → init subjects → create XML |
| Component names | Always add explicit `name="..."` to component tags |
| Widget lookup | `lv_obj_find_by_name()` not `lv_obj_get_child(idx)` |
| Copyright headers | SPDX header required in all new source files |
| Image scaling | Call `lv_obj_update_layout()` before scaling |
| Nav history | `ui_nav_push_overlay()`/`ui_nav_go_back()` for overlays |
| Public API only | Never use `_lv_*()` private LVGL interfaces |
| API docs | Doxygen `@brief`/`@param`/`@return` required |

---

## Common Gotchas

1. **No `flag_` prefix** - Use `hidden="true"` not `flag_hidden="true"`
2. **Conditional bindings = child elements** - `<lv_obj-bind_flag_if_eq>` not attributes
3. **Three flex properties** - `style_flex_main_place` + `style_flex_cross_place` + `style_flex_track_place`
4. **Subject conflicts** - Don't declare subjects in `globals.xml`
5. **Component names = filename** - `nozzle_temp_panel.xml` → component name is `nozzle_temp_panel`
6. **WebSocket callbacks = background thread** - libhv callbacks run on a separate thread. NEVER call `lv_subject_set_*()` directly - use `ui_async_call()` (from `ui_update_queue.h`) to defer to main thread. See `printer_state.cpp` for the `set_*_internal()` pattern.
7. **Deferred dependency propagation** - When `set_X()` updates a member, also update child objects that cached the old value. Example: `PrintSelectPanel::set_api()` must call `file_provider_->set_api()` because `file_provider_` was created with nullptr.
8. **LVGL shutdown order** - NEVER call `lv_display_delete()`, `lv_group_delete()`, or `lv_indev_delete()` manually. `lv_deinit()` handles all cleanup internally. Manual calls cause double-free crashes. See `display_manager.cpp` comments.
9. **Application shutdown guard** - `Application::shutdown()` must guard against double-calls with `m_shutdown_complete` flag. Without it, the destructor calls shutdown again after `spdlog::shutdown()`, causing SIGABRT in `fmt::detail::throw_format_error`.
10. **LVGL 9.4 API renames** - `lv_xml_register_component_from_file()` (was `lv_xml_component_register_from_file`), `lv_xml_register_widget()` (was `lv_xml_widget_register`), `<event_cb trigger="clicked">` (was `lv_event-call_function`). Valid align values: `left_mid`, `right_mid`, `top_left`, `top_mid`, `top_right`, `bottom_left`, `bottom_mid`, `bottom_right`, `center`.
