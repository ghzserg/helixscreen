# LVGL 9 XML UI Quick Reference

Quick patterns and cheat sheets for daily development. For comprehensive documentation, see [LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md).

## Class-Based Patterns (PREFERRED)

### Panel Class Pattern

```cpp
// include/ui_panel_example.h
class ExamplePanel : public PanelBase {
public:
    explicit ExamplePanel(lv_obj_t* parent);
    ~ExamplePanel() override;

    void show() override;
    void hide() override;
    lv_obj_t* get_root() const override { return root_; }

    void update(const std::string& text, int value);

private:
    void init_subjects();
    void setup_events();

    lv_obj_t* root_ = nullptr;
    lv_subject_t text_subject_{};
    lv_subject_t value_subject_{};
    char text_buf_[128]{};
    char value_buf_[32]{};
};

// src/ui_panel_example.cpp
ExamplePanel::ExamplePanel(lv_obj_t* parent) {
    init_subjects();
    root_ = lv_xml_create(parent, "example_panel", nullptr);
    setup_events();
}

void ExamplePanel::init_subjects() {
    lv_subject_init_string(&text_subject_, text_buf_, nullptr, sizeof(text_buf_), "Initial");
    lv_subject_init_string(&value_subject_, value_buf_, nullptr, sizeof(value_buf_), "0");
    lv_xml_register_subject(nullptr, "text_data", &text_subject_);
    lv_xml_register_subject(nullptr, "value_data", &value_subject_);
}

void ExamplePanel::show() { lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN); }
void ExamplePanel::hide() { lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN); }
```

### Manager Class Pattern (Backend)

```cpp
// Singleton manager with pluggable backend
class ExampleManager {
public:
    static ExampleManager& instance();

    bool start();
    void stop();

    void do_async_work(std::function<void(Result)> callback);

private:
    ExampleManager() = default;
    std::unique_ptr<ExampleBackend> backend_;
};
```

### Modal Class Pattern

```cpp
class ConfirmDialog {
public:
    void show(const std::string& title, std::function<void()> on_confirm);
    void dismiss();

private:
    lv_obj_t* backdrop_ = nullptr;
    lv_obj_t* dialog_ = nullptr;
};
```

---

## Legacy Patterns (avoid for new code)

### Function-Based Panel (❌ OLD)

```cpp
// ❌ AVOID - Use class-based pattern above
lv_obj_t* ui_panel_xyz_create(lv_obj_t* parent);
void ui_panel_xyz_update(const char* text, int value);
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

### Component Instantiation with Names (CRITICAL)

**Always add explicit `name` attributes to component tags for findability:**

```xml
<!-- app_layout.xml -->
<lv_obj name="content_area">
  <!-- ✓ CORRECT - Explicit names on component tags -->
  <controls_panel name="controls_panel"/>
  <home_panel name="home_panel"/>
  <settings_panel name="settings_panel"/>
</lv_obj>
```

```cpp
// Now findable from C++
lv_obj_t* controls = lv_obj_find_by_name(parent, "controls_panel");
lv_obj_clear_flag(controls, LV_OBJ_FLAG_HIDDEN);
```

**Why:** Component names in `<view name="...">` definitions don't propagate to instantiation tags. Without explicit names, `lv_obj_find_by_name()` returns NULL.

## Custom Components

### Icon Component

**Custom Material Design icon widget with semantic properties for sizing and color variants.**

**Properties:**
- `src` - Material icon name (default: `"mat_home_img"`)
- `size` - Semantic size string: `xs`, `sm`, `md`, `lg`, `xl` (default: `"xl"`)
- `variant` - Color variant string: `primary`, `secondary`, `accent`, `disabled`, `none` (default: no recoloring)
- `color` - Custom color override (hex format: `"0xFF0000"` or `"#FF0000"`) - overrides `variant`

```xml
<!-- Basic usage with defaults (mat_home_img, 64px, no recolor) -->
<icon/>

<!-- Specify icon source -->
<icon src="mat_print_img"/>

<!-- Semantic sizes with color variants -->
<icon src="mat_heater_img" size="lg" variant="primary"/>
<icon src="mat_back_img" size="md" variant="secondary"/>
<icon src="mat_pause_img" size="sm" variant="disabled"/>

<!-- Custom color overrides variant -->
<icon src="mat_emergency_img" size="lg" color="0xFFFF00"/>
<icon src="mat_prohibited_img" size="md" color="#FF0000"/>
```

**Available Sizes:**
- `xs` - 16×16px @ scale 64 (UI elements)
- `sm` - 24×24px @ scale 96 (small buttons)
- `md` - 32×32px @ scale 128 (standard buttons)
- `lg` - 48×48px @ scale 192 (large UI elements)
- `xl` - 64×64px @ scale 256 (default, primary icons)

**Color Variants:**
- `primary` - Recolored with `#text_primary` (100% opacity)
- `secondary` - Recolored with `#text_secondary` (100% opacity)
- `accent` - Recolored with `#primary_color` (100% opacity)
- `disabled` - Recolored with `#text_secondary` (50% opacity)
- `none` - No recoloring (0% opacity, shows original icon colors)

**C++ Runtime API:**
```cpp
// Change icon source dynamically
ui_icon_set_source(icon_widget, "mat_wifi_strength_4_img");

// Change icon size at runtime
ui_icon_set_size(icon_widget, "lg");  // xs/sm/md/lg/xl

// Change color variant at runtime
ui_icon_set_variant(icon_widget, "accent");  // primary/secondary/accent/disabled/none

// Set custom color with opacity
ui_icon_set_color(icon_widget, lv_color_hex(0xFF0000), LV_OPA_COVER);
```

**Material Icon Names:**
All icons use `mat_*_img` naming convention: `mat_home_img`, `mat_print_img`, `mat_pause_img`, `mat_heater_img`, `mat_bed_img`, `mat_fan_img`, `mat_extruder_img`, `mat_cancel_img`, `mat_refresh_img`, `mat_back_img`, `mat_delete_img`, etc. See [material_icons.cpp](../src/material_icons.cpp) for complete list.

**Adding New Icons:**
1. Find icon SVG at [Pictogrammers Material Design Icons](https://pictogrammers.com/library/mdi/) (7200+ icons)
2. Copy the SVG path and create `assets/images/material/<name>.svg`
3. Convert to LVGL C array:
   ```bash
   inkscape assets/images/material/<name>.svg --export-type=png --export-filename=/tmp/<name>.png -w 64 -h 64
   .venv/bin/python scripts/LVGLImage.py --ofmt C --cf RGB565A8 --compress NONE -o assets/images/material /tmp/<name>.png
   ```
4. Add `LV_IMG_DECLARE(<name>);` to `include/material_icons.h`
5. Register in `src/material_icons.cpp`: `lv_xml_register_image(NULL, "mat_<name>_img", &<name>);`

### ui_switch Component

**Custom switch widget extending lv_switch with semantic size presets for responsive UI design.**

**Properties:**
- `size` - Semantic size string: `tiny`, `small`, `medium`, `large` (default: LVGL defaults)
- `width` - Explicit width override (optional, overrides size preset)
- `height` - Explicit height override (optional, overrides size preset)
- `knob_pad` - Knob padding in pixels (optional, overrides size preset)
- `checked` - Initial state: `true` or `false` (default: `false`)

```xml
<!-- Basic usage with semantic size (recommended) -->
<ui_switch size="medium"/>

<!-- Different sizes -->
<ui_switch size="tiny"/>
<ui_switch size="small" checked="true"/>
<ui_switch size="medium"/>
<ui_switch size="large"/>

<!-- Progressive enhancement: size preset + selective override -->
<ui_switch size="medium" width="100"/>  <!-- Custom width, keeps medium height and knob_pad -->

<!-- Combine with standard LVGL properties -->
<ui_switch size="medium" style_bg_color="#ff0000" style_margin_right="10"/>
```

**Available Sizes (screen-size-aware):**

| Size | TINY (480×320) | SMALL (800×480) | LARGE (1280×720+) |
|------|----------------|-----------------|-------------------|
| `tiny` | 32×16px, pad=1 | 48×24px, pad=2 | 64×32px, pad=2 |
| `small` | 40×20px, pad=1 | 64×32px, pad=2 | 88×44px, pad=3 |
| `medium` | 48×24px, pad=2 | 80×40px, pad=3 | 112×56px, pad=4 |
| `large` | 56×28px, pad=2 | 88×44px, pad=3 | 128×64px, pad=4 |

### Step Progress Widget

**Reusable step-by-step progress indicator for wizards and multi-step operations.**

**Features:**
- Vertical and horizontal orientations
- Three visual states: PENDING (gray filled), ACTIVE (red filled), COMPLETED (green filled)
- Step numbers (1, 2, 3...) automatically toggle to checkmarks on completion
- Seamless 1px connector lines between steps, colored green from completed steps
- Dynamic updates via `ui_step_progress_set_current()`

**API Usage:**
```cpp
#include "ui_step_progress.h"

// Define steps
ui_step_t steps[] = {
    {"Nozzle heating", UI_STEP_STATE_COMPLETED},
    {"Prepare to retract", UI_STEP_STATE_ACTIVE},
    {"Retracting", UI_STEP_STATE_PENDING},
    {"Retract done", UI_STEP_STATE_PENDING}
};

// Create widget
lv_obj_t* progress = ui_step_progress_create(parent, steps, 4, false);  // false = vertical

// Update current step (advances to next)
ui_step_progress_set_current(progress, 2);  // Now on step 3
```

**Files:**
- `include/ui_step_progress.h` - Public API
- `src/ui_step_progress.cpp` - Implementation
- `ui_xml/step_progress_test.xml` - Test panel

## Layout Patterns

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

### Responsive Theme System

The theme system provides **responsive design tokens** that automatically adapt to screen size. Use these tokens instead of hardcoded pixel values.

**Screen Breakpoints** (determined by `max(width, height)` at startup):
- **SMALL** (≤480px): Tiny screens like 480×320
- **MEDIUM** (481-800px): Standard screens like 800×480
- **LARGE** (>800px): Large screens like 1024×600+

#### Responsive Spacing Tokens

Use `#space_*` tokens for all padding, margins, and gaps:

| Token | SMALL | MEDIUM | LARGE | Use Case |
|-------|-------|--------|-------|----------|
| `#space_xxs` | 2px | 3px | 4px | Keypad rows, compact icon gaps |
| `#space_xs` | 4px | 5px | 6px | Button icon+text, dense info |
| `#space_sm` | 6px | 7px | 8px | Tight layouts, minor separations |
| `#space_md` | 8px | 10px | 12px | Standard flex gaps, compact padding |
| `#space_lg` | 12px | 16px | 20px | Container padding, major sections |
| `#space_xl` | 16px | 20px | 24px | Emphasis cards, major separations |

```xml
<!-- ✅ CORRECT - Responsive spacing -->
<lv_obj style_pad_all="#space_lg" style_pad_gap="#space_md"/>
<ui_card style_pad_all="#space_lg"/>

<!-- ❌ WRONG - Hardcoded pixels -->
<lv_obj style_pad_all="16" style_pad_gap="10"/>
```

**C++ access:** `ui_theme_get_spacing("space_lg")` returns the pixel value.

#### Responsive Typography

Use semantic text components or `#font_*` tokens:

| Token | SMALL | MEDIUM | LARGE | Component |
|-------|-------|--------|-------|-----------|
| `#font_heading` | montserrat_20 | montserrat_26 | montserrat_28 | `<text_heading>` |
| `#font_body` | montserrat_14 | montserrat_18 | montserrat_20 | `<text_body>` |
| `#font_small` | montserrat_12 | montserrat_16 | montserrat_18 | `<text_small>` |

```xml
<!-- ✅ PREFERRED - Semantic text components -->
<text_heading>Section Title</text_heading>
<text_body>Body text content</text_body>
<text_small style_text_color="#text_secondary">Helper text</text_small>

<!-- ✅ ALSO OK - Font tokens for custom styling -->
<lv_label style_text_font="#font_body" style_text_color="#text_primary"/>
```

#### Theme-Aware Colors (Light/Dark Mode)

Colors with `_light` and `_dark` variants are auto-registered as base names:

```xml
<!-- globals.xml defines pairs -->
<color name="card_bg_light" value="#F0F3F9"/>
<color name="card_bg_dark" value="#1F1F1F"/>

<!-- XML uses base name - theme system selects appropriate variant -->
<lv_obj style_bg_color="#card_bg"/>
<lv_label style_text_color="#text_primary"/>
```

**C++ access:** `ui_theme_get_color("card_bg")` returns the theme-appropriate color.

#### How It Works

1. **globals.xml** defines variants with suffixes (`space_lg_small`, `card_bg_light`, etc.)
2. **ui_theme.cpp** reads variants at startup and registers base tokens with screen-appropriate values
3. XML uses base tokens (`#space_lg`, `#card_bg`) which resolve to the correct values

#### Adding New Tokens

All three token types now use **pattern matching** - fully extensible from globals.xml without C++ changes!

| Token Type | How to Add |
|------------|------------|
| **Colors** | Add `mycolor_light` and `mycolor_dark` to globals.xml |
| **Spacing** | Add `myspace_small`, `myspace_medium`, and `myspace_large` to globals.xml (as `<px>` elements) |
| **Fonts** | Add `myfont_small`, `myfont_medium`, and `myfont_large` to globals.xml (as `<string>` elements) |

**Example - Adding a new spacing token:**
```xml
<!-- In globals.xml - add all three variants -->
<px name="space_xxl_small" value="20"/>
<px name="space_xxl_medium" value="28"/>
<px name="space_xxl_large" value="32"/>

<!-- Now usable in XML as #space_xxl -->
<lv_obj style_pad_all="#space_xxl"/>
```

**Example - Adding a new responsive font:**
```xml
<!-- In globals.xml - add all three variants -->
<string name="font_display_small" value="montserrat_28"/>
<string name="font_display_medium" value="montserrat_36"/>
<string name="font_display_large" value="montserrat_48"/>

<!-- Now usable in XML as #font_display -->
<lv_label style_text_font="#font_display"/>
```

**⚠️ CRITICAL:** Base tokens (`space_lg`, `font_body`, `card_bg`) are registered at runtime by C++. Do NOT define base names in globals.xml or responsive overrides will be silently ignored (LVGL ignores duplicate `lv_xml_register_const()` calls).

## Subject Types & Bindings

### Subject Initialization

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

// Observer callback for image recoloring
static void image_color_observer(lv_observer_t* obs, lv_subject_t* subj) {
    lv_obj_t* image = (lv_obj_t*)lv_observer_get_target(obs);
    lv_color_t color = lv_subject_get_color(subj);
    lv_obj_set_style_img_recolor(image, color, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(image, 255, LV_PART_MAIN);
}

// Register observer
lv_subject_add_observer_obj(&color_subj, image_color_observer, image_widget, NULL);
```

### XML Binding Reference

| Widget | Binding | Subject Type |
|--------|---------|--------------|
| `lv_label` | `bind_text="name"` | String |
| `lv_slider` | `bind_value="name"` | Integer |
| `lv_arc` | `bind_value="name"` | Integer |
| `lv_dropdown` | `bind_value="name"` | Integer |

## Registration Order (CRITICAL)

```cpp
// ALWAYS follow this order:
lv_xml_register_font(...);                    // 1. Fonts
lv_xml_register_image(...);                   // 2. Images
lv_xml_component_register_from_file(...);     // 3. Components (globals first!)
lv_subject_init_string(...);                  // 4. Init subjects
lv_xml_register_subject(...);                 // 5. Register subjects
lv_xml_create(...);                           // 6. Create UI
```

## Style Properties Cheat Sheet

⚠️ **IMPORTANT:** In `<styles>` definitions, use bare names (`bg_color`). On widgets, use `style_` prefix (`style_bg_color`).

```xml
<!-- In <styles> section - NO style_ prefix! -->
<styles>
    <style name="my_style" bg_color="#ff0000" border_width="4"/>
</styles>

<!-- On widgets - USE style_ prefix -->
<view style_bg_color="#card_bg" style_border_width="0">
    <style name="my_style" selector="checked"/>
</view>
```

```xml
<!-- Layout -->
width="100" height="200" x="50" y="100"
flex_flow="row|column|row_wrap|column_wrap"
flex_grow="1"
align="center|top_left|bottom_right|left_mid|right_mid|..."

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

## Icon & Image Assets

### Material Design Icons (Navigation)

**Automated Workflow using `material_icons.py`:**

```bash
# List all registered Material Design icons
make material-icons-list

# Add new icons from Material Design Icons (download + convert + register)
make material-icons-add ICONS="wifi-strength-1 wifi-strength-2 lock"

# Convert existing SVG files to LVGL C arrays
make material-icons-convert SVGS="icon1.svg icon2.svg"
```

**What the automated workflow does:**
1. Downloads SVG from `google/material-design-icons` GitHub repo
2. Converts SVG → PNG (64x64, alpha preserved)
3. Converts PNG → LVGL C array (RGB565A8 format)
4. Adds declarations to `include/material_icons.h`
5. Registers in `src/material_icons.cpp`

**Manual Conversion (if needed):**

```bash
# 1. SVG to PNG with ImageMagick
# CRITICAL: Use -density 300 for sharp rendering when upscaling
magick -density 300 -background none icon.svg \
  -resize 64x64 -colorspace gray \
  -channel RGB -evaluate set 0 +channel \
  icon.png

# 2. PNG to LVGL 9 C array with LVGLImage.py
.venv/bin/python3 scripts/LVGLImage.py icon.png \
  --ofmt C --cf RGB565A8 -o assets/images/material --name icon
```

**Icon Format Requirements:**
- **RGB565A8 format** - 16-bit RGB + 8-bit alpha, works with `lv_obj_set_style_img_recolor()`
- **64x64 resolution** - Standard size for all Material Design icons
- **High-density rendering** - Use `-density 300` to avoid fuzzy upscaling from 24x24 SVGs
- **Black base color** - Icons must be black (graya(0,*)) for recoloring to work
- **Alpha channel preserved** - Smooth anti-aliased edges required for quality rendering

**Registration Pattern:**
```cpp
// Header (material_icons.h)
LV_IMG_DECLARE(home);
LV_IMG_DECLARE(print);

// Registration (material_icons.cpp)
void material_icons_register() {
    lv_xml_register_image(NULL, "mat_home", &home);
    lv_xml_register_image(NULL, "mat_print", &print);
}

// Usage in XML
<lv_image src="mat_home" align="center"/>
```

**Scaling & Recoloring in XML:**
```xml
<!-- XML scaling (256 = 100%) - PREFERRED METHOD -->
<lv_image src="mat_home"
          scale_x="128" scale_y="128"              <!-- 50% size (32px) -->
          style_image_recolor="#primary_color"     <!-- MUST use 'image' not 'img' -->
          style_image_recolor_opa="255"/>

<lv_image src="mat_home"
          scale_x="256" scale_y="256"              <!-- 100% size (64px) -->
          style_image_recolor="#text_secondary"
          style_image_recolor_opa="255"/>
```

**⚠️ CRITICAL Gotchas:**
- ❌ **zoom** - Doesn't exist in LVGL 9!
- ✅ **scale_x, scale_y** - Use these (256 = 100%)
- ❌ **style_img_recolor** - Parser ignores abbreviated 'img'!
- ✅ **style_image_recolor** - Must use full word 'image'

**C++ Scaling & Recoloring:**
```cpp
// Responsive scaling
lv_image_set_scale_x(icon, 128);  // 50% (32px)
lv_image_set_scale_y(icon, 128);
lv_image_set_scale_x(icon, 256);  // 100% (64px)
lv_image_set_scale_y(icon, 256);

// Dynamic recoloring (active = red, inactive = gray)
lv_obj_set_style_img_recolor(icon, UI_COLOR_PRIMARY, LV_PART_MAIN);
lv_obj_set_style_img_recolor_opa(icon, 255, LV_PART_MAIN);
```

### FontAwesome Icons (UI Content)

Generate icon constants for use in `globals.xml`:

```bash
# Regenerate icon constants after adding new icons
python3 scripts/generate-icon-consts.py

# This updates ui_xml/globals.xml with UTF-8 byte sequences
```

**Adding New FontAwesome Icons:**
1. Edit `package.json` to add Unicode codepoint to font range
2. Run `npm run convert-font-XX` (XX = 16/32/48/64)
3. Run `python3 scripts/generate-icon-consts.py`
4. Rebuild: `make`

**Usage in XML:**
```xml
<!-- globals.xml defines constants -->
<str name="icon_temperature" value=""/>  <!-- UTF-8 bytes -->

<!-- Use in components -->
<lv_label text="#icon_temperature" style_text_font="fa_icons_48"/>
```

**When to Use Which:**
- **Material Design Images**: Navigation, primary actions, needs recoloring
- **FontAwesome Fonts**: UI content, inline icons, text-based rendering

## Common Gotchas

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

---

❌ **Don't:** Use abbreviated 'img' in style properties
```xml
<lv_image style_img_recolor="#ff0000"/>  <!-- Parser ignores this -->
```

✅ **Do:** Use full word 'image'
```xml
<lv_image style_image_recolor="#ff0000"/>  <!-- This works -->
```

---

## See Also

- **[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md)** - Complete XML syntax and patterns
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - System design and reactive patterns
- **[DEVELOPMENT.md](DEVELOPMENT.md)** - Build system and daily workflow
- **[DOXYGEN_GUIDE.md](DOXYGEN_GUIDE.md)** - API documentation standards
