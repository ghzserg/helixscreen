# LVGL 9.4 XML Attributes Reference

**Source:** `lvgl/src/others/xml/parsers/*.c` | **Updated:** 2025-12-18

---

## Quick Rules

- `style_image_recolor` not `style_img_recolor` (full words, no abbreviations)
- `width="content"` not `width="LV_SIZE_CONTENT"` (XML string, not C constant)
- `flex_align` doesn't exist → use `style_flex_main_place` / `style_flex_cross_place`
- `zoom` doesn't exist → use `scale_x` / `scale_y` (256 = 100%)
- Unknown attributes silently ignored

---

## Base Object (lv_obj)

All widgets inherit these.

### Layout

| Attr | Type | Notes |
|------|------|-------|
| `name` | str | For `lv_obj_find_by_name()` |
| `x`, `y` | size | px or % |
| `width`, `height` | size | px, %, or `"content"` |
| `align` | enum | `center`, `top_left`, `top_mid`, `top_right`, `bottom_*`, `left_mid`, `right_mid` |

### Flex

| Attr | Type | Notes |
|------|------|-------|
| `flex_flow` | enum | `row`, `column`, `row_wrap`, `column_wrap`, `*_reverse` |
| `flex_grow` | int | 0=fixed, 1+=grows |

### Flags (bool)

`hidden`, `clickable`, `checkable`, `scrollable`, `scroll_elastic`, `scroll_momentum`, `scroll_chain`, `ignore_layout`, `floating`, `overflow_visible`, `event_bubble`

### States (bool)

`checked`, `focused`, `disabled`, `pressed`, `hovered`

### Data Binding

```xml
<!-- Simple bindings as attributes -->
<lv_label bind_text="temp_subject"/>
<lv_label bind_text="temp" bind_text-fmt="%.1f°C"/>
<lv_slider bind_value="volume"/>

<!-- Conditional bindings as child elements -->
<lv_obj>
    <bind_flag_if_eq subject="panel" flag="hidden" ref_value="0"/>
</lv_obj>
<lv_button>
    <bind_state_if_eq subject="power" state="disabled" ref_value="0"/>
</lv_button>
```

**Operators:** `bind_flag_if_eq`, `bind_flag_if_not_eq`, `bind_flag_if_gt`, `bind_flag_if_ge`, `bind_flag_if_lt`, `bind_flag_if_le` (same for `bind_state_*`)

---

## Style Attributes (style_* prefix)

### Size & Spacing

| Attr | Notes |
|------|-------|
| `style_radius` | Corner radius |
| `style_pad_all`, `style_pad_hor`, `style_pad_ver` | Padding |
| `style_pad_gap` | Flex/grid gap |
| `style_margin_all`, `style_margin_hor`, `style_margin_ver` | Margin |
| `style_min_width`, `style_max_width`, `style_min_height`, `style_max_height` | Constraints |

### Background

| Attr | Notes |
|------|-------|
| `style_bg_color` | Hex: `0xff0000` |
| `style_bg_opa` | 0-255 or `"50%"` |
| `style_bg_grad_dir` | `none`, `hor`, `ver` |
| `style_bg_grad_color` | Gradient end |

### Border & Shadow

| Attr | Notes |
|------|-------|
| `style_border_color`, `style_border_width`, `style_border_opa` | |
| `style_border_side` | `none`, `top`, `bottom`, `left`, `right`, `full` |
| `style_shadow_width`, `style_shadow_color`, `style_shadow_opa` | |
| `style_shadow_offset_x`, `style_shadow_offset_y` | |

### Text

| Attr | Notes |
|------|-------|
| `style_text_color`, `style_text_font`, `style_text_opa` | |
| `style_text_align` | `left`, `right`, `center`, `auto` |

### Image

| Attr | Notes |
|------|-------|
| `style_image_recolor` | ⚠️ `image` not `img` |
| `style_image_recolor_opa` | |

### Flex Layout

| Attr | Notes |
|------|-------|
| `style_flex_main_place` | Main axis: `start`, `end`, `center`, `space_between`, `space_around`, `space_evenly` |
| `style_flex_cross_place` | Cross axis alignment |
| `style_flex_track_place` | Track alignment — **needed to center items with explicit widths** (not just wrap!) |

### Transforms

| Attr | Notes |
|------|-------|
| `style_transform_scale_x`, `style_transform_scale_y` | 256=100%, 512=200% |
| `style_transform_rotation` | 0.1° units (900=90°) |
| `style_translate_x`, `style_translate_y` | Offset |
| `style_opa` | Overall opacity |

---

## Reusable Styles

Define in `<styles>`, apply with child `<style>`. Drop `style_` prefix.

```xml
<styles>
    <style name="btn" bg_color="0x2196f3" radius="8"/>
</styles>

<lv_button>
    <style name="btn"/>
    <style name="btn_pressed" selector="pressed"/>
</lv_button>
```

**Selectors:** `default`, `pressed`, `checked`, `focused`, `disabled` | **Parts:** `main`, `indicator`, `knob`

**Combine:** `selector="indicator:pressed"` | **Remove:** `bg_color="remove"` | **Constants:** `bg_color="#primary"`

---

## Widgets

### lv_label

| Attr | Notes |
|------|-------|
| `text` | Content |
| `long_mode` | `wrap`, `scroll`, `dots`, `clip` |
| `bind_text` | `"subject 'format'"` |

### lv_image

| Attr | Notes |
|------|-------|
| `src` | Image name or path |
| `scale_x`, `scale_y` | 256=100% (⚠️ no `zoom`) |
| `rotation` | 0.1° units |
| `inner_align` | `center`, `stretch`, `tile` |

### lv_slider / lv_bar

| Attr | Notes |
|------|-------|
| `value` | `"50"` or `"50 true"` (animated) |
| `range` | `"0 100"` |
| `mode` | `normal`, `range`, `symmetrical` |
| `bind_value` | Subject name |

### lv_arc

| Attr | Notes |
|------|-------|
| `value`, `range` | Same as slider |
| `angles` | `"0 270"` (start end) |
| `mode` | `normal`, `reverse`, `symmetrical` |

### lv_textarea

| Attr | Notes |
|------|-------|
| `text`, `placeholder` | |
| `one_line`, `password_mode` | bool |

### lv_checkbox

| Attr | Notes |
|------|-------|
| `text` | Label |
| `checked` | bool state |

### lv_dropdown / lv_roller

| Attr | Notes |
|------|-------|
| `options` | `"A&#10;B&#10;C"` (use `&#10;` for newlines in XML!) |
| `selected` | Index |
| `bind_value` | Subject |

### lv_buttonmatrix

```xml
<lv_buttonmatrix map="'1' '2' '3' '\n' '4' '5' '6'" one_checked="true"/>
```

---

## Event Callbacks (9.4+)

```xml
<lv_button>
    <event_cb trigger="clicked" callback="my_handler"/>
</lv_button>
```

Register: `lv_xml_register_event_cb(nullptr, "my_handler", fn)`

**Triggers:** `clicked`, `value_changed`, `pressed`, `released`, `ready`, `cancel`

---

## Enums Reference

| Type | Values |
|------|--------|
| align | `center`, `top_left`, `top_mid`, `top_right`, `bottom_*`, `left_mid`, `right_mid` |
| flex_flow | `row`, `column`, `row_wrap`, `column_wrap`, `*_reverse` |
| flex_align | `start`, `end`, `center`, `space_between`, `space_around`, `space_evenly` |
| dir | `none`, `top`, `bottom`, `left`, `right`, `hor`, `ver`, `all` |
| border_side | `none`, `top`, `bottom`, `left`, `right`, `full` |
| text_align | `left`, `right`, `center`, `auto` |
| blend_mode | `normal`, `additive`, `subtractive`, `multiply` |
