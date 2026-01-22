# LESSONS.md - Project Level

> **Lessons System**: Cite lessons with [L###] when applying them.
> Stars accumulate with each use. At 50 uses, project lessons promote to system.
>
> **Add lessons**: `LESSON: [category:] title - content`
> **Categories**: pattern, correction, decision, gotcha, preference

## Active Lessons


### [L004] [****-|-----] Subject init before create
- **Uses**: 16 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-08 | **Category**: pattern | **Type**: informational
> 


### [L008] [***--|-----] Design tokens mandatory
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-31 | **Category**: pattern | **Type**: informational
> No hardcoded colors or spacing. Use #card_bg, #space_md, text_body etc. Check globals.xml for available tokens


### [L009] [****-|-----] Icon font sync workflow
- **Uses**: 13 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-17 | **Category**: gotcha | **Type**: constraint
> After adding icon to codepoints.h: add to regen_mdi_fonts.sh, run make regen-fonts, then rebuild. Forgetting any step = missing icon


### [L011] [****-|-----] No mutex in destructors
- **Uses**: 13 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-08 | **Category**: gotcha | **Type**: constraint
> Avoid mutex locks in destructors during static destruction phase. Other objects may already be destroyed, causing deadlock or crash on exit


### [L014] [****-|-----] Register all XML components
- **Uses**: 26 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-20 | **Category**: gotcha | **Type**: constraint
> When adding new XML components, must add lv_xml_component_register_from_file() call in main.cpp. Forgetting causes silent failures


### [L020] [****-|-----] ObserverGuard for cleanup
- **Uses**: 18 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2026-01-07 | **Category**: gotcha | **Type**: constraint
> Use ObserverGuard RAII wrapper for lv_subject observers. Manual observer cleanup is error-prone and causes use-after-free on panel destruction


### [L021] [***--|-----] Centidegrees for temps
- **Uses**: 8 | **Velocity**: 0.01 | **Learned**: 2025-12-14 | **Last**: 2025-12-31 | **Category**: pattern | **Type**: informational
> Use centidegrees (int) for temperature subjects to preserve 0.1C resolution. Float subjects lose precision in LVGL binding


### [L025] [***--|-----] Button content centering
- **Uses**: 7 | **Velocity**: 0.01 | **Learned**: 2025-12-21 | **Last**: 2025-12-31 | **Category**: pattern | **Type**: constraint
> Text-only buttons: use `align="center"` on child. Icon+text buttons with flex_flow="row": need ALL THREE flex properties - style_flex_main_place="center" (horizontal), style_flex_cross_place="center" (vertical align items), style_flex_track_place="center" (vertical position of row). Missing track_place causes content to sit at top.


### [L027] [***--|-----] Worktree initialization
- **Uses**: 11 | **Velocity**: 0.01 | **Learned**: 2025-12-24 | **Last**: 2026-01-20 | **Category**: pattern | **Type**: constraint
> When creating a git worktree, ALWAYS run ./scripts/init-worktree.sh BEFORE any commits. Worktrees don't auto-initialize submodules - uninitialized submodules appear as deletions and will be silently removed from git's tree on your next commit.


### [L029] [***--|-----] LVGL observer callbacks
- **Uses**: 12 | **Velocity**: 0.01 | **Learned**: 2025-12-25 | **Last**: 2026-01-01 | **Category**: pattern | **Type**: constraint
> LVGL observer callbacks use C-style function signatures (lv_observer_t*, lv_subject_t*) - NOT lambdas. Must pass user_data via lv_observer_get_user_data(observer). Also: lv_subject_set_*() from non-main threads must use ui_async_call() to avoid render-phase assertions.


### [L031] [****-|-----] XML no recompile
- **Uses**: 16 | **Velocity**: 0.01 | **Learned**: 2025-12-27 | **Last**: 2026-01-21 | **Category**: gotcha | **Type**: constraint
> 


### [L035] [**---|-----] Push It celebration
- **Uses**: 5 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2025-12-30 | **Category**: preference | **Type**: informational
> After successful git push, play Salt-N-Pepa's Push It: open 'https://www.youtube.com/watch?v=vCadcBR95oU&t=45s'


### [L036] [**---|-----] Header file documentation
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2025-12-28 | **Last**: 2026-01-05 | **Category**: pattern | **Type**: informational
> Important files have documentation in their header files (include/*.h). Check the header file first when trying to understand a class or module - it often contains usage examples, design rationale, and API documentation.


### [L039] [**---|-----] Unique XML callback names
- **Uses**: 4 | **Velocity**: 0.01 | **Learned**: 2025-12-30 | **Last**: 2026-01-09 | **Category**: pattern | **Type**: constraint
> All XML event_cb callback names must be globally unique using on_<component>_<action> pattern. LVGL's XML callback registry is a flat global namespace with no scoping. Generic names like on_modal_ok_clicked cause collisions when multiple components register handlers.


### [L040] [**---|-----] Inline XML attrs override bind_style
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2025-12-30 | **Last**: 2025-12-30 | **Category**: gotcha | **Type**: constraint
> When using bind_style for reactive visual changes, inline style attributes (style_bg_color, style_text_color, etc.) have higher priority in LVGL's style cascade. bind_style cannot override them. Solution: use TWO bind_styles (one per state) with NO inline styling for properties you want to change reactively.


### [L042] [*----|-----] XML bind_flag exclusive visibility
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2025-12-31 | **Last**: 2025-12-31 | **Category**: pattern | **Type**: informational
> Multiple bind_flag_if_eq on same object creates independent observers where last one wins (race condition). For 'show only when X=value' logic, use single bind_flag_if_not_eq instead. Example: bind_flag_if_not_eq ref_value="0" shows only when value IS 0.


### [L043] [**---|-----] Sonnet for structural reviews
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2026-01-01 | **Last**: 2026-01-21 | **Category**: pattern | **Type**: informational
> Use Sonnet (not Haiku) for architectural-level code reviews, structural changes, or final comprehensive reviews. Haiku is fine for quick single-file spot-checks with clear pass/fail criteria.


### [L045] [*----|-----] LVGL dropdown options
- **Uses**: 1 | **Velocity**: 0.0 | **Learned**: 2026-01-06 | **Last**: 2026-01-06 | **Category**: correction | **Type**: constraint
> LVGL dropdowns do NOT support bind_options in XML. Always use lv_dropdown_set_options() directly in C++ code to set dropdown options dynamically. All other dropdowns in the codebase follow this pattern.


### [L046] [*----|-----] XML subject shadows C++ subject
- **Uses**: 1 | **Velocity**: 0.0 | **Learned**: 2026-01-06 | **Last**: 2026-01-06 | **Category**: correction | **Type**: constraint
> When XML <subjects> declares a subject with the same name as a C++-registered subject (UI_SUBJECT_INIT_AND_REGISTER_*), the XML component-local subject shadows the global C++ one. XML bindings will find the local subject (stuck at default value) instead of the C++ one. Solution: Don't declare XML subjects for values managed entirely by C++.


### [L047] [**---|-----] Claude session forensics
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2026-01-08 | **Last**: 2026-01-08 | **Category**: recovery | **Type**: informational
> Recover lost session content: 1) ~/.claude/history.jsonl has user prompts + session IDs + timestamps, 2) ~/.claude/projects/<url-encoded-path>/<session-id>.jsonl has full transcripts, 3) ~/.claude/plans/ survives /clear. Key grep: -l for filename search, -o to extract JSON fields, pipe through sed 's/\n/\n/g' to decode. Use ls -lt for recency, ls -lS for size (longer sessions).


### [L048] [**---|-----] Async tests need queue drain
- **Uses**: 5 | **Velocity**: 0.01 | **Learned**: 2026-01-08 | **Last**: 2026-01-12 | **Category**: pattern | **Type**: constraint
> Tests calling async setters (functions using helix::async::invoke or ui_queue_update) must call helix::ui::UpdateQueue::instance().drain_queue_for_testing() before assertions. Without draining, the update is still pending and subjects won't have the new value. See test_printer_state.cpp for examples.


### [L050] [*----|-----] Post-compaction agent recovery
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2026-01-08 | **Last**: 2026-01-08 | **Category**: recovery | **Type**: informational
> When context compacts mid-session, agent outputs are summarized away but full content is preserved in the session JSONL. To recover: 1) Find current session file in ~/.claude/projects/<project>/<session-id>.jsonl (use ls -lt to find most recent), 2) Search for agent outputs: grep "tool_result.*agentId" <file>.jsonl, 3) Search for distinctive keywords from lost work to extract full analysis. The JSONL is append-only so nothing is truly lost - compaction only affects Claude's active context window.


### [L051] [*----|-----] LVGL timer lifetime safety
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2026-01-08 | **Last**: 2026-01-08 | **Category**: gotcha | **Type**: constraint
> When using lv_timer_create with object pointer as user_data, wrap in struct that captures alive_guard. Check alive_guard BEFORE dereferencing object pointer to prevent use-after-free if object destroyed during timer delay.


### [L051] [*----|-----] LVGL timer lifetime safety
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2026-01-08 | **Last**: 2026-01-08 | **Category**: gotcha | **Type**: constraint
> When using lv_timer_create with object pointer as user_data, wrap in struct that captures alive_guard. Check alive_guard BEFORE dereferencing object pointer to prevent use-after-free if object destroyed during timer delay.


### [L052] [***--|-----] Tag hv::EventLoop tests as slow
- **Uses**: 8 | **Velocity**: 0.01 | **Learned**: 2026-01-09 | **Last**: 2026-01-10 | **Category**: gotcha | **Type**: constraint
> Tests using hv::EventLoop (libhv network operations) MUST be tagged [slow] or they cause parallel test shards to hang indefinitely. This includes fixtures like MoonrakerRobustnessFixture, MoonrakerClientSecurityFixture, NewFeaturesTestFixture, EventTestFixture. The [slow] tag excludes them from default `make test-run` which uses filter `~[.] ~[slow]`.


### [L053] [***--|-----] Reset static fixture state in destructor
- **Uses**: 6 | **Velocity**: 0.01 | **Learned**: 2026-01-10 | **Last**: 2026-01-10 | **Category**: gotcha | **Type**: constraint
> Test fixtures using static state (e.g., static bool queue_initialized) MUST reset that state in the destructor. Otherwise, state persists across tests causing: 1) initialization to be skipped when it shouldn't, 2) shutdown to leave stale state for next test. Pattern: destructor calls shutdown(), then resets static flag to false.


### [L054] [***--|-----] Clear pending queues on shutdown
- **Uses**: 6 | **Velocity**: 0.01 | **Learned**: 2026-01-10 | **Last**: 2026-01-10 | **Category**: gotcha | **Type**: constraint
> Singleton queues (like UpdateQueue) MUST clear pending callbacks in shutdown(), not just null the timer. Without clearing, stale callbacks remain queued and execute on next init() with pointers to destroyed objects → use-after-free. Pattern: std::queue<T>().swap(pending_) to clear, then null timer.


### [L055] [**---|-----] LVGL pad_all excludes flex gaps
- **Uses**: 3 | **Velocity**: 0.01 | **Learned**: 2026-01-10 | **Last**: 2026-01-10 | **Category**: gotcha | **Type**: constraint
> `style_pad_all` only sets edge padding (top/bottom/left/right), NOT inter-item spacing. For zero-gap flex layouts, also need `style_pad_row="0"` (column) or `style_pad_column="0"` (row), or `style_pad_gap="0"` for both.


### [L056] [*----|-----] Re-stage clang-formatted files after commit
- **Uses**: 2 | **Velocity**: 0.01 | **Learned**: 2026-01-19 | **Last**: 2026-01-20 | **Category**: workflow | **Type**: informational
> Pre-commit hook auto-formats files but doesn't re-stage them. After committing, check git status for modified files and amend if they're just formatting changes.


### [L057] [*----|-----] Use lv_obj_safe_delete for LVGL cleanup
- **Uses**: 1 | **Velocity**: 0.0 | **Learned**: 2026-01-20 | **Last**: 2026-01-20 | **Category**: pattern | **Type**: constraint
> Always use lv_obj_safe_delete() instead of raw lv_obj_delete() - it guards against shutdown race conditions by checking lv_is_initialized() and lv_display_get_next() before deletion, and auto-nulls the pointer to prevent use-after-free
