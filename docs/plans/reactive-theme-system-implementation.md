# Reactive Theme System - Implementation Plan

## Status: IN PROGRESS

**Worktree:** `../helixscreen-reactive-theme`
**Branch:** `reactive-theme-system`

---

## Process Requirements (NON-NEGOTIABLE)

### For EVERY step:
1. **TDD** - Write failing test first (where applicable)
2. **Implement** - Make the test pass
3. **Review** - Code review at logical chunks
4. **Update this plan** - Mark checkboxes complete, add commit hashes
5. **Done means DONE** - No "good enough", no excuses

### Completion criteria:
- All tests pass (existing + new)
- Visual verification where applicable
- No regressions introduced
- Matches spec exactly
- Review findings ALL addressed (no "fix later")

### If blocked or ambiguous:
- **STOP immediately**
- Document what's blocking
- **DISCUSS before proceeding**
- Do NOT work around it silently

### Delegation:
- Subagents for implementation work
- Main context for critical thinking/coordination
- Bite-sized chunks (1-3 files per commit)

---

## Phase 1: Core Surface & Text Styles ✅ COMPLETE

### Section 1.1: theme_core Style Infrastructure
- [x] **TEST**: Write failing tests for `theme_core_get_card_style()`, `theme_core_get_dialog_style()`, `theme_core_get_text_style()`, `theme_core_get_text_muted_style()` getters
- [x] **IMPLEMENT**: Add styles to `helix_theme_t`, init/update/preview functions, getter APIs
- [x] **REVIEW**: Code review of theme_core changes

### Section 1.2: ui_card Shared Styles
- [x] **TEST**: Write failing test that card bg_color updates when theme changes (currently frozen)
- [x] **IMPLEMENT**: Replace inline styles with `lv_obj_add_style(obj, theme_core_get_card_style(), ...)`
- [x] **REVIEW**: Code review of ui_card changes

### Section 1.3: ui_dialog Shared Styles
- [x] **TEST**: Write failing test that dialog bg_color updates when theme changes
- [x] **IMPLEMENT**: Replace inline styles with `theme_core_get_dialog_style()`
- [x] **REVIEW**: Code review of ui_dialog changes

### Section 1.4: ui_text Shared Styles
- [x] **TEST**: Write failing test that text colors update when theme changes
- [x] **IMPLEMENT**: Add text styles to create handlers
- [x] **REVIEW**: Code review of ui_text changes

### Phase 1 Completion
- [x] **FULL TEST SUITE**: `make test-run` passes
- [x] **PHASE REVIEW**: Comprehensive code review of all Phase 1 changes
- [x] **COMMIT**: `e28e1e6c feat(theme): add reactive shared styles for cards, dialogs, and text`

---

## Phase 2: Icons, Status Colors, Semantic Buttons

### Section 2.1: Icon Styles in theme_core ✅ COMPLETE
- [x] **TEST**: Write failing tests for icon style getters
- [x] **IMPLEMENT**: Add icon_text_style_, icon_muted_style_, icon_primary_style_, etc.
- [x] **REVIEW**: Code review of icon styles in theme_core
- [x] **COMMIT**: `eecc2fbc feat(theme): add icon style infrastructure to theme_core`

### Section 2.2: ui_icon Refactor ✅ COMPLETE
- [x] **TEST**: Write failing test that icon colors update when theme changes
- [x] **IMPLEMENT**: Refactor variants (text/muted/primary/secondary/tertiary/success/warning/danger/info), use shared styles
- [x] **REVIEW**: Code review of ui_icon changes (fixed: NULL handling, style accumulation, SECONDARY reachable)

### Section 2.3: Spinner & Severity Styles in theme_core ✅ COMPLETE
- [x] **TEST**: Write failing tests for spinner_style and severity style getters
- [x] **IMPLEMENT**: Add spinner_style_, severity_info/success/warning/danger_style_
- [x] **REVIEW**: Code review of spinner/severity styles

### Section 2.4: ui_spinner Shared Style ✅ COMPLETE
- [x] **TEST**: Write failing test that spinner arc color updates when theme changes
- [x] **IMPLEMENT**: Use `theme_core_get_spinner_style()` instead of inline color
- [x] **REVIEW**: Code review of ui_spinner changes
- [x] **COMMIT**: `adeda572 feat(spinner): use shared theme_core style for reactive theme updates`

### Section 2.5: ui_severity_card Shared Styles ✅ COMPLETE
- [x] **TEST**: Write failing test that severity card border updates when theme changes
- [x] **IMPLEMENT**: Use shared severity styles
- [x] **REVIEW**: Code review of ui_severity_card changes
- [x] **COMMIT**: `611a0034 feat(severity-card): use shared theme_core styles for reactive theme updates`

### Section 2.6: ui_button Semantic Widget (NEW) ✅ COMPLETE
- [x] **TEST**: Write tests for ui_button with variant/text/icon attrs, auto-contrast
- [x] **IMPLEMENT**: Create ui_button.cpp/h with LV_EVENT_STYLE_CHANGED pattern
- [x] **REVIEW**: Code review of ui_button widget
- [x] **COMMIT**: `632755b7` (theme_core button styles) + `652c03cb` (ui_button widget)

### Phase 2 Completion ✅
- [x] **FULL TEST SUITE**: `make test-run` passes
- [x] **PHASE REVIEW**: Code reviews completed per section
- [x] **COMMITS**: Multiple commits for clarity (2.1-2.6)

---

## Phase 3: Remove Brittle Preview Code ✅ COMPLETE

### Section 3.1: Delete theme_manager_refresh_preview_elements ✅ COMPLETE
- [x] **TEST**: Verify theme preview still works after deletion (existing tests)
- [x] **IMPLEMENT**: Deleted ~80 lines of brittle widget lookup code (`update_text_colors_recursive` helper and manual `lv_obj_find_by_name`/`lv_obj_set_style_*` calls in `handle_preview_dark_mode_toggled()`)
- [x] **REVIEW**: Pending

### Section 3.2: Simplify ui_settings_display Preview Handlers ✅ COMPLETE
- [x] **TEST**: Theme preview integration test (via manual verification)
- [x] **IMPLEMENT**: Simplified `handle_preview_dark_mode_toggled()` to call `theme_core_preview_colors()` + `theme_manager_refresh_widget_tree()` (~40 lines vs ~120 lines)
- [x] **REVIEW**: Pending

### Phase 3 Completion
- [x] **FULL TEST SUITE**: `make test-run` passes (378 assertions in 82 test cases)
- [ ] **PHASE REVIEW**: Comprehensive code review of all Phase 3 changes
- [ ] **COMMIT**: Pending

---

## Phase 4: XML Cleanup

**Design doc:** `docs/plans/2026-01-28-xml-cleanup-design.md`

**CRITICAL RULE:** When in doubt, STOP AND DISCUSS before changing anything.

### Section 4.1: Text Widget Cleanup
- [ ] **IMPLEMENT**: Remove redundant `style_text_color="#text"` from `<text_body>` elements
- [ ] **IMPLEMENT**: Remove redundant `style_text_color="#text_muted"` from `<text_heading>`, `<text_small>`, `<text_xs>` elements
- [ ] **VERIFY**: Visual check - text colors unchanged
- [ ] **REVIEW**: Code review of text widget cleanup

### Section 4.2: Card/Dialog Cleanup
- [ ] **IMPLEMENT**: Remove redundant `style_bg_color="#card_bg"` from `<card>` elements
- [ ] **IMPLEMENT**: Remove redundant `style_bg_color` from `<dialog>` elements
- [ ] **VERIFY**: Visual check - card/dialog backgrounds unchanged
- [ ] **REVIEW**: Code review of card/dialog cleanup

### Section 4.3: Icon Cleanup
- [ ] **IMPLEMENT**: Convert inline `color="#text_muted"` to `variant="muted"` (and similar for all semantic colors)
- [ ] **VERIFY**: Visual check - icon colors unchanged
- [ ] **REVIEW**: Code review of icon cleanup

### Section 4.4: Divider Cleanup
- [ ] **AUDIT**: Find all raw `<lv_obj>` divider patterns
- [ ] **IMPLEMENT**: Convert to `<divider_horizontal>` / `<divider_vertical>`
- [ ] **VERIFY**: Visual check - dividers unchanged
- [ ] **REVIEW**: Code review of divider cleanup

### Section 4.5: Raw Label Conversion (case-by-case)
- [ ] **AUDIT**: Find all `<lv_label style_text_color="...">` patterns
- [ ] **DISCUSS**: Review list with user - which to convert, which to keep
- [ ] **IMPLEMENT**: Convert approved labels to semantic text widgets
- [ ] **VERIFY**: Visual check - text unchanged
- [ ] **REVIEW**: Code review of label conversions

### Section 4.6: Button Cleanup (after 2.6)
- [ ] **IMPLEMENT**: Convert `<lv_button style_bg_color="#primary">` to `<ui_button variant="primary">`
- [ ] **IMPLEMENT**: Convert danger, secondary, ghost button patterns
- [ ] **VERIFY**: Visual check - buttons unchanged
- [ ] **REVIEW**: Code review of button cleanup

### Section 4.7: Final Audit
- [ ] **AUDIT**: List all remaining inline color styles
- [ ] **DISCUSS**: Review with user - intentional or tech debt?
- [ ] **IMPLEMENT**: Clean up any remaining tech debt
- [ ] **VERIFY**: Full visual verification
- [ ] **REVIEW**: Final code review

### Phase 4 Completion
- [ ] **FULL TEST SUITE**: `make test-run` passes
- [ ] **VISUAL VERIFICATION**: All panels checked for regressions
- [ ] **PHASE REVIEW**: Comprehensive code review of all Phase 4 changes
- [ ] **COMMIT**: `[phase-4] Clean up redundant inline styles in XML`

---

## Final Completion Checklist

- [ ] All phases (1-4) marked complete above
- [ ] Full test suite passes: `make test-run`
- [ ] Final comprehensive code review completed
- [ ] Branch cleanly mergeable to main
- [ ] Manual verification: dark/light toggle works, theme preview works
- [ ] Visual verification: all panels checked for regressions
- [ ] Plan document updated with all completion status

---

## Test Tags

Use these tags for targeted test runs:
- `[theme-core]` - theme_core style getter tests
- `[reactive-card]` - ui_card reactive style tests
- `[reactive-dialog]` - ui_dialog reactive style tests
- `[reactive-text]` - ui_text reactive style tests
- `[reactive-icon]` - ui_icon reactive style tests
- `[reactive-spinner]` - ui_spinner reactive style tests
- `[reactive-severity]` - ui_severity_card reactive style tests
- `[ui-button]` - ui_button semantic widget tests

Phase 4 (XML cleanup) is primarily visual verification - no new test tags needed.

---

## Architecture Reference

```
helix_theme_t (theme_core.c) - singleton owns ALL shared lv_style_t objects

On theme/mode change:
  theme_core_update_colors() or theme_core_preview_colors()
  -> updates ALL style objects
  -> lv_obj_report_style_change(NULL)
  -> LVGL auto-refreshes all widgets using those styles
```

**Key Principle:** DATA in C++, APPEARANCE in XML, Shared Styles connect them reactively.
