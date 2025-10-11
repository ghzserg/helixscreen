# LVGL XML UI System Reference

Based on LVGL Editor tutorials and LVGL 9.3 documentation.

## Project Structure

LVGL Editor projects follow this standard structure:

```
project_name/
├── project.xml          # Project config (targets, display sizes)
├── globals.xml          # Global resources (fonts, images, subjects, styles, consts)
├── screens/             # Screen XML files (<screen> tag)
├── components/          # Reusable components (<component> tag)
├── widgets/             # Custom widget definitions
├── ui_project_name.c    # User code (calls init_gen)
└── ui_project_name_gen.c # Generated code (auto-created by LVGL Editor)
```

## Globals.xml Structure

The `globals.xml` file contains all shared resources for the project:

```xml
<globals name="project_name">
    <api>
        <!-- Enumeration definitions -->
        <enumdef name="my_enum">
            <enum name="value1" value="0"/>
            <enum name="value2" value="1"/>
        </enumdef>
    </api>

    <consts>
        <!-- Constants: <px>, <int>, <color>, <string> -->
        <int name="nav_width" value="102"/>
        <px name="padding_normal" value="12"/>
        <color name="primary_color" value="0xff4444"/>
        <string name="app_name" value="My App"/>
    </consts>

    <styles>
        <!-- Reusable style definitions -->
        <style name="style_button"
               bg_color="0x111"
               radius="8"
               pad_all="12"/>
    </styles>

    <subjects>
        <!-- Reactive data bindings -->
        <int name="active_panel" value="0" help="Currently active panel index"/>
        <string name="status_text" value="Ready"/>
        <float name="temperature" value="25.0"/>
    </subjects>

    <fonts>
        <!-- Font definitions -->
        <bin name="font_medium" as_file="false"
             src_path="fonts/Montserrat_Medium.ttf"
             size="20" bpp="4"/>
    </fonts>

    <images>
        <!-- Image assets -->
        <file name="logo" src_path="images/logo.png"/>
    </images>
</globals>
```

## Component Structure

Components are reusable UI pieces defined with the `<component>` tag:

```xml
<component>
    <!-- Optional: Define component API properties -->
    <api>
        <prop name="text" type="string" default="Click me"/>
        <prop name="subject" type="subject" default=""/>
        <prop name="enabled" type="bool" default="true"/>
    </api>

    <!-- Optional: Local constants -->
    <consts>
        <int name="button_size" value="36"/>
    </consts>

    <!-- Optional: Local styles -->
    <styles>
        <style name="style_base" bg_color="0x333" text_color="0xfff"/>
    </styles>

    <!-- The actual UI definition -->
    <view extends="lv_button" width="#button_size">
        <!-- Use API properties with $ prefix -->
        <lv_label text="$text" align="center"/>

        <!-- Reference local or global constants with # prefix -->
        <style name="style_base"/>
    </view>
</component>
```

## Data Binding with Subjects

Subjects enable reactive data binding between data and UI elements.

### Defining Subjects

In `globals.xml`:
```xml
<subjects>
    <int name="volume" value="10"/>
    <string name="status" value="Ready"/>
    <float name="temp" value="25.0"/>
</subjects>
```

### Binding to Widgets

```xml
<!-- Bind label text to a subject -->
<lv_label bind_text="$volume"/>

<!-- Bind with format string -->
<lv_label bind_text="$temp" bind_text-fmt="%.1f°C"/>

<!-- Bind slider value -->
<lv_slider bind_value="$volume" min_value="0" max_value="100"/>

<!-- Conditional flag binding -->
<lv_obj bind_flag_if_eq="subject=$status flag=hidden ref_value=0"/>
```

## Event Handling

### Custom Callback Events - Use C Code, Not XML

**IMPORTANT**: For custom callback events with application-specific logic, **add event handlers in C code**, not in XML.

XML event elements like `<event_cb>` and `<lv_event-call_function>` are internal implementation details and should not be used directly.

**Correct approach:**

**XML** (clean, no event elements):
```xml
<lv_button>
    <lv_label text="Click me"/>
</lv_button>
```

**C Code** (add handlers after XML creation):
```c
// Get button widget from XML-created UI
lv_obj_t* button = lv_obj_get_child(parent, index);

// Add event handler
lv_obj_add_event_cb(button, my_button_clicked, LV_EVENT_CLICKED, user_data);

// Event handler function
static void my_button_clicked(lv_event_t* e) {
    void* user_data = lv_event_get_user_data(e);
    // Handle click
}
```

### XML Event Elements - For Built-in Actions Only

XML supports built-in event types for common reactive patterns:

### Event Triggers

Common trigger values:
- `"clicked"` - Button click (press + release)
- `"pressed"` - Button pressed down
- `"released"` - Button released
- `"long_pressed"` - Long press detected
- `"long_pressed_repeat"` - Held down (repeats)
- `"all"` - Any event

### Custom Callback Events

**XML:**
```xml
<lv_button>
    <lv_label text="Click"/>
    <lv_event-call_function trigger="clicked" callback="my_handler" user_data="42"/>
</lv_button>
```

**C Code Registration (MUST happen BEFORE loading XML):**
```c
// Register callback with LVGL XML system
lv_xml_register_event_cb(NULL, "my_handler", my_handler_function);

// Standard LVGL event handler signature
void my_handler_function(lv_event_t* e) {
    const char* user_data = lv_event_get_user_data(e);
    int value = atoi(user_data);

    // Handle event
    LV_LOG_USER("Button clicked with value: %d", value);
}
```

### Subject Manipulation Events

**Set Subject Value:**
```xml
<lv_button>
    <subject_set_int_event trigger="clicked" subject="$counter" value="0"/>
</lv_button>
```

**Increment/Decrement Subject:**
```xml
<lv_button>
    <lv_label text="+"/>
    <subject_increment_event trigger="clicked" subject="$volume" step="1" max="100"/>
</lv_button>

<lv_button>
    <lv_label text="-"/>
    <subject_increment_event trigger="clicked" subject="$volume" step="-1" min="0"/>
</lv_button>
```

### Screen Navigation Events

**Load Existing Screen:**
```xml
<lv_button>
    <screen_load_event trigger="clicked" screen="settings_screen"
                       anim_type="fade_in" duration="300"/>
</lv_button>
```

**Create Screen Dynamically:**
```xml
<lv_button>
    <screen_create_event trigger="clicked" screen="dialog_screen"/>
</lv_button>
```

## Styles

### Defining Styles

**Global styles** (in globals.xml):
```xml
<styles>
    <style name="style_button"
           bg_color="0x111"
           text_color="0xfff"
           radius="8"/>
</styles>
```

**Local styles** (in component):
```xml
<component>
    <styles>
        <style name="style_dark" bg_color="0x333"/>
    </styles>

    <view>
        <style name="style_dark"/>
    </view>
</component>
```

### Applying Styles

**By name:**
```xml
<lv_button>
    <style name="style_button"/>
</lv_button>
```

**With selector (state-based):**
```xml
<lv_button>
    <style name="style_base"/>
    <style name="style_pressed" selector="pressed"/>
    <style name="style_checked" selector="checked"/>
</lv_button>
```

**Inline styles:**
```xml
<lv_button style_bg_color="0x111" style_radius="8" style_pad_all="12">
</lv_button>
```

### Conditional Style Binding

```xml
<lv_obj>
    <bind_style name="style_dark" subject="$dark_theme" ref_value="1"/>
</lv_obj>
```

## Layouts

### Flex Layout

```xml
<lv_obj flex_flow="row"
        style_flex_main_place="space_between"
        style_flex_cross_place="center">

    <lv_label text="Left"/>
    <lv_label text="Center" flex_grow="1"/>
    <lv_label text="Right"/>
</lv_obj>
```

**Flex flow options:**
- `"row"` - Horizontal left to right
- `"row_reverse"` - Horizontal right to left
- `"column"` - Vertical top to bottom
- `"column_reverse"` - Vertical bottom to top

**Flex alignment:**
- `style_flex_main_place`: `start`, `end`, `center`, `space_between`, `space_around`, `space_evenly`
- `style_flex_cross_place`: `start`, `end`, `center`, `stretch`

### Starting New Track

```xml
<lv_obj flex_flow="row">
    <lv_button/>
    <lv_button/>
    <lv_button flex_in_new_track="true"/>  <!-- Starts on new line -->
</lv_obj>
```

## Constants and References

### Using Constants

```xml
<!-- Define in globals.xml -->
<consts>
    <int name="nav_width" value="102"/>
    <color name="primary" value="0xff4444"/>
</consts>

<!-- Reference with # prefix -->
<lv_obj width="#nav_width" style_bg_color="#primary"/>
```

### Using API Properties

```xml
<component>
    <api>
        <prop name="title" type="string" default="Title"/>
    </api>

    <view>
        <!-- Reference API props with $ prefix -->
        <lv_label text="$title"/>
    </view>
</component>
```

## Widget Hierarchy

### Child Elements

```xml
<lv_obj>
    <!-- Children are automatically added to parent -->
    <lv_label text="Child 1"/>
    <lv_label text="Child 2"/>
</lv_obj>
```

### Alignment

```xml
<lv_label text="Centered" align="center"/>
<lv_label text="Top Right" align="top_right"/>
```

**Alignment options:** `center`, `top_left`, `top_mid`, `top_right`, `bottom_left`, `bottom_mid`, `bottom_right`, `left_mid`, `right_mid`

## Common Patterns

### Clickable Icon Button

```xml
<lv_button width="64" height="64"
           style_bg_opa="0%"
           style_border_width="0"
           style_shadow_width="0">
    <lv_label text="" style_text_font="fa_icons_64" align="center"/>
    <lv_event-call_function trigger="clicked" callback="icon_clicked" user_data="0"/>
</lv_button>
```

### Reactive Counter with Buttons

```xml
<!-- In globals.xml -->
<subjects>
    <int name="counter" value="0"/>
</subjects>

<!-- In component -->
<lv_obj flex_flow="row">
    <lv_button>
        <lv_label text="-"/>
        <subject_increment_event subject="$counter" step="-1" min="0"/>
    </lv_button>

    <lv_label bind_text="$counter" bind_text-fmt="%d" flex_grow="1"/>

    <lv_button>
        <lv_label text="+"/>
        <subject_increment_event subject="$counter" step="1" max="100"/>
    </lv_button>
</lv_obj>
```

### Tabbed Navigation

```xml
<lv_obj flex_flow="row">
    <lv_button flex_grow="1">
        <lv_label text="Home"/>
        <subject_set_int_event subject="$active_tab" value="0"/>
    </lv_button>
    <lv_button flex_grow="1">
        <lv_label text="Settings"/>
        <subject_set_int_event subject="$active_tab" value="1"/>
    </lv_button>
</lv_obj>

<lv_obj>
    <bind_flag_if_eq subject="$active_tab" flag="hidden" ref_value="1"/>
    <!-- Home tab content -->
</lv_obj>

<lv_obj>
    <bind_flag_if_eq subject="$active_tab" flag="hidden" ref_value="0"/>
    <!-- Settings tab content -->
</lv_obj>
```

## Important Notes

1. **Event element naming**: Use `<lv_event-call_function>` for runtime XML loading
2. **Callback registration**: Always register callbacks BEFORE loading XML components
3. **Constants**: Use `#` prefix for constants, `$` for API properties and subjects
4. **Subjects**: Great for reactive UIs, but only work for compatible properties (text, value, flags)
5. **Style properties**: Cannot use `bind_` attributes; need custom C code for reactive style changes
6. **Event children order**: Place event elements AFTER visual children (labels, etc.)

## Resources

- LVGL Editor Tutorials: https://github.com/lvgl/lvgl_editor/tree/master/tutorials
- LVGL XML Documentation: https://docs.lvgl.io/master/details/xml/
- LVGL Events: https://docs.lvgl.io/latest/en/html/details/xml/ui_elements/events.html
