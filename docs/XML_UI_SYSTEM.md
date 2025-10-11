# LVGL 9 XML UI System Guide

## Overview

This project uses LVGL 9's declarative XML UI system with reactive data binding through the Subject-Observer pattern. This approach separates UI layout (XML) from business logic (C++), similar to modern web frameworks.

## Architecture

```
┌─────────────────┐
│  XML Component  │ ← Declarative UI layout
│  (home_panel)   │
└────────┬────────┘
         │ bind_text="subject_name"
         ↓
┌─────────────────┐
│    Subjects     │ ← Reactive data (strings, ints, etc.)
│  (status_text)  │
└────────┬────────┘
         │ lv_subject_copy_string()
         ↓
┌─────────────────┐
│  C++ Wrapper    │ ← Business logic & updates
│ (ui_panel_*.cpp)│
└─────────────────┘
```

## Key Concepts

### 1. XML Components

XML files define reusable UI components with declarative syntax:

**Location:** `ui_xml/*.xml`

**Structure:**
```xml
<component>
    <api>
        <!-- Component properties (optional) -->
        <prop type="int" name="active_panel" default="0"/>
    </api>

    <view extends="lv_obj" ...>
        <!-- Widget hierarchy -->
    </view>
</component>
```

**Example:** `ui_xml/home_panel.xml`

### 2. Subjects (Reactive Data)

Subjects are observable data containers that automatically update all bound widgets when changed.

**Types:**
- `lv_subject_init_string()` - String data
- `lv_subject_init_int()` - Integer data
- `lv_subject_init_pointer()` - Pointer data
- `lv_subject_init_color()` - Color data

**Lifecycle:**
1. Create subject in C++
2. Register globally with `lv_xml_register_subject()`
3. Bind widgets in XML with `bind_text="subject_name"`
4. Update from C++ with `lv_subject_copy_string()` or `lv_subject_set_int()`

### 3. Data Binding

XML widgets can bind to subjects using special attributes:

**Supported Bindings:**
- `bind_text` - Label text (string subjects)
- `bind_value` - Slider/arc value (integer subjects)

**Example:**
```xml
<!-- This label's text automatically updates when status_text subject changes -->
<lv_label bind_text="status_text" style_text_color="#text_primary"/>
```

## Implementation Pattern

### Step 1: Define XML Layout

Create component in `ui_xml/component_name.xml`:

```xml
<component>
    <view extends="lv_obj" width="100%" height="100%">
        <!-- Status label bound to subject -->
        <lv_label bind_text="status_message" style_text_font="montserrat_20"/>

        <!-- Temperature label bound to subject -->
        <lv_label bind_text="temp_value" style_text_font="montserrat_28"/>
    </view>
</component>
```

### Step 2: Create C++ Wrapper

Create header `include/ui_component_name_xml.h`:

```cpp
#pragma once
#include "lvgl/lvgl.h"

// Create component with reactive data binding
lv_obj_t* ui_component_name_xml_create(lv_obj_t* parent);

// Update component state
void ui_component_name_xml_update(const char* status, int temp);
```

Create implementation `src/ui_component_name_xml.cpp`:

```cpp
#include "ui_component_name_xml.h"
#include <cstdio>
#include <cstring>

static lv_obj_t* component = nullptr;

// Subjects for reactive binding
static lv_subject_t status_subject;
static lv_subject_t temp_subject;
static char status_buffer[128];
static char temp_buffer[32];

lv_obj_t* ui_component_name_xml_create(lv_obj_t* parent) {
    // Initialize subjects BEFORE creating XML
    lv_subject_init_string(&status_subject, status_buffer, NULL,
                           sizeof(status_buffer), "Initial status");
    lv_subject_init_string(&temp_subject, temp_buffer, NULL,
                           sizeof(temp_buffer), "25 °C");

    // Register subjects globally (NULL = global scope)
    lv_xml_register_subject(NULL, "status_message", &status_subject);
    lv_xml_register_subject(NULL, "temp_value", &temp_subject);

    // Create XML component (automatically binds to registered subjects)
    component = (lv_obj_t*)lv_xml_create(parent, "component_name", nullptr);

    return component;
}

void ui_component_name_xml_update(const char* status, int temp) {
    // Update subjects - all bound widgets update automatically
    if (status) {
        lv_subject_copy_string(&status_subject, status);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d °C", temp);
    lv_subject_copy_string(&temp_subject, buf);
}
```

### Step 3: Register and Use

In `main.cpp` or application code:

```cpp
// Register XML component files (after fonts/images)
lv_xml_component_register_from_file("A:/path/to/ui_xml/globals.xml");
lv_xml_component_register_from_file("A:/path/to/ui_xml/component_name.xml");

// Create component (initializes subjects and bindings)
lv_obj_t* panel = ui_component_name_xml_create(screen);

// Update component state (triggers reactive updates)
ui_component_name_xml_update("Printing...", 210);
```

## Working Example: Home Panel

### Files

1. **XML Layout:** `ui_xml/home_panel.xml`
2. **C++ Wrapper:** `src/ui_panel_home_xml.cpp`, `include/ui_panel_home_xml.h`
3. **Test Program:** `test_home_panel.cpp`

### Key Features

**Subjects:**
- `status_text` - Status message (string)
- `temp_text` - Temperature display (string)

**XML Bindings:**
```xml
<lv_label bind_text="status_text" .../>
<lv_label bind_text="temp_text" .../>
```

**Update API:**
```cpp
ui_panel_home_xml_update("Ready", 25);  // Updates both labels instantly
```

### Running Test

```bash
# Build and capture screenshot
./scripts/screenshot.sh test_home_panel home-panel-initial

# Or run manually
./build/bin/test_home_panel
# Press 1-3 to test different states
# Press S to save screenshot
```

## Important Details

### Subject Registration Timing

**⚠️ CRITICAL:** Register subjects BEFORE creating XML components that bind to them.

```cpp
// CORRECT ORDER:
lv_subject_init_string(&subject, ...);          // 1. Initialize
lv_xml_register_subject(NULL, "name", ...);     // 2. Register
lv_obj_t* widget = lv_xml_create(...);          // 3. Create XML
```

### Global vs. Component Scope

**Global scope** (`NULL`):
- Subjects accessible to all XML components
- Use for shared data (status, temperature, etc.)

**Component scope** (non-NULL):
- Subjects only accessible within specific component
- Use for component-internal state

### String Subject Buffer Management

String subjects require persistent buffers:

```cpp
static char status_buffer[128];  // Must be static or heap-allocated
lv_subject_init_string(&subject, status_buffer, NULL, sizeof(status_buffer), "Initial");
```

**Do NOT:**
```cpp
char buffer[128];  // Stack-allocated - WRONG!
lv_subject_init_string(&subject, buffer, ...);
```

## LVGL XML Flex Layout

### Navigation Bar Pattern

For proper flex distribution (like evenly-spaced navbar icons), use style properties instead of attributes:

**CORRECT:**
```xml
<view extends="lv_obj"
      flex_flow="column"
      style_flex_main_place="space_evenly"   ← main axis (vertical for column)
      style_flex_cross_place="center"        ← cross axis (horizontal for column)
      style_flex_track_place="center">       ← track placement
```

**INCORRECT:**
```xml
<view flex_align="space_evenly center center">  ← Won't work in XML!
```

### Centering Child Elements

For centering labels within buttons:

```xml
<lv_button ...>
    <lv_label align="center" text="..."/>  ← Equivalent to lv_obj_center()
</lv_button>
```

## Constants and Theme System

### Global Constants

Defined in `ui_xml/globals.xml`:

```xml
<component>
    <consts>
        <!-- Colors -->
        <color name="bg_dark" value="0x1a1a1a"/>
        <color name="panel_bg" value="0x111410"/>
        <color name="text_primary" value="0xffffff"/>

        <!-- Dimensions -->
        <px name="nav_width" value="102"/>
        <px name="nav_padding" value="16"/>
        <percent name="card_width" value="45%"/>

        <!-- Icon strings (auto-generated) -->
        <str name="icon_home" value=""/>  <!-- UTF-8 char -->
    </consts>
</component>
```

### Using Constants

Reference with `#name` syntax:

```xml
<lv_obj width="#nav_width" style_bg_color="#bg_dark"/>
```

### Auto-Generated Icon Constants

FontAwesome icon constants are generated to avoid UTF-8 editing issues:

```bash
python3 scripts/generate-icon-consts.py
```

Updates the auto-generated section in `globals.xml`.

## Comparison: C++ vs. XML Approach

### Traditional C++ Approach

**Pros:**
- Type safety
- IDE autocomplete
- Compile-time errors

**Cons:**
- Verbose boilerplate
- Layout mixed with logic
- Hard to iterate on design
- Must recompile for UI changes

**Example:**
```cpp
lv_obj_t* btn = lv_button_create(parent);
lv_obj_set_size(btn, 70, 70);
lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
// ... 10 more lines for one button
```

### XML Approach

**Pros:**
- Declarative and concise
- Separation of concerns
- Reactive data binding
- Rapid iteration (no recompile for layout changes)
- Designer-friendly

**Cons:**
- No compile-time type checking for XML
- Runtime errors if subjects not registered
- Requires understanding of subject lifecycle

**Example:**
```xml
<lv_button width="#btn_size" height="#btn_size"
           style_bg_opa="0%" style_border_width="0">
    <lv_label bind_text="button_label" align="center"/>
</lv_button>
```

## Best Practices

1. **One subject per updatable value** - Don't share subjects across unrelated widgets
2. **Initialize before register before create** - Follow strict ordering
3. **Use const references** - Pass string data efficiently to update functions
4. **Descriptive subject names** - `"printer_status"` not `"data1"`
5. **Component-local wrappers** - Each XML component gets its own C++ wrapper
6. **Debug output** - Use printf() to verify subject registration and updates
7. **Screenshot testing** - Use `./scripts/screenshot.sh binary_name output_name`

## Troubleshooting

### "No constant was found with name X"

**Cause:** Constant not defined in globals.xml or not registered before component creation.

**Fix:** Ensure `globals.xml` is registered first:
```cpp
lv_xml_component_register_from_file(".../globals.xml");  // FIRST
lv_xml_component_register_from_file(".../other.xml");    // Then others
```

### Labels not updating when subject changes

**Cause:** Subject not registered before XML creation, or wrong subject name.

**Fix:**
1. Check registration order
2. Verify `bind_text="subject_name"` matches registered name
3. Add debug printf() in update function

### Flex layout not distributing items evenly

**Cause:** Using `flex_align` attribute instead of `style_flex_main_place`.

**Fix:** Use style properties:
```xml
style_flex_main_place="space_evenly"
style_flex_cross_place="center"
```

### Icons not centered in buttons

**Cause:** Missing `align="center"` on label.

**Fix:**
```xml
<lv_label align="center" text="#icon_name"/>
```

## Resources

- **LVGL XML Docs:** https://docs.lvgl.io/master/details/xml/
- **Subject-Observer:** https://docs.lvgl.io/master/details/auxiliary-modules/observer/observer.html
- **Project Examples:**
  - Navigation bar: `ui_xml/navigation_bar.xml`
  - Home panel: `ui_xml/home_panel.xml` + `src/ui_panel_home_xml.cpp`
  - Test program: `test_home_panel.cpp`

## Future Enhancements

- [ ] Integer subjects for numeric displays
- [ ] Color subjects for theme switching
- [ ] Pointer subjects for image sources
- [ ] Observer callbacks for custom logic on changes
- [ ] Component library of reusable widgets
- [ ] Theme variants (light/dark mode)
