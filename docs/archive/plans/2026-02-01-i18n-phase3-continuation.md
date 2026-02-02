# i18n Phase 3+ Continuation Plan

**Date:** 2026-02-01
**Status:** Planning

---

## Current State

### Completed
- ✅ Language chooser wizard step (step 1, after touch calibration)
- ✅ Language dropdown in Settings panel
- ✅ Config persistence (`/language` key)
- ✅ SettingsManager language APIs (`get_language()`, `set_language()`)
- ✅ Extended Unicode fonts (Latin Extended-A, Cyrillic) for native language names
- ✅ translations.xml with 93 strings × 5 languages
- ✅ 5 flag icons (en, de, fr, es, ru)

### Stubbed (Blocking Issue)
The plan assumed LVGL 9.4 would have translation APIs:
- `lv_translation_set_language(lang)` - hot-reload all translatable text
- `lv_xml_register_translation_from_file(path)` - load translation catalog

**These don't exist in LVGL.** Currently stubbed in `include/lv_translation_stub.h`.

---

## What Phase 3+ Originally Required

| Phase | Task | Status |
|-------|------|--------|
| 3 | Custom component translation_tag passthrough | Not started |
| 4 | XML migration (~142 files, ~1,200 strings) | Not started |
| 5 | Translation files (de, fr, es complete) | Partial (93 strings exist) |

---

## The Problem

Without LVGL translation support, we can't do hot-reload. Two options:

### Option A: Build Our Own Translation System
Implement what LVGL was supposed to provide:
1. Parse translations.xml on startup into a hashmap (`tag → translated_string`)
2. Hook into XML widget creation to replace `translation_tag` values
3. On language change, walk all widgets and update text

**Pros:** Hot-reload, matches original design
**Cons:** Significant effort, LVGL widget tree traversal is complex

### Option B: Static Translation (Preprocessor)
Generate language-specific XML files at build time:
1. Run a script that reads translations.xml + source XML
2. Output `ui_xml_de/*.xml`, `ui_xml_fr/*.xml`, etc.
3. Load correct directory based on language setting
4. Language change requires app restart

**Pros:** Simpler, no runtime overhead
**Cons:** No hot-reload, file duplication, restart required

### Option C: Subject-Based Translation (Hybrid)
Use string subjects for translatable text:
1. Create subjects for each translation key
2. Bind XML widgets to these subjects
3. On language change, update all translation subjects

**Pros:** Uses existing infrastructure, hot-reload works
**Cons:** ~1,200 subjects, memory overhead, registration complexity

---

## Recommended: Option A (Minimal)

Build a lightweight translation system that:
1. Loads translations.xml into memory at startup
2. Provides `lv_tr(tag)` function to look up translations
3. Uses observer pattern for hot-reload

### Implementation Tasks

| Task | Description | Effort |
|------|-------------|--------|
| 1 | Create `translation_manager.h/cpp` with XML parsing | Medium |
| 2 | Implement `lv_tr(tag)` lookup function | Small |
| 3 | Create translation observer for hot-reload | Medium |
| 4 | Hook XML widget creation to auto-translate | Complex |
| 5 | Update stubs to call real implementation | Small |

### Alternative: Start Simple
For MVP, skip hot-reload:
1. Parse translations.xml at startup
2. Set active language before UI creation
3. Require restart to change language (show toast)

This is much simpler and covers 95% of user needs.

---

## Immediate Next Steps

1. **Decide on approach** - Ask user: hot-reload required or restart OK?
2. **Implement translation_manager** - Parse XML, provide lookup
3. **Wire to Settings panel** - Show restart-required toast
4. **Phase 4: XML migration** - Add translation_tag to all strings
5. **Phase 5: Complete translations** - Fill out all 5 languages

---

## Questions for User

1. Is hot-reload (instant language switch) required, or is restart acceptable?
2. Priority: Should we finish i18n before other features, or pause it?
3. Russian support: Keep as 5th language or defer to 4-language MVP?
