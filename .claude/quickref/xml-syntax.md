# LVGL 9 XML Quick Reference

**Ultra-condensed syntax guide for common patterns. For complete reference, see `docs/LVGL9_XML_GUIDE.md`**

---

## Critical Syntax Rules

| Element | ✅ Correct | ❌ Wrong | Notes |
|---------|-----------|---------|-------|
| **Flags** | `hidden="true"` | `flag_hidden="true"` | No `flag_` prefix |
| **Clickable** | `clickable="true"` | `flag_clickable="true"` | Same - no prefix |
| **Scrollable** | `scrollable="false"` | `flag_scrollable="false"` | Same - no prefix |

---

## Flex Layout (3 Properties Required)

| Property | Purpose | CSS Equivalent | Values |
|----------|---------|----------------|--------|
| `style_flex_main_place` | Main axis distribution | `justify-content` | `start`, `center`, `end`, `space_evenly`, `space_around`, `space_between` |
| `style_flex_cross_place` | Cross axis alignment | `align-items` | `start`, `center`, `end` |
| `style_flex_track_place` | Track distribution (wrap) | `align-content` | `start`, `center`, `end`, `space_evenly`, `space_around`, `space_between` |

**❌ WRONG:** `<lv_obj flex_align="center center center">` - `flex_align` doesn't exist!

**✅ CORRECT:**
```xml
<lv_obj flex_flow="row"
        style_flex_main_place="center"
        style_flex_cross_place="center"
        style_flex_track_place="start">
```

**Verified `flex_flow` values:** `row`, `column`, `row_reverse`, `column_reverse`, `row_wrap`, `column_wrap`, `row_wrap_reverse`, `column_wrap_reverse`

---

## Conditional Bindings (Child Elements, NOT Attributes!)

**❌ WRONG:**
```xml
<lv_obj bind_flag_if_eq="subject=visible flag=hidden ref_value=0"/>
```

**✅ CORRECT:**
```xml
<lv_obj>
  <lv_obj-bind_flag_if_eq subject="visible" flag="hidden" ref_value="0"/>
</lv_obj>
```

**Available conditionals:** `bind_flag_if_eq`, `bind_flag_if_ne`, `bind_flag_if_gt`, `bind_flag_if_ge`, `bind_flag_if_lt`, `bind_flag_if_le`

---

## Component Instantiation (Always Add Name!)

**❌ WRONG:**
```xml
<controls_panel/>  <!-- name comes from view definition, but doesn't propagate -->
```

**✅ CORRECT:**
```xml
<controls_panel name="controls_panel"/>  <!-- Explicit name required -->
```

**Why:** `lv_obj_find_by_name()` returns NULL without explicit names

---

## Alignment Values

**Valid `align` values:** `left_mid`, `right_mid`, `top_left`, `top_mid`, `top_right`, `bottom_left`, `bottom_mid`, `bottom_right`, `center`

**❌ WRONG:** `align="left"` - not a valid value

---

## Event Callbacks (LVGL 9.4 Syntax)

**✅ CORRECT (v9.4):**
```xml
<lv_button>
  <event_cb trigger="clicked" callback="my_callback"/>
</lv_button>
```

**❌ WRONG (old v9.3 syntax):**
```xml
<lv_button>
  <lv_event-call_function trigger="clicked" callback="my_callback"/>
</lv_button>
```

---

## Quick Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Widget not found | Missing `name=` on component | Add explicit name |
| Flex not working | Missing properties | Add all 3: `main_place`, `cross_place`, `track_place` |
| Flag not applied | Using `flag_` prefix | Remove prefix, use `hidden` not `flag_hidden` |
| Binding not working | Using attribute syntax | Use child element: `<lv_obj-bind_flag_if_eq>` |
| Height collapses to 0 | `height="LV_SIZE_CONTENT"` bug | Use `flex_grow="1"` instead |

---

**Full documentation:** `docs/LVGL9_XML_GUIDE.md` (lazy-load when needed)
**Common patterns:** `docs/QUICK_REFERENCE.md`
**Pre-flight checklist:** `.claude/checklist.md`
