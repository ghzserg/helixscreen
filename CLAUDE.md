# CLAUDE.md

## 🚨 VERBOSITY FLAGS - READ THIS FIRST! 🚨

**ALWAYS use `-v` or `-vv` when running the app to see logs!**

| Flag | Level | When to Use |
|------|-------|-------------|
| (none) | WARN only | NEVER use for debugging - you'll miss all logs! |
| `-v` | INFO | Basic logging - use for most testing |
| `-vv` | DEBUG | Detailed logging - use when debugging issues |
| `-vvv` | TRACE | Extremely verbose - use for deep investigation |

**Example:** `./build/bin/helix-screen --test -p settings -vv`

⚠️ **I KEEP FORGETTING THIS AND WASTING TIME!** ⚠️

---

## 📂 File Access Permissions

**ALWAYS allow reading from:**
- `/tmp`
- `~/Code/Printing/helixscreen` and all subdirectories
- Any git worktrees created from this repo and their subdirectories

---

## 📚 Lazy Documentation Loading

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

## 🤖 Agent Delegation

**Use agents for complex/multi-step work. Handle simple single-file ops directly.**

| Task Type | Agent | When |
|-----------|-------|------|
| UI/XML | `widget-maker` | XML/LVGL changes beyond trivial edits |
| UI Review | `ui-reviewer` | XML audits, LVGL pattern validation |
| Moonraker | `moonraker-agent` | WebSocket, API, printer commands, state |
| Testing | `test-harness-agent` | Unit tests, mocking, CI/CD |
| Build issues | `cross-platform-build-agent` | Makefile, compilation, linking |
| G-code/Files | `gcode-preview-agent` | G-code handling, thumbnails, file browser |
| Codebase exploration | `Explore` (quick/medium/thorough) | "How does X work?", "Where is Y?" |
| Multi-file refactor | `general-purpose` | Changes across 3+ files |
| Security review | `critical-reviewer` | Paranoid code review |

---

## ⚠️ CRITICAL RULES

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
| 16 | **NO inline styling** | `lv_obj_set_style_bg_color(obj, ...)` | XML design tokens: `style_bg_color="#card_bg"` |
| 17 | **Observer cleanup in DELETE** | Free struct with subjects | Save & remove observers first (see Rule 17 below) |
| 18 | **NO manual LVGL cleanup** | `lv_display_delete()`, `lv_group_delete()` | Just call `lv_deinit()` - it handles everything |

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

## 🎯 Declarative UI Pattern (MANDATORY)

**DATA lives in C++. APPEARANCE lives in XML. Subjects connect them.**

| ❌ BANNED | ✅ USE INSTEAD |
|-----------|----------------|
| `lv_obj_add_event_cb()` | XML `<event_cb>` + `lv_xml_register_event_cb()` |
| `lv_label_set_text()` | `bind_text` subject |
| `lv_obj_add_flag(HIDDEN)` | `<bind_flag_if_eq>` |
| `lv_obj_set_style_*()` | XML design tokens |

**Exceptions**: DELETE cleanup, widget pool recycling, chart data points, animations.

**Reference**: `ui_panel_bed_mesh.cpp` (gold standard), `docs/LVGL9_XML_GUIDE.md` (full examples)

---

## 🐛 Debugging Principles

**Trust the debug output.** When logs show impossible values (e.g., a 26px font reporting 16px line height), the bug is UPSTREAM of where you're looking. Don't re-check the same fix - look for a second failure point.

**Example:** Font not rendering correctly?
1. First fix: Enable in `lv_conf.h` → Still broken
2. Don't re-check `lv_conf.h` - look for the SECOND requirement
3. Second fix: Register with `lv_xml_register_font()` in `main.cpp` → Fixed!

**When a fix doesn't work, ask:** "What ELSE could cause this?" not "Did I do the first fix wrong?"

---

## Project Overview

**HelixScreen** - A best-in-class Klipper touchscreen UI designed for a variety of 3D printers. Built with LVGL 9.4 using declarative XML layouts and reactive Subject-Observer data binding. Runs on SDL2 for development, targets framebuffer displays on embedded hardware.

**Core pattern:** XML (layout) → Subjects (reactive data) → C++ (logic). No hardcoded colors - use `globals.xml` with `*_light`/`*_dark` variants.

**Pluggable backends:** Manager → Abstract Interface → Platform implementations (macOS/Linux/Mock). Factory pattern at runtime.

---

## Quick Start

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
```

**⚠️ IMPORTANT:** Always use `--test` when testing without a real printer. Without it, panels expecting printer data show nothing.

**Panels:** home, controls, motion, nozzle-temp, bed-temp, extrusion, filament, settings, advanced, print-select

**Screenshots:** Press 'S' in UI, or `./scripts/screenshot.sh helix-screen output-name [panel]`

---

## LVGL 9.4 API Changes (from 9.3)

```cpp
// Registration renamed:
lv_xml_register_component_from_file()  // was: lv_xml_component_register_from_file
lv_xml_register_widget()               // was: lv_xml_widget_register
```

```xml
<!-- Event syntax: -->
<event_cb trigger="clicked" callback="my_callback"/>  <!-- was: lv_event-call_function -->

<!-- Valid align values (NOT just "left"): -->
<!-- left_mid, right_mid, top_left, top_mid, top_right, bottom_left, bottom_mid, bottom_right, center -->
```

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

---

## Documentation

**Core docs:**
- `README.md` - Project overview
- `docs/DEVELOPMENT.md` - Build system, daily workflow
- `docs/ARCHITECTURE.md` - System design, patterns
- `docs/CONTRIBUTING.md` - Code standards, git workflow
- `docs/ROADMAP.md` - Future features

**Reference (load when needed):**
- `docs/LVGL9_XML_GUIDE.md` - Complete XML reference
- `docs/QUICK_REFERENCE.md` - Common code patterns
- `docs/BUILD_SYSTEM.md` - Makefile, patches
- `docs/TESTING.md` - Catch2, test infrastructure
- `docs/COPYRIGHT_HEADERS.md` - SPDX headers

---

## File Organization

```
helixscreen/
├── src/              # C++ business logic
├── include/          # Headers
├── lib/              # External libs (lvgl, libhv, spdlog, sdl2, tinygl, etc.)
├── ui_xml/           # XML component definitions
├── assets/           # Fonts, images, icons
├── config/           # Config templates (helixconfig.json.template, printer_database.json)
├── scripts/          # Build/screenshot automation
├── docker/           # Cross-compilation Dockerfiles (pi, ad5m)
├── mk/               # Makefile modules (cross.mk, deps.mk, rules.mk, etc.)
├── docs/             # Documentation
└── Makefile          # Build system entry point
```

---

## Development Workflow

**Session startup:** Check `git log --oneline -10` and `git status` to understand recent work.

**Daily cycle:** Edit XML (no recompile) or C++ → `make -j` → test → screenshot
