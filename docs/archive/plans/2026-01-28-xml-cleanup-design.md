# Design: XML Cleanup for Reactive Theme System

## Status: APPROVED

## Problem

The codebase has ~954 inline color styles in XML files. With the reactive theme system (Phase 1-2), many of these are now redundant because widgets apply shared styles by default. The XML is cluttered with boilerplate that:
- Obscures intentional styling decisions
- Makes theme changes fragile (inline styles override reactive styles)
- Creates maintenance burden

## Goal

Clean, minimal XML that relies on widget defaults and the theme system. When theme settings change, everything "just works" without touching XML.

## Design Decisions (from brainstorming session)

### Q1: text_heading default color
**Decision:** Keep as MUTED. Explicit `#text` overrides are intentional emphasis - KEEP those.

### Q2: Raw `<lv_label>` elements
**Decision:** Case-by-case. Convert to semantic widgets where possible, leave raw if there's a reason (custom font/size). Discuss ambiguous cases.

### Q3: Overlay backgrounds
**Decision:** Leave black overlays (`0x000000`) hardcoded. Universal, not themed.

### Q4: Button colors
**Decision:** DEFER until ui_button widget exists (Phase 2.6). Don't touch button colors in XML cleanup.

### Q5: Dividers and progress bars
**Decision:** Convert raw `<lv_obj>` divider patterns to `<divider_horizontal>`/`<divider_vertical>`. Progress bars need future ui_progress widget.

### Q6: `#app_bg` usage
**Decision:** Panels get automatic style. Token stays available for "ghost buttons" and edge cases. Ghost buttons can migrate to `variant="ghost"` when ui_button is ready.

### Q7: Icon colors
**Decision:** Convert ALL inline icon colors to semantic variants (`variant="muted"` instead of `color="#text_muted"`).

---

## Implementation Rules

### RULE 0: When in doubt, STOP AND DISCUSS

Do not guess. Do not interpret. Do not assume. If something is ambiguous or seems wrong, stop and discuss before changing anything.

### Definitely Remove (now defaults)

| Pattern | Action |
|---------|--------|
| `<text_body style_text_color="#text">` | Remove attribute |
| `<text_heading style_text_color="#text_muted">` | Remove attribute |
| `<text_small style_text_color="#text_muted">` | Remove attribute |
| `<text_xs style_text_color="#text_muted">` | Remove attribute |
| `<card style_bg_color="#card_bg">` | Remove attribute |
| `<dialog style_bg_color="...">` | Remove attribute |

### Convert to Semantic Widgets

| Pattern | Convert To |
|---------|------------|
| `<lv_label style_text_color="#text">` | `<text_body>` (case-by-case) |
| `<lv_label style_text_color="#text_muted">` | `<text_small>` or `<text_heading>` |
| `<lv_obj>` divider pattern | `<divider_horizontal>` / `<divider_vertical>` |
| `<icon color="#text_muted">` | `<icon variant="muted">` |
| `<icon color="#text">` | `<icon variant="text">` |
| `<icon color="#primary">` | `<icon variant="primary">` |
| `<icon color="#danger">` | `<icon variant="danger">` |
| etc. for all semantic colors | etc. for all variants |

### Intentional Overrides - KEEP

| Pattern | Reason |
|---------|--------|
| `<text_heading style_text_color="#text">` | Intentional emphasis override |
| Black overlay `0x000000` | Universal, not themed |
| Custom icon colors not matching any variant | Discuss first |

### DEFER (blocked by missing widgets)

| Pattern | Blocked By |
|---------|------------|
| Button colors (`#primary`, `#danger`, etc.) | Phase 2.6 - ui_button |
| Spinner colors | Phase 2.4 - ui_spinner |
| Progress bar colors | Future ui_progress widget |

---

## Implementation Phases

### Prerequisites (must complete first)
- Phase 2.3: Spinner/severity styles in theme_core
- Phase 2.4: ui_spinner reactive style
- Phase 2.5: ui_severity_card reactive styles
- Phase 2.6: ui_button semantic widget
- Phase 3: Remove brittle preview code

### Phase 4: XML Cleanup

#### 4.1: Text widgets
Remove redundant `style_text_color` from `text_body`, `text_heading`, `text_small`, `text_xs`

#### 4.2: Card/dialog
Remove redundant `style_bg_color` from `<card>` and `<dialog>` elements

#### 4.3: Icons
Convert inline `color="..."` to `variant="..."` for all icons

#### 4.4: Dividers
Convert raw `<lv_obj>` divider patterns to `<divider_horizontal>`/`<divider_vertical>`

#### 4.5: Raw labels
Convert to semantic text widgets (case-by-case, discuss ambiguous)

#### 4.6: Buttons
Convert to `<ui_button variant="...">` (after 2.6 complete)

#### 4.7: Final review
Audit remaining inline styles, discuss any ambiguous cases

---

## Completion Criteria (NON-NEGOTIABLE)

For EVERY step:

1. **Done means DONE** - meets spec completely, not "good enough"
2. **No excuses** - "pre-existing problem" is not a pass. Fix it or document as blocker and STOP.
3. **No moving on** until step is fully complete and verified
4. **If spec seems broken** → STOP and DISCUSS. No improvising.

### Verification checklist per step:
- [ ] All tests pass (existing + new)
- [ ] Visual verification where applicable
- [ ] No regressions introduced
- [ ] Matches spec exactly
- [ ] Ambiguous cases discussed and resolved before implementation

### If blocked:
- STOP immediately
- Document what's blocking
- Discuss before proceeding
- Do NOT work around it silently

### Code review findings:
- ALL findings must be addressed before moving on
- No "we'll fix that later"
- No "that's out of scope"
- Fix it or discuss why it can't be fixed

---

## Process

1. TDD where applicable (mostly visual verification for XML)
2. Implement in bite-sized chunks (1-3 files per commit)
3. Code review at logical chunks
4. **DISCUSS any ambiguous cases before changing**
5. Review of related functionality at end of section
6. Subagent delegation for implementation work
7. Main context reserved for critical thinking/coordination

---

## Future Work (out of scope)

- `ui_progress` widget for themed progress bars
- `app_bg_style_` for panels (if needed)
- Button `variant="ghost"` for app_bg buttons
