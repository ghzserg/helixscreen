# CLAUDE.md

## Quick Start

**HelixScreen**: LVGL 9.4 touchscreen UI for Klipper 3D printers. XML engine in `lib/lvgl/src/xml/`. Pattern: XML → Subjects → C++.

**Before compiling:** Check for existing build processes (`pgrep -f 'make|c\+\+'`) — concurrent compilations thrash the machine.

```bash
make -j                              # Build ONLY the program binary (NOT tests)
./build/bin/helix-screen --test -vv  # Mock printer + DEBUG logs
# ALWAYS use verbosity: -v=INFO, -vv=DEBUG, -vvv=TRACE (default=WARN)

make test                            # Build tests only (does NOT run them)
make test-run                        # Build AND run tests in parallel
./build/bin/helix-tests "[tag]"      # Run specific test tags
make pi-test                         # Build on thelio + deploy + run

# Worktrees — MUST use for MAJOR work. Always in .worktrees/ (project root).
scripts/setup-worktree.sh feature/my-branch  # Symlinks deps, builds fast
```

**Screenshots:** Press 'S' in UI, or `./scripts/screenshot.sh helix-screen output-name [panel]`

---

## Docs (load when needed)

Full index: **`docs/devel/CLAUDE.md`** (auto-loaded when working in docs/devel/)

Most commonly needed:

| Doc | When |
|-----|------|
| `docs/devel/UI_CONTRIBUTOR_GUIDE.md` | UI/layout work: breakpoints, tokens, colors, widgets, layout overrides |
| `docs/devel/LVGL9_XML_GUIDE.md` | XML layouts, widgets, bindings, observer cleanup |
| `docs/devel/MODAL_SYSTEM.md` | Modal architecture: ui_dialog, modal_button_row, Modal pattern |
| `docs/devel/FILAMENT_MANAGEMENT.md` | AMS, AFC, Happy Hare, ValgACE, Tool Changer |
| `docs/devel/ENVIRONMENT_VARIABLES.md` | Runtime env vars, mock config |
| `docs/devel/LOGGING.md` | spdlog levels: info vs debug vs trace |
| `docs/devel/BUILD_SYSTEM.md` | Makefile, cross-compilation |

---

## Code Standards

| Rule | ❌ WRONG | ✅ CORRECT |
|------|----------|-----------|
| **spdlog only** | `printf()`, `cout`, `LV_LOG_*` | `spdlog::info("temp: {}", t)` |
| **SPDX headers** | 20-line GPL boilerplate | `// SPDX-License-Identifier: GPL-3.0-or-later` |
| **RAII widgets** | `lv_malloc()` / `lv_free()` | `lvgl_make_unique<T>()` + `release()` |
| **Class-based** | `ui_panel_*_init()` functions | Classes: `MotionPanel`, `WiFiManager` |
| **Observer factory** | Static callback + `lv_observer_get_user_data()` | `observe_int_sync<Panel>()` from `observer_factory.h` |
| **Icon sync** | Add icon, forget fonts | codepoints.h + `make regen-fonts` + rebuild |
| **Formatting** | Manual formatting | Let pre-commit hook (clang-format) fix |
| **No auto-mock** | `if(!start()) return Mock()` | Check `RuntimeConfig::should_mock_*()` |
| **JSON include** | `#include <nlohmann/json.hpp>` | `#include "hv/json.hpp"` (libhv's bundled version) |
| **Build system** | `cmake`, `ninja` | `make -j` (pure Makefile) |
| **Bug commits** | `fix: thing` (no reference) | `fix(scope): thing (prestonbrown/helixscreen#123)` |

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

Note: `ui_theme_get_color()` for tokens, `ui_theme_parse_color()` for hex strings only (NOT tokens).

---

## Threading & Lifecycle

WebSocket/libhv callbacks = background thread. **NEVER** call `lv_subject_set_*()` directly.
Use `ui_queue_update()` from `ui_update_queue.h`. Pattern: `printer_state.cpp` `set_*_internal()`

Use `ObserverGuard` for RAII cleanup. See `observer_factory.h` for `observe_int_sync`, `observe_int_async`, `observe_string`, `observe_string_async`.

**Observer safety:** `observe_int_sync` and `observe_string` **defer callbacks** via `ui_queue_update()` to prevent re-entrant observer destruction crashes (issue #82). Use `observe_int_immediate` / `observe_string_immediate` ONLY if you're certain the callback won't modify observer lifecycle (no reassignment, no widget destruction).

**Subject shutdown safety (MANDATORY):** Any class that creates LVGL subjects MUST self-register its cleanup inside `init_subjects()`. This prevents shutdown crashes (observer removal on freed subjects during `lv_deinit`). See `static_subject_registry.h` for full docs.

```cpp
void MyState::init_subjects() {
    if (subjects_initialized_) return;
    // ... create subjects ...
    subjects_initialized_ = true;
    StaticSubjectRegistry::instance().register_deinit(
        "MyState", []() { MyState::instance().deinit_subjects(); });
}
```

**Never** register cleanup externally (e.g., in SubjectInitializer). Co-locating init+cleanup prevents forgotten registrations that cause shutdown crashes.

**Dynamic subject lifetime safety (MANDATORY):** Per-fan, per-sensor, and per-extruder subjects are **dynamic** — they can be destroyed and recreated during reconnection/rediscovery. Observing a dynamic subject without a `SubjectLifetime` token causes **use-after-free crashes** when `lv_subject_deinit()` frees observers but `ObserverGuard` still holds a dangling pointer.

| ❌ CRASH | ✅ SAFE |
|----------|---------|
| `auto* s = state.get_fan_speed_subject(name);` | `SubjectLifetime lt;` |
| `obs = observe_int_sync(s, this, handler);` | `auto* s = state.get_fan_speed_subject(name, lt);` |
| | `obs = observe_int_sync(s, this, handler, lt);` |

**Dynamic subject sources** (always require lifetime token when observing):
- `PrinterFanState::get_fan_speed_subject(name, lifetime)` — per-fan speeds
- `TemperatureSensorManager::get_temp_subject(name, lifetime)` — per-sensor temps
- `PrinterTemperatureState::get_extruder_temp_subject(name, lifetime)` / `get_extruder_target_subject(name, lifetime)` — per-extruder temps

**Static subjects** (singleton lifetime, no token needed): `get_fan_speed_subject()` (no args), `get_bed_temp_subject()`, etc.

See `ui_observer_guard.h` for full documentation of the `SubjectLifetime` pattern.

---

## Patterns

| Pattern | Key Point |
|---------|-----------|
| Subject init order | Register components → init subjects → create XML |
| Widget lookup | `lv_obj_find_by_name()` not `lv_obj_get_child()` |
| Overlays | `ui_nav_push_overlay()`/`ui_nav_go_back()` |
| Modals (simple) | `Modal::show("component_name")` / `Modal::hide(dialog)` |
| Modals (subclass) | Extend `Modal`, implement `get_name()` + `component_name()`, override `on_ok()`/`on_cancel()` |
| Confirmation dialog | `modal_show_confirmation(title, msg, severity, btn_text, on_confirm, on_cancel, data)` (in `helix::ui`) |
| Modal buttons (XML) | `<modal_button_row primary_text="Save" primary_callback="on_save"/>` |

---

## Where Things Live

**Singletons** (all `::instance()`):
`PrinterState` (all printer data/subjects), `SettingsManager` (persistent settings), `NavigationManager` (panel/overlay stack), `UpdateQueue` (thread-safe UI updates), `SoundManager`, `DisplayManager`, `ModalStack`, `PrinterDetector` (printer DB + capabilities), `ToolState` (multi-tool tracking), `AmsState` (multi-backend filament systems)

**Entry flow**: `main.cpp` → `Application` → `DisplayManager` → panels via `NavigationManager`

**Key directories**:
| Path | Contents |
|------|----------|
| `src/ui/` | All UI code — flat dir, prefixed: `ui_panel_*.cpp`, `ui_overlay_*.cpp`, `ui_modal*.cpp` |
| `src/ui/modals/` | Additional modal implementations |
| `src/printer/` | PrinterState, MoonrakerAPI, macro/filament managers |
| `src/system/` | Config, settings, update checker, sound, telemetry |
| `src/application/` | App lifecycle, display, input, runtime config |
| `ui_xml/` | All XML layouts (loaded at runtime — no rebuild needed) |
| `ui_xml/components/` | Reusable XML components |
| `assets/` | Fonts, images, sounds, printer DB JSON |
| `config/` | Default config files, env templates |

**Runtime config** (on device): `~/helixscreen/config/` — settings.json, printer_database.json, helixscreen.env

---

## Debugging

**NEVER debug without flags!** Use `-vv` minimum.
Trust debug output. Impossible values = bug is UPSTREAM. Ask "what ELSE?" not "did first fix work?"

**Debug bundles**: `./scripts/debug-bundle.sh <SHARE_CODE> --save` to download. Save to `/tmp/` for investigation (not in repo).

---

## Critical Paths (always MAJOR work)

PrinterState, WebSocket/threading, shutdown, DisplayManager, XML processing

---

## Autonomous Sessions

When given autonomous control, Claude works independently to improve HelixScreen with minimal interruption.

**Scratchpad**: `.claude/scratchpad/` - Claude's workspace for:
- Ideas and feature concepts
- Research notes and findings
- Work-in-progress designs
- Lessons learned (like `animated_value_use_cases.md`)

**Mission**: Make HelixScreen the best damn touchscreen UI for Klipper printers.

**Autonomy Guidelines**:
- Work independently on improvements that align with existing patterns
- Commit working code with tests (don't leave broken state)
- Ask user ONLY for: major architectural decisions, UX preference calls, or when truly blocked
- Document findings in scratchpad for future sessions
- Small failures are fine - learn and move on

**Good autonomous work**:
- Polish and micro-improvements
- Code cleanup and consistency
- Adding missing tests
- Fixing obvious bugs
- Implementing features from ROADMAP.md

**Ask first**:
- New architectural patterns
- Removing/deprecating features
- Changes to critical paths (see above)
- Anything that changes user-facing behavior significantly
