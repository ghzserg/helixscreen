# LESSONS.md - Project Level

> **Lessons System**: Cite lessons with [L###] when applying them.
> Stars accumulate with each use. At 50 uses, project lessons promote to system.
>
> **Add lessons**: `LESSON: [category:] title - content`
> **Categories**: pattern, correction, decision, gotcha, preference

## Active Lessons


### [L001] [*****/*****] Conventional commits format
- **Uses**: 48 | **Learned**: 2025-12-21 | **Last**: 2025-12-24 | **Category**: pattern
> Use type(scope): description format for commit messages. Types: feat, fix, refactor, docs, test, chore, style, perf, build. Examples: fix(home): auto-configure LED, feat(thumbnails): add dynamic sizing.

### [L002] [****+/-----] Verbose flags required
- **Uses**: 9 | **Learned**: 2025-12-14 | **Last**: 2025-12-24 | **Category**: gotcha
> Always use -v or -vv when running helix-screen to see logs. Default shows WARN only which misses all debug info

### [L003] [***--/-----] Component names explicit
- **Uses**: 6 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: pattern
> Always add name='component_name' on XML component tags. Internal view names don't propagate, causing lv_obj_find_by_name to return NULL

### [L004] [**---/-----] Subject init before create
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: pattern
> Initialize and register subjects BEFORE lv_xml_create(). Order: fonts, images, components, init subjects, register subjects, create UI

### [L005] [**---/-----] Static buffers for subjects
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: gotcha
> String subject buffers must be static or heap allocated, not stack. Stack buffers go out of scope and corrupt data

### [L006] [**---/-----] get_color vs parse_color
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: correction
> ui_theme_get_color('token') looks up theme tokens. ui_theme_parse_color('#hex') parses literal hex only. Using parse_color with token names returns garbage

### [L007] [**---/-----] XML event callbacks only
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: correction
> Never use lv_obj_add_event_cb() in C++. Always use XML event_cb trigger and register with lv_xml_register_event_cb()

### [L008] [**---/-----] Design tokens mandatory
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: pattern
> No hardcoded colors or spacing. Use #card_bg, #space_md, text_body etc. Check globals.xml for available tokens

### [L009] [**---/-----] Icon font sync workflow
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: gotcha
> After adding icon to codepoints.h: add to regen_mdi_fonts.sh, run make regen-fonts, then rebuild. Forgetting any step = missing icon

### [L010] [*****/*****] No spdlog in destructors
- **Uses**: 26 | **Learned**: 2025-12-14 | **Last**: 2025-12-24 | **Category**: gotcha
> Never call spdlog::info/warn/error in destructors. Logger may be destroyed before your object during static destruction, causing crash on exit

### [L011] [****+/-----] No mutex in destructors
- **Uses**: 9 | **Learned**: 2025-12-14 | **Last**: 2025-12-24 | **Category**: gotcha
> Avoid mutex locks in destructors during static destruction phase. Other objects may already be destroyed, causing deadlock or crash on exit

### [L012] [*****/*----] Guard async callbacks
- **Uses**: 12 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: gotcha
> Async WebSocket callbacks can fire after object destruction. Use weak_ptr or flag checks to guard against stale this pointers in async handlers

### [L013] [***+-/-----] Callbacks before XML creation
- **Uses**: 7 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: correction
> Register event callbacks with lv_xml_register_event_cb() BEFORE calling lv_xml_create(). XML parser needs callbacks available during creation

### [L014] [*****/*----] Register all XML components
- **Uses**: 12 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: gotcha
> When adding new XML components, must add lv_xml_component_register_from_file() call in main.cpp. Forgetting causes silent failures

### [L015] [**---/-----] No hardcoded colors in C++
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: correction
> Use ui_theme_get_color() for all colors in C++. Hardcoded lv_color_hex() values break dark mode and violate design token system

### [L016] [**---/-----] Test flag required
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: pattern
> Always use --test flag when running without real printer. Without it, panels expecting printer data show nothing

### [L017] [**---/-----] make -j no number
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: correction
> Use make -j not make -j4 or -j8. The -j flag without number auto-detects optimal core count

### [L018] [**---/-----] Find widgets by name
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: pattern
> Use lv_obj_find_by_name(parent, name) not lv_obj_get_child(parent, idx). Child indices are fragile and break when layout changes

### [L019] [**---/-----] Style prefix rules
- **Uses**: 4 | **Learned**: 2025-12-14 | **Last**: 2025-12-21 | **Category**: gotcha
> In <styles> blocks use bare names (bg_color). On widgets use style_ prefix (style_bg_color). Mixing them up silently fails

### [L020] [*****/-----] ObserverGuard for cleanup
- **Uses**: 10 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: gotcha
> Use ObserverGuard RAII wrapper for lv_subject observers. Manual observer cleanup is error-prone and causes use-after-free on panel destruction

### [L021] [***--/-----] Centidegrees for temps
- **Uses**: 6 | **Learned**: 2025-12-14 | **Last**: 2025-12-22 | **Category**: pattern
> Use centidegrees (int) for temperature subjects to preserve 0.1C resolution. Float subjects lose precision in LVGL binding

### [L022] [***--/-----] Propagate deferred deps
- **Uses**: 6 | **Learned**: 2025-12-19 | **Last**: 2025-12-22 | **Category**: gotcha
> When set_X() updates a member, also update child objects that cached the old value (e.g., file_provider_->set_api() in PrintSelectPanel::set_api)

### [L023] [*****/*****] Stage files explicitly
- **Uses**: 56 | **Learned**: 2025-12-19 | **Last**: 2025-12-24 | **Category**: correction
> When committing, stage only files you actually modified (`git add <file>`) rather than `git add -A`. But ALSO verify completeness - check `git status` to ensure no critical feature files are missed. Atomic commits need both precision AND completeness.


### [L024] [*****/*****] WIP file safety
- **Uses**: 40 | **Learned**: 2025-12-21 | **Last**: 2025-12-24 | **Category**: correction
> Never modify, stash, restore, or commit files that are part of uncommitted WIP changes without explicit user permission. Focus only on the specific files/changes requested.

### [L025] [*----/-----] Button content centering
- **Uses**: 2 | **Learned**: 2025-12-21 | **Last**: 2025-12-24 | **Category**: pattern
> Text-only buttons: use `align="center"` on child. Icon+text buttons with flex_flow="row": need ALL THREE flex properties - style_flex_main_place="center" (horizontal), style_flex_cross_place="center" (vertical align items), style_flex_track_place="center" (vertical position of row). Missing track_place causes content to sit at top.


### [L026] [+----/-----] Never delete unknown files
- **Uses**: 1 | **Learned**: 2025-12-22 | **Last**: 2025-12-22 | **Category**: correction
> NEVER delete files you don't understand without explicit user permission. If untracked files cause build errors, ASK the user first - they may be important WIP. Ignoring the build error is always safer than deleting files.


### [L027] [*----/-----] Worktree initialization
- **Uses**: 2 | **Learned**: 2025-12-24 | **Last**: 2025-12-24 | **Category**: pattern
> When creating a git worktree for a project with submodules or generated files, ALWAYS run the init script (e.g., init-worktree.sh) before building. Worktrees share git history but have separate working directories - generated files like lib/libhv/include/hv/ don't exist until built.


### [L028] [*----/-----] Use project venv
- **Uses**: 2 | **Learned**: 2025-12-24 | **Last**: 2025-12-24 | **Category**: correction
> Always use project's .venv for Python packages (.venv/bin/pip install X), never system pip. Project dependencies should stay isolated in the venv.

