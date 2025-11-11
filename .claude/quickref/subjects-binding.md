# Subject & Data Binding Quick Reference

**Ultra-condensed guide for LVGL reactive data binding. For complete architecture, see `ARCHITECTURE.md`**

---

## ⚠️ CRITICAL INITIALIZATION ORDER

```cpp
// 1. Register XML components
lv_xml_register_component_from_file("A:/ui_xml/globals.xml");
lv_xml_register_component_from_file("A:/ui_xml/home_panel.xml");

// 2. Initialize subjects in C++ (BEFORE creating XML!)
ui_nav_init();                    // Initialize navigation subjects
ui_panel_home_init_subjects();    // Initialize panel-specific subjects

// 3. NOW create XML (subjects are ready)
lv_xml_create(screen, "app_layout", NULL);
```

**❌ WRONG ORDER:** If you create XML before initializing subjects, they'll be empty/have default values.

---

## Subject Types & Usage

| Type | C++ Declaration | C++ Update | XML Binding |
|------|----------------|------------|-------------|
| **Int** | `lv_subject_t my_int;`<br>`lv_subject_init_int(&my_int, 0);` | `lv_subject_set_int(&my_int, 42);` | `bind_text="my_int"` |
| **String** | `lv_subject_t my_str;`<br>`lv_subject_init_string(&my_str, "");` | `lv_subject_copy_string(&my_str, "text");` | `bind_text="my_str"` |
| **Pointer** | `lv_subject_t my_ptr;`<br>`lv_subject_init_pointer(&my_ptr, NULL);` | `lv_subject_set_pointer(&my_ptr, data);` | Custom handler |

---

## Common Binding Patterns

### Text Binding (Simple)
```xml
<!-- XML -->
<lv_label name="status" bind_text="status_text"/>
```
```cpp
// C++ - Initialize
static lv_subject_t status_text;
lv_subject_init_string(&status_text, "Ready");

// C++ - Update (UI updates automatically)
lv_subject_copy_string(&status_text, "Connecting...");
```

### Value Binding (Numbers)
```xml
<!-- XML -->
<lv_label name="temp" bind_text="temp_value"/>
```
```cpp
// C++ - Initialize
static lv_subject_t temp_value;
lv_subject_init_int(&temp_value, 0);

// C++ - Update
lv_subject_set_int(&temp_value, 220);  // Label shows "220"
```

### Flag Binding (Conditional Show/Hide)
```xml
<!-- XML - Hide when value == 0 -->
<lv_obj name="indicator">
  <lv_obj-bind_flag_if_eq subject="is_visible" flag="hidden" ref_value="0"/>
</lv_obj>
```
```cpp
// C++ - Initialize
static lv_subject_t is_visible;
lv_subject_init_int(&is_visible, 1);  // Visible by default

// C++ - Hide
lv_subject_set_int(&is_visible, 0);  // Element becomes hidden

// C++ - Show
lv_subject_set_int(&is_visible, 1);  // Element becomes visible
```

### Button State Binding (Enable/Disable)
```xml
<!-- XML - Button disabled when connection_ok == 0 -->
<lv_button name="next_btn">
  <lv_obj-bind_flag_if_eq subject="connection_ok" flag="clickable" ref_value="0" negate="true"/>
</lv_button>
```
```cpp
// C++ - Initialize
static lv_subject_t connection_ok;
lv_subject_init_int(&connection_ok, 0);  // Disabled by default

// C++ - Enable button
lv_subject_set_int(&connection_ok, 1);  // Button becomes clickable
```

---

## Conditional Binding Operators

| Operator | XML Element | When Flag Applied |
|----------|-------------|-------------------|
| `==` | `bind_flag_if_eq` | `subject == ref_value` |
| `!=` | `bind_flag_if_ne` | `subject != ref_value` |
| `>` | `bind_flag_if_gt` | `subject > ref_value` |
| `>=` | `bind_flag_if_ge` | `subject >= ref_value` |
| `<` | `bind_flag_if_lt` | `subject < ref_value` |
| `<=` | `bind_flag_if_le` | `subject <= ref_value` |

**Add `negate="true"` to invert the condition.**

---

## Widget Lookup by Name

```cpp
// Find widget by name (set in XML)
lv_obj_t* my_label = lv_obj_find_by_name(panel, "status_label");

if (my_label) {
    // Direct manipulation (if not using subjects)
    lv_label_set_text(my_label, "Updated");
}
```

**❌ WRONG:** `lv_obj_get_child(panel, 3)` - Index-based lookup is fragile

**✅ CORRECT:** `lv_obj_find_by_name(panel, "widget_name")` - Name-based lookup

---

## Common Gotchas

| Problem | Cause | Solution |
|---------|-------|----------|
| Subject has default/empty value | Created XML before initializing subjects | Follow initialization order (see top) |
| Can't find widget | Missing `name="..."` in XML | Add explicit names to all components |
| Binding not working | Using attribute syntax | Use child element: `<lv_obj-bind_flag_if_eq>` |
| UI not updating | Using wrong update function | Int: `set_int()`, String: `copy_string()` |

---

## Performance Notes

- **Subjects are lightweight** - Small memory overhead per subject
- **Updates are efficient** - Only bound widgets redraw
- **Thread-safe** - Subjects can be updated from any thread (LVGL processes in main thread)
- **Use subjects for dynamic data** - Static content can just be in XML

---

**Full architecture:** `ARCHITECTURE.md`
**Example implementations:** `src/ui_panel_home.cpp`, `src/ui_panel_wizard.cpp`
**XML guide:** `docs/LVGL9_XML_GUIDE.md` "Data Binding" section
