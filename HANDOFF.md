# Session Handoff Document

**Last Updated:** 2025-11-02
**Current Focus:** Multiple tracks - Wizard implementation (other session), Print Select real file ops (JUST COMPLETED)

---

## ✅ JUST COMPLETED: Real File Operations Integration (2025-11-02)

**Print Select Panel now fetches real data from Moonraker**

**What Works:**
- `ui_panel_print_select_refresh_files()` fetches real file list via `MoonrakerAPI::list_files()`
- Nested async metadata fetching for each file via `get_file_metadata()`
- Extracts print time (seconds → minutes) and filament weight (grams)
- Cards/list rows update automatically as metadata arrives
- Thumbnail URL construction (`construct_thumbnail_url()`) - logs URLs, download deferred

**Files Modified:**
- `src/ui_panel_print_select.cpp` - Added config.h include, construct_thumbnail_url() helper, enhanced refresh_files() with nested callbacks

**Edge Cases Handled:**
- Bounds checking (file_list changes during async ops)
- Missing metadata → keeps placeholders (0 min, 0g)
- Error callbacks log warnings without crashing
- Empty file lists → shows existing empty state UI

**What's Deferred:**
- Actual HTTP thumbnail downloads (URL construction works, marked TODO)
- Would need: libhv HttpClient integration, temp directory, file cleanup

**Testing:**
- ✅ Builds cleanly (zero warnings)
- ✅ Mock mode (`--test`) still works with test data
- ⏳ Real Moonraker test requires live printer (code ready)

**Key Pattern - Async Metadata Loading:**
```cpp
api->list_files("gcodes", "", false, [api](const std::vector<FileInfo>& files) {
    // Show files immediately with placeholders
    for (const auto& file : files) { /* add to file_list */ }
    populate_card_view();

    // Fetch metadata asynchronously
    for (size_t i = 0; i < file_list.size(); i++) {
        api->get_file_metadata(filename, [i, filename](const FileMetadata& m) {
            // Bounds check, update file_list[i], re-render
        });
    }
});
```

---

## 🧙 Active Work: First-Run Configuration Wizard (IN PROGRESS)

### What's Working ✅

**Wizard Framework Infrastructure**
- Subject-based navigation (current_step, total_steps, wizard_title, wizard_progress)
- Responsive constants system (SMALL ≤480, MEDIUM 481-800, LARGE >800px)
- Back/Next navigation with automatic "Finish" button on last step
- Parent-defined constants pattern (wizard_container → child screens)
- Theme-aware background colors
- Test infrastructure (wizard_validation.h/cpp with IP/hostname validation)

**Step 1: WiFi Setup ✅ FULLY IMPLEMENTED**
- WiFi toggle with immediate placeholder hide/show
- Network scanning with 3-second delay after enable
- Network list population (signal strength icons + encryption indicators)
- Connected network highlighting with accent color
- Click-to-connect with password modal
- Responsive layout for all screen sizes
- Backend integration with WiFiManager (mock + real)

### What Exists But NOT Wired ⚠️

**8 XML Screens Created:**
1. ✅ wizard_wifi_setup.xml (working)
2. ⚠️ wizard_connection.xml (exists, not wired - Moonraker IP/port)
3. ⚠️ wizard_printer_identify.xml (exists, not wired - printer type/name)
4. ⚠️ wizard_bed_select.xml (exists, not wired - bed heater/sensor dropdowns)
5. ⚠️ wizard_hotend_select.xml (exists, not wired - extruder dropdowns)
6. ⚠️ wizard_fan_select.xml (exists, not wired - fan assignments)
7. ⚠️ wizard_led_select.xml (exists, not wired - LED assignments)
8. ⚠️ wizard_summary.xml (exists, not wired - review selections)

**C++ Implementation Gaps** (ui_wizard.cpp:305-338):
- Steps 2-7 show "not yet implemented" placeholders
- total_steps = 7 but 8 XML screens exist (mismatch to resolve)
- Only WiFi screen has content creation function (ui_wizard_wifi_create())

### Next Session (Tomorrow Morning) 🌅

1. **Wire Step 2: Moonraker Connection**
   - Connection test with spinner + status feedback
   - IP address entry (numeric keypad)
   - Save to config on success
   - Trigger auto-discovery

2. **Resolve step count mismatch** (7 vs 8 screens)

3. **Wire Step 3: Printer Identify**
   - Roller widget for printer type (32 options)
   - Text input for printer name
   - Auto-populate from discovery

**Key Files:**
- `src/ui_wizard.cpp` - Navigation (WiFi only, rest TODOs)
- `src/ui_wizard_wifi.cpp` - WiFi screen implementation
- `ui_xml/wizard_*.xml` - 8 screens (1 working, 7 waiting)
- `include/wizard_validation.h` - IP/hostname/port helpers

---

## 📋 Critical Patterns for Wizard Work

### Pattern #1: Flex Layout Height Requirements ⚠️

**flex_grow children require parent with explicit height:**
```xml
<!-- ❌ BROKEN: Parent has no height -->
<lv_obj flex_flow="row">
    <lv_obj flex_grow="3">Left</lv_obj>
    <lv_obj flex_grow="7">Right</lv_obj>
</lv_obj>

<!-- ✅ CORRECT: Parent has height, columns have height="100%" -->
<lv_obj height="100%" flex_flow="row">
    <lv_obj flex_grow="3" height="100%">Left</lv_obj>
    <lv_obj flex_grow="7" height="100%">Right</lv_obj>
</lv_obj>
```
**Fix:** Add `style_bg_color="#ff0000"` to visualize bounds. See `docs/LVGL9_XML_GUIDE.md:634-716`

### Pattern #2: Runtime Constants (Responsive + Theme) ⚠️

**Wizard uses heavily for screen size adaptation:**
```cpp
// Detect size, override constants BEFORE creating XML
int width = lv_display_get_horizontal_resolution(lv_display_get_default());
lv_xml_component_scope_t* scope = lv_xml_component_get_scope("wizard_container");

if (width < 600) {
    lv_xml_register_const(scope, "wizard_padding", "6");
} else {
    lv_xml_register_const(scope, "wizard_padding", "20");
}
```
**See:** `ui_wizard.cpp:90-203` for complete wizard responsive pattern

### Pattern #3: Subject Initialization Order ⚠️

**Always init subjects BEFORE creating XML:**
```cpp
lv_xml_register_component_from_file("A:/ui_xml/my_panel.xml");
ui_my_panel_init_subjects();  // FIRST
lv_xml_create(screen, "my_panel", NULL);  // AFTER
```

### Pattern #4: Test Mode ⚠️

**Wizard will use test mode - never auto-fallback to mocks:**
```cpp
if (config.should_mock_wifi()) {
    return std::make_unique<WifiBackendMock>();
}
// Production: return nullptr if hardware fails, NO fallback
```
**See:** `test_config.h` for complete API

---

## 🔧 Known Issues & Gotchas

### LVGL 9 XML Roller Options

**Issue:** XML parser fails with `options="'item1\nitem2' normal"` syntax

**Workaround:** Set roller options programmatically:
```cpp
lv_roller_set_options(roller, "Item 1\nItem 2\nItem 3", LV_ROLLER_MODE_NORMAL);
```

**Files:** `src/ui_wizard.cpp:352-387`

---

**Rule:** When work is complete, DELETE it from HANDOFF immediately. Keep this document lean and current.
