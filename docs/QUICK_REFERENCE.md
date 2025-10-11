# LVGL 9 XML UI Quick Reference

## Common Patterns

### Create XML Component with Reactive Binding

```cpp
// Header
lv_obj_t* ui_panel_xyz_create(lv_obj_t* parent);
void ui_panel_xyz_update(const char* text, int value);

// Implementation
static lv_subject_t text_subject, value_subject;
static char text_buf[128], value_buf[32];

lv_obj_t* ui_panel_xyz_create(lv_obj_t* parent) {
    // 1. Init subjects
    lv_subject_init_string(&text_subject, text_buf, NULL, sizeof(text_buf), "Initial");
    lv_subject_init_string(&value_subject, value_buf, NULL, sizeof(value_buf), "0");

    // 2. Register globally
    lv_xml_register_subject(NULL, "text_data", &text_subject);
    lv_xml_register_subject(NULL, "value_data", &value_subject);

    // 3. Create XML
    return (lv_obj_t*)lv_xml_create(parent, "panel_xyz", nullptr);
}

void ui_panel_xyz_update(const char* text, int value) {
    lv_subject_copy_string(&text_subject, text);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_subject_copy_string(&value_subject, buf);
}
```

### XML Component Structure

```xml
<component>
    <view extends="lv_obj" width="100%" height="100%">
        <!-- Bound labels update automatically -->
        <lv_label bind_text="text_data" style_text_color="#text_primary"/>
        <lv_label bind_text="value_data" style_text_font="montserrat_28"/>
    </view>
</component>
```

### Flex Layout (Navbar Pattern)

```xml
<view extends="lv_obj"
      flex_flow="column"
      style_flex_main_place="space_evenly"
      style_flex_cross_place="center"
      style_flex_track_place="center">

    <lv_button width="70" height="70">
        <lv_label align="center" text="#icon_name"/>
    </lv_button>
</view>
```

### Constants

```xml
<!-- globals.xml -->
<consts>
    <color name="bg_dark" value="0x1a1a1a"/>
    <px name="width" value="102"/>
    <percent name="card" value="45%"/>
    <str name="icon" value=""/>  <!-- UTF-8 char -->
</consts>

<!-- Usage -->
<lv_obj style_bg_color="#bg_dark" width="#width"/>
```

## Subject Types

```cpp
// String
char buf[128];
lv_subject_init_string(&subj, buf, NULL, sizeof(buf), "init");
lv_subject_copy_string(&subj, "new text");

// Integer
lv_subject_init_int(&subj, 0);
lv_subject_set_int(&subj, 42);

// Color
lv_subject_init_color(&subj, lv_color_hex(0xFF0000));
lv_subject_set_color(&subj, lv_color_hex(0x00FF00));
```

## XML Bindings

| Widget | Binding | Subject Type |
|--------|---------|--------------|
| `lv_label` | `bind_text="name"` | String |
| `lv_slider` | `bind_value="name"` | Integer |
| `lv_arc` | `bind_value="name"` | Integer |
| `lv_dropdown` | `bind_value="name"` | Integer |

## Registration Order

```cpp
// ALWAYS follow this order:
lv_xml_register_font(...);                    // 1. Fonts
lv_xml_register_image(...);                   // 2. Images
lv_xml_component_register_from_file(...);     // 3. Components (globals first!)
lv_subject_init_string(...);                  // 4. Init subjects
lv_xml_register_subject(...);                 // 5. Register subjects
lv_xml_create(...);                           // 6. Create UI
```

## Common Style Properties

```xml
<!-- Layout -->
width="100" height="200" x="50" y="100"
flex_flow="row|column|row_wrap|column_wrap"
flex_grow="1"
align="center|top_left|bottom_right|..."

<!-- Colors & Opacity -->
style_bg_color="#hexcolor"
style_bg_opa="0%|50%|100%"
style_text_color="#hexcolor"

<!-- Spacing -->
style_pad_all="10"
style_pad_left="10" style_pad_right="10"
style_margin_top="10" style_margin_bottom="10"

<!-- Borders & Radius -->
style_border_width="2"
style_radius="8"
style_shadow_width="10"

<!-- Flex Alignment -->
style_flex_main_place="start|end|center|space_between|space_evenly|space_around"
style_flex_cross_place="start|end|center"
style_flex_track_place="start|end|center|space_between|space_evenly|space_around"
```

## Testing & Screenshots

```bash
# Build and screenshot
./scripts/screenshot.sh [binary_name] [output_name]

# Examples
./scripts/screenshot.sh                    # guppy-ui-proto, timestamp
./scripts/screenshot.sh test_nav navbar    # test_nav, navbar.png
./scripts/screenshot.sh test_home_panel hp # test_home_panel, hp.png
```

## File Structure

```
ui_xml/
  ├── globals.xml           # Theme constants (colors, sizes, icons)
  ├── navigation_bar.xml    # Navbar component
  └── home_panel.xml        # Home panel component

src/
  └── ui_panel_home_xml.cpp # C++ wrapper for home_panel.xml

include/
  └── ui_panel_home_xml.h   # Header for C++ wrapper

scripts/
  ├── screenshot.sh          # Build, run, capture screenshot
  └── generate-icon-consts.py # Generate icon constants
```

## Debugging

```cpp
// Add debug output to verify subject updates
void ui_panel_xyz_update(const char* text, int value) {
    printf("DEBUG: Updating text to: %s\n", text);
    lv_subject_copy_string(&text_subject, text);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    printf("DEBUG: Updating value to: %s\n", buf);
    lv_subject_copy_string(&value_subject, buf);
}
```

## Gotchas

❌ **Don't:** Use stack buffers for string subjects
```cpp
char buf[128];  // Stack - WRONG!
lv_subject_init_string(&subj, buf, ...);
```

✅ **Do:** Use static or heap buffers
```cpp
static char buf[128];  // Static - CORRECT
lv_subject_init_string(&subj, buf, ...);
```

---

❌ **Don't:** Use `flex_align` attribute in XML
```xml
<view flex_align="space_evenly center">
```

✅ **Do:** Use `style_flex_*_place` properties
```xml
<view style_flex_main_place="space_evenly" style_flex_cross_place="center">
```

---

❌ **Don't:** Register subjects after creating XML
```cpp
lv_xml_create(...);
lv_xml_register_subject(...);  // Too late!
```

✅ **Do:** Register subjects before XML creation
```cpp
lv_xml_register_subject(...);
lv_xml_create(...);
```
