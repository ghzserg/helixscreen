# LESSONS.md - Project Level

> **Lessons System**: Cite lessons with [L###] when applying them.
> Stars accumulate with each use. At 50 uses, project lessons promote to system.
>
> **Add lessons**: `LESSON: [category:] title - content`
> **Categories**: pattern, correction, decision, gotcha, preference

## Active Lessons


### [L002] [***--|-----] Verbose flags required
- **Uses**: 12 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-29 | **Category**: gotcha
> Always use -v or -vv when running helix-screen to see logs. Default shows WARN only which misses all debug info


### [L003] [***--|-----] Component names explicit
- **Uses**: 10 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-30 | **Category**: pattern
> Always add name='component_name' on XML component tags. Internal view names don't propagate, causing lv_obj_find_by_name to return NULL


### [L004] [****-|-----] Subject init before create
- **Uses**: 13 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-30 | **Category**: pattern
> Initialize and register subjects BEFORE lv_xml_create(). Order: fonts, images, components, init subjects, register subjects, create UI


### [L005] [***--|-----] Static buffers for subjects
- **Uses**: 6 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-28 | **Category**: gotcha
> String subject buffers must be static or heap allocated, not stack. Stack buffers go out of scope and corrupt data


### [L006] [**---|-----] get_color vs parse_color
- **Uses**: 5 | **Velocity**: 0.0 | **Learned**: 2025-12-14 | **Last**: 2025-12-25 | **Category**: correction
> ui_theme_get_color('token') looks up theme tokens. ui_theme_parse_color('#hex') parses literal hex only. Using parse_color with token names returns garbage


### [L007] [***--|-----] XML event callbacks only
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-01 | **Category**: correction
> Never use lv_obj_add_event_cb() in C++. Always use XML event_cb trigger and register with lv_xml_register_event_cb()


### [L008] [***--|-----] Design tokens mandatory
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-31 | **Category**: pattern
> No hardcoded colors or spacing. Use #card_bg, #space_md, text_body etc. Check globals.xml for available tokens


### [L009] [***--|-----] Icon font sync workflow
- **Uses**: 9 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-01 | **Category**: gotcha
> After adding icon to codepoints.h: add to regen_mdi_fonts.sh, run make regen-fonts, then rebuild. Forgetting any step = missing icon


### [L011] [***--|-----] No mutex in destructors
- **Uses**: 12 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-29 | **Category**: gotcha
> Avoid mutex locks in destructors during static destruction phase. Other objects may already be destroyed, causing deadlock or crash on exit


### [L012] [*****|-----] Guard async callbacks
- **Uses**: 32 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-29 | **Category**: gotcha
> Async WebSocket callbacks can fire after object destruction. Use weak_ptr or flag checks to guard against stale this pointers in async handlers


### [L013] [***--|-----] Callbacks before XML creation
- **Uses**: 12 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-01 | **Category**: correction
> Register event callbacks with lv_xml_register_event_cb() BEFORE calling lv_xml_create(). XML parser needs callbacks available during creation


### [L014] [****-|-----] Register all XML components
- **Uses**: 15 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-28 | **Category**: gotcha
> When adding new XML components, must add lv_xml_component_register_from_file() call in main.cpp. Forgetting causes silent failures


### [L015] [***--|-----] No hardcoded colors in C++
- **Uses**: 6 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-28 | **Category**: correction
> Use ui_theme_get_color() for all colors in C++. Hardcoded lv_color_hex() values break dark mode and violate design token system


### [L016] [**---|-----] Test flag required
- **Uses**: 5 | **Velocity**: 0.0 | **Learned**: 2025-12-14 | **Last**: 2025-12-25 | **Category**: pattern
> Always use --test flag when running without real printer. Without it, panels expecting printer data show nothing


### [L017] [**---|-----] make -j no number
- **Uses**: 5 | **Velocity**: 0.0 | **Learned**: 2025-12-14 | **Last**: 2025-12-25 | **Category**: correction
> Use make -j not make -j4 or -j8. The -j flag without number auto-detects optimal core count


### [L018] [***--|-----] Find widgets by name
- **Uses**: 6 | **Velocity**: 0.0 | **Learned**: 2025-12-14 | **Last**: 2025-12-27 | **Category**: pattern
> Use lv_obj_find_by_name(parent, name) not lv_obj_get_child(parent, idx). Child indices are fragile and break when layout changes


### [L019] [**---|-----] Style prefix rules
- **Uses**: 5 | **Velocity**: 0.0 | **Learned**: 2025-12-14 | **Last**: 2025-12-25 | **Category**: gotcha
> In <styles> blocks use bare names (bg_color). On widgets use style_ prefix (style_bg_color). Mixing them up silently fails


### [L020] [****-|-----] ObserverGuard for cleanup
- **Uses**: 17 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-29 | **Category**: gotcha
> Use ObserverGuard RAII wrapper for lv_subject observers. Manual observer cleanup is error-prone and causes use-after-free on panel destruction


### [L021] [***--|-----] Centidegrees for temps
- **Uses**: 8 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-31 | **Category**: pattern
> Use centidegrees (int) for temperature subjects to preserve 0.1C resolution. Float subjects lose precision in LVGL binding


### [L022] [***--|-----] Propagate deferred deps
- **Uses**: 7 | **Velocity**: 0.0 | **Learned**: 2025-12-19 | **Last**: 2025-12-25 | **Category**: gotcha
> When set_X() updates a member, also update child objects that cached the old value (e.g., file_provider_->set_api() in PrintSelectPanel::set_api)


### [L025] [***--|-----] Button content centering
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-21 | **Last**: 2025-12-31 | **Category**: pattern
> Text-only buttons: use `align="center"` on child. Icon+text buttons with flex_flow="row": need ALL THREE flex properties - style_flex_main_place="center" (horizontal), style_flex_cross_place="center" (vertical align items), style_flex_track_place="center" (vertical position of row). Missing track_place causes content to sit at top.


### [L026] [*----|-----] Never delete unknown files
- **Uses**: 2 | **Velocity**: 0.0 | **Learned**: 2025-12-22 | **Last**: 2025-12-25 | **Category**: correction
> NEVER delete files you don't understand without explicit user permission. If untracked files cause build errors, ASK the user first - they may be important WIP. Ignoring the build error is always safer than deleting files.


### [L027] [***--|-----] Worktree initialization
- **Uses**: 6 | **Velocity**: 0.01 | **Learned**: 2025-12-24 | **Last**: 2025-12-28 | **Category**: pattern
> When creating a git worktree, ALWAYS run ./scripts/init-worktree.sh BEFORE any commits. Worktrees don't auto-initialize submodules - uninitialized submodules appear as deletions and will be silently removed from git's tree on your next commit.


### [L028] [**---|-----] Use project venv
- **Uses**: 3 | **Velocity**: 0.0 | **Learned**: 2025-12-24 | **Last**: 2025-12-25 | **Category**: correction
> Always use project's .venv for Python packages (.venv/bin/pip install X), never system pip. Project dependencies should stay isolated in the venv.


### [L029] [***--|-----] LVGL observer callbacks
- **Uses**: 12 | **Velocity**: 0.01 | **Learned**: 2025-12-25 | **Last**: 2026-01-01 | **Category**: pattern
> LVGL observer callbacks use C-style function signatures (lv_observer_t*, lv_subject_t*) - NOT lambdas. Must pass user_data via lv_observer_get_user_data(observer). Also: lv_subject_set_*() from non-main threads must use ui_async_call() to avoid render-phase assertions.


### [L030] [***--|-----] Local vs remote UI design
- **Uses**: 6 | **Velocity**: 0.0 | **Learned**: 2025-12-25 | **Last**: 2025-12-26 | **Category**: pattern
> Touchscreen UIs are for physically present users - prioritize tactile controls, at-a-glance info, and real-time tuning. Don't copy features from remote web UIs (cameras, system stats, multi-device views) that assume users aren't standing at the machine.


### [L031] [***--|-----] XML no recompile
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-27 | **Last**: 2025-12-31 | **Category**: gotcha
> XML layout changes (ui_xml/*.xml) don't require recompilation - just restart the app. Only C++ changes need make.


### [L032] [*****|+----] Re-stage after pre-commit format
- **Uses**: 46 | **Velocity**: 1.26 | **Learned**: 2025-12-27 | **Last**: 2026-01-01 | **Category**: correction
> When pre-commit hook auto-formats files, they are NOT automatically re-staged. Always check git status after a commit and amend if the hook formatted files. Look for 'Auto-formatted: <file>' messages and run 'git add -u && git commit --amend --no-edit'.


### [L033] [*----|-----] No detached threads on ARM/glibc
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2025-12-28 | **Category**: gotcha
> std::thread::detach() causes SIGABRT on ARM Linux with static glibc due to TLS cleanup issues during thread exit. Affects anything using TLS (spdlog, std::function, STL). Use synchronous execution or persistent worker threads instead.


### [L034] [***--|-----] macOS audio feedback
- **Uses**: 8 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2025-12-28 | **Category**: preference
> When using macOS say command for audio feedback, use the Fred voice: say -v Fred


### [L035] [**---|-----] Push It celebration
- **Uses**: 5 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2025-12-30 | **Category**: preference
> After successful git push, play Salt-N-Pepa's Push It: open 'https://www.youtube.com/watch?v=vCadcBR95oU&t=45s'


### [L036] [*----|-----] Header file documentation
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2025-12-28 | **Category**: pattern
> Important files have documentation in their header files (include/*.h). Check the header file first when trying to understand a class or module - it often contains usage examples, design rationale, and API documentation.


### [L038] [**---|-----] bind_flag_if_not_eq NOT ne
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2025-12-29 | **Last**: 2025-12-29 | **Category**: correction
> LVGL XML uses bind_flag_if_not_eq (NOT bind_flag_if_ne). This typo has caused layout corruption multiple times. The 'ne' abbreviation does NOT exist - always use the full 'not_eq' suffix.


### [L039] [**---|-----] Unique XML callback names
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2025-12-30 | **Last**: 2025-12-30 | **Category**: pattern
> All XML event_cb callback names must be globally unique using on_<component>_<action> pattern. LVGL's XML callback registry is a flat global namespace with no scoping. Generic names like on_modal_ok_clicked cause collisions when multiple components register handlers.


### [L040] [**---|-----] Inline XML attrs override bind_style
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2025-12-30 | **Last**: 2025-12-30 | **Category**: gotcha
> When using bind_style for reactive visual changes, inline style attributes (style_bg_color, style_text_color, etc.) have higher priority in LVGL's style cascade. bind_style cannot override them. Solution: use TWO bind_styles (one per state) with NO inline styling for properties you want to change reactively.


### [L041] [***--|-----] Subject init/deinit symmetry
- **Uses**: 8 | **Velocity**: 0.01 | **Learned**: 2025-12-31 | **Last**: 2026-01-01 | **Category**: pattern
> Every init_subjects() must have a corresponding deinit_subjects() that calls lv_subject_deinit() on each subject. This applies to singletons AND panel classes with local lv_subject_t members.


### [L042] [*----|-----] XML bind_flag exclusive visibility
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2025-12-31 | **Last**: 2025-12-31 | **Category**: pattern
> Multiple bind_flag_if_eq on same object creates independent observers where last one wins (race condition). For 'show only when X=value' logic, use single bind_flag_if_not_eq instead. Example: bind_flag_if_not_eq ref_value="0" shows only when value IS 0.
