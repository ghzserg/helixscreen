# Print Select Panel - Implementation Plan

**Date Started:** 2025-10-11
**Status:** Phase 1 COMPLETE, Ready for Phase 2
**Requirements:** `docs/requirements/print-select-panel-v1.md`
**Last Updated:** 2025-10-11 23:30

---

## Current Progress Checkpoint

### ✅ Phase 1: COMPLETE (2025-10-11 Afternoon)

**Prerequisites & Prototyping - ALL DONE:**
- [x] Requirements document created (430+ lines)
- [x] All 10 design decisions made and documented
- [x] Color mapping: Using existing palette only
- [x] Generated fa_icons_16 font (clock, leaf icons)
- [x] Updated fa_icons_64 with folder icon
- [x] All 13 icon constants in globals.xml
- [x] Updated Makefile and package.json
- [x] Updated ui_fonts.h declarations
- [x] Researched LVGL XML text truncation (`long_mode="dots"` for ellipsis)
- [x] Added folder icon to navigation (6th button in navigation_bar.xml)
- [x] Updated navigation system to support 6 panels (UI_PANEL_COUNT now 6)
- [x] **PROTOTYPE VALIDATED:** XML component dynamic instantiation works!
  - Created test_card.xml component
  - Created test_dynamic_cards.cpp test harness
  - Successfully instantiated 6 cards from XML template
  - Successfully populated card data via `lv_obj_find_by_name()`
  - Added color constant `card_bg_dark` to globals.xml
  - Confirmed pattern is viable for production

**Key Finding:** Dynamic XML component instantiation with `lv_xml_create()` + name-based widget lookup works perfectly. Ready to build full panel.

### 🚧 Next Session - Resume Here

**Phase 2: Static Panel Structure (30 min)**
- Create print_select_panel.xml with tabs and grid
- Register component in main.cpp
- Wire into navigation system
- See Phase 2 section below for details

---

## Design Decisions Summary

1. ✅ **Navigation:** Folder icon as 6th navigation button
2. ✅ **Tab Styling:** Color change only (no underline/background)
3. ✅ **Thumbnails:** Placeholder gray squares for v1
4. ✅ **Dynamic Cards:** Prototype XML instantiation first (risk mitigation)
5. ✅ **Scope:** Basics only, defer polish to later iteration

---

## Implementation Phases

### Phase 1: Prerequisites & Prototyping (60-75 min)

**1.1 Generate fa_icons_16 Font**
- Add clock-o (`\uf017`) and leaf (`\uf06c`) icons
- Use existing `lv_font_conv` process
- Update package.json with convert-font-16 script
- Update ui_fonts.h with icon constants

**1.2 Update Global Constants**
```xml
<!-- Add to globals.xml -->
<const name="card_bg_dark" type="color" value="#3a3d42"/>
<const name="accent_green" type="color" value="#4a9d5f"/>
```

**1.3 Research Text Truncation**
- Verify LVGL XML attributes for text overflow
- Test ellipsis behavior with long text
- Document findings

**1.4 Add Folder Navigation Icon**
- Choose folder icon from FontAwesome (fa-folder or fa-folder-open)
- Add to existing fa_icons_64 font if not present
- Update navigation_bar.xml with 6th button
- Update ui_nav.cpp arrays for 6 panels
- Update UI_PANEL_COUNT enum

**1.5 PROTOTYPE: Dynamic Card Generation**
**CRITICAL:** Test before implementing full panel

Test approach:
```cpp
// Can we do this?
lv_obj_t* card = lv_xml_create(container, "test_card_component", NULL);

// How do we set data?
// Option A: Find children and set text
lv_obj_t* label = lv_obj_find_by_name(card, "filename_label");
lv_label_set_text(label, "Test.gcode");

// Option B: Use subjects per card (probably not scalable)
// Option C: Different approach entirely?
```

**Goal:** Validate architecture before committing to Phase 5

---

### Phase 2: Static Panel Structure (30 min)

**2.1 Create print_select_panel.xml**
```xml
<component>
    <view flex_flow="column" width="100%" height="100%">
        <!-- Tab bar -->
        <lv_obj height="56px" flex_flow="row">
            <lv_label name="tab_internal" text="Internal"/>
            <lv_label name="tab_sd" text="SD card"/>
        </lv_obj>

        <!-- Scrollable grid container -->
        <lv_obj flex_grow="1" flag_scrollable="true"
                flex_flow="row_wrap" style_pad_gap="20">
            <!-- Cards added here -->
        </lv_obj>
    </view>
</component>
```

**2.2 Register Component**
- Add to main.cpp component registration
- Add to navigation system (panel ID 5)

**2.3 Verify Structure**
- Build and run
- Navigate to print select panel
- Verify tab bar visible
- Verify content area shows

---

### Phase 3: Card Component & Grid (75 min)

**3.1 Define Card as XML Component (or C++ structure)**
Depending on prototype results from Phase 1.5

**Option A: XML Component**
```xml
<!-- card_component.xml -->
<component>
    <lv_obj width="240" style_bg_color="#card_bg_dark"
            style_radius="8" style_pad_all="12"
            flex_flow="column" flex_grow="1">

        <!-- Placeholder thumbnail -->
        <lv_obj width="236" height="236"
                style_bg_color="#3a3d42"
                style_radius="6"/>

        <!-- Filename -->
        <lv_label name="filename" text="placeholder.gcode"
                  style_text_font="montserrat_16"
                  style_text_color="#text_primary"/>

        <!-- Metadata row -->
        <lv_obj flex_flow="row" style_pad_gap="12">
            <!-- Print time -->
            <lv_obj flex_flow="row" style_pad_gap="4">
                <lv_label text="#icon_clock"
                          style_text_font="fa_icons_16"
                          style_text_color="#accent_green"/>
                <lv_label name="print_time" text="19m"
                          style_text_font="montserrat_14"
                          style_text_color="#accent_green"/>
            </lv_obj>

            <!-- Filament weight -->
            <lv_obj flex_flow="row" style_pad_gap="4">
                <lv_label text="#icon_leaf"
                          style_text_font="fa_icons_16"
                          style_text_color="#accent_green"/>
                <lv_label name="filament" text="4g"
                          style_text_font="montserrat_14"
                          style_text_color="#accent_green"/>
            </lv_obj>
        </lv_obj>
    </lv_obj>
</component>
```

**Option B: C++ Programmatic (if XML instantiation doesn't work)**
```cpp
lv_obj_t* create_file_card(lv_obj_t* parent, const char* filename,
                           const char* time, const char* weight) {
    // Programmatically create card structure
    // Apply all styles from globals
}
```

**3.2 Add Multiple Cards**
- Create 4 cards initially (test grid wrapping)
- Add 12 more cards (16 total for scrolling)
- Use different placeholder filenames

**3.3 Test Layout**
- Verify 3-5 column responsive wrapping
- Test vertical scrolling
- Verify spacing (20px gaps)
- Check text truncation on long filenames

---

### Phase 4: Reactive Tab Switching (60 min)

**4.1 Create C++ Wrapper**
```cpp
// ui_panel_print_select.h
typedef enum {
    STORAGE_INTERNAL = 0,
    STORAGE_SD_CARD = 1
} storage_type_t;

void ui_panel_print_select_init_subjects();
void ui_panel_print_select_set_storage(storage_type_t type);
```

```cpp
// ui_panel_print_select.cpp
static lv_subject_t active_storage_subject;

void ui_panel_print_select_init_subjects() {
    lv_subject_init_int(&active_storage_subject, STORAGE_INTERNAL);
    lv_xml_register_subject(NULL, "active_storage", &active_storage_subject);
}
```

**4.2 Add Tab Click Handlers**
```cpp
static void tab_internal_clicked(lv_event_t* e) {
    ui_panel_print_select_set_storage(STORAGE_INTERNAL);
}

static void tab_sd_clicked(lv_event_t* e) {
    ui_panel_print_select_set_storage(STORAGE_SD_CARD);
}
```

**4.3 Reactive Tab Colors**
- Update tab label colors based on active_storage_subject
- Observer pattern: white for active, gray for inactive
- Similar to navigation icon color updates

**4.4 Test Tab Switching**
- Click tabs
- Verify color updates
- Verify state changes

---

### Phase 5: Dynamic Card Generation (90-120 min)

**5.1 Define File Data Structure**
```cpp
struct PrintFile {
    std::string filename;
    int print_time_minutes;
    float filament_grams;
    // Thumbnail path added later when we implement extraction
};

std::vector<PrintFile> mock_files = {
    {"Burr Puzzle_P.gcode", 19, 4.0f},
    {"Scraper_grip.gcode", 80, 30.0f},
    {"Robort.gcode", 121, 12.04f},
    // ... more files
};
```

**5.2 Implement Card Generation**
Based on prototype findings from Phase 1.5

**If XML instantiation works:**
```cpp
for (const auto& file : mock_files) {
    lv_obj_t* card = lv_xml_create(container, "file_card", NULL);

    // Find and populate children
    lv_obj_t* filename_label = lv_obj_find_by_name(card, "filename");
    lv_label_set_text(filename_label, file.filename.c_str());

    // Format and set time
    char time_buf[16];
    format_print_time(time_buf, file.print_time_minutes);
    lv_obj_t* time_label = lv_obj_find_by_name(card, "print_time");
    lv_label_set_text(time_label, time_buf);

    // Format and set weight
    char weight_buf[16];
    snprintf(weight_buf, sizeof(weight_buf), "%.1fg", file.filament_grams);
    lv_obj_t* weight_label = lv_obj_find_by_name(card, "filament");
    lv_label_set_text(weight_label, weight_buf);
}
```

**If programmatic creation needed:**
```cpp
for (const auto& file : mock_files) {
    lv_obj_t* card = create_file_card(container,
                                      file.filename.c_str(),
                                      time_str,
                                      weight_str);
}
```

**5.3 Create Mock Data**
- 16-20 mock files with varied data
- Different filename lengths (test truncation)
- Different time formats (19m, 1h20m, 2h1m)
- Different weights (4g, 30g, 12.04g)

**5.4 Test Dynamic Generation**
- Verify all cards render
- Check scrolling performance
- Verify responsive wrapping still works
- Test with different data sets

---

## Success Criteria

### Phase 1 ✅ COMPLETE
- [x] fa_icons_16 font generated and working
- [x] New colors in globals.xml (added card_bg_dark)
- [x] 6-panel navigation working
- [x] Dynamic card generation approach validated
- [x] Text truncation syntax researched (long_mode="dots")
- [x] Test harness created and prototype successful

### Phase 2
- [x] Panel XML structure created
- [x] Tab bar visible with 2 tabs
- [x] Scroll container ready for cards

### Phase 3
- [x] File card structure defined
- [x] 16 cards visible in grid
- [x] 3-5 column responsive wrapping works
- [x] Vertical scrolling functional
- [x] Text truncation working

### Phase 4
- [x] Tab click handlers registered
- [x] Tab colors update reactively
- [x] Storage state managed via subject

### Phase 5
- [x] File data structure defined
- [x] Cards generated from data array
- [x] All card content populated correctly
- [x] Performance acceptable with 20+ cards

---

## Deferred to Future Iterations

- Clock badge overlay on thumbnails
- Real thumbnail extraction from GCode
- Chevron icons on cards
- Empty state messaging
- Loading indicators
- Card click behavior (print dialog)
- File filtering/search
- SD card detection
- Pull-to-refresh gesture
- Virtual scrolling optimization
- Tab transition animations

---

## Risk Mitigation

**High Risk:** Dynamic XML component instantiation
- **Mitigation:** Prototype in Phase 1.5 before full implementation
- **Fallback:** Programmatic C++ card creation

**Medium Risk:** Text truncation behavior
- **Mitigation:** Research and test in Phase 1.3
- **Fallback:** Fixed-width labels with manual substring

**Medium Risk:** 6-panel navigation changes
- **Mitigation:** Test thoroughly after Phase 1.4
- **Fallback:** Revert to 5 panels, make print select a sub-panel

---

**Next Action:** Begin Phase 1.1 - Generate fa_icons_16 font

