# Translation System Design

**Date:** 2026-01-28
**Status:** Draft

---

## Setup

### Worktree Creation

Before starting implementation, create an isolated worktree:

```bash
# From main repo
git worktree add ../helixscreen-i18n feature/i18n

# Navigate to worktree
cd ../helixscreen-i18n

# Verify clean state
git status
```

### Branch Naming
- Feature branch: `feature/i18n`
- PR target: `main`

### Environment
```bash
# Build in worktree
make -j

# Run tests
make test-run

# Test UI
./build/bin/helix-screen --test -vv
```

---

## Acceptance Criteria

**These are non-negotiable requirements. The feature is NOT complete until ALL criteria pass.**

### Infrastructure (Phase 1)
- [ ] `LV_USE_TRANSLATION=1` enabled in `lv_conf.h`
- [ ] `Config::get_language()` and `Config::set_language()` implemented
- [ ] `SettingsManager` has `language_subject_` with reactive binding
- [ ] Language persists to `helixconfig.json` and survives restart
- [ ] All Phase 1 tests pass: `./build/bin/helix-tests "[translation]"`

### UI (Phase 2)
- [ ] Wizard has Language step at position 2 (after Connection)
- [ ] Language dropdown in Settings → APPEARANCE section
- [ ] Changing language triggers live UI update (hot-reload works)
- [ ] All Phase 2 tests pass

### Components (Phase 3)
- [ ] `setting_toggle_row.xml` passes `translation_tag` for label and description
- [ ] `setting_dropdown_row.xml` passes `translation_tag` for label and description
- [ ] All other custom components with user text updated
- [ ] All Phase 3 tests pass

### String Migration (Phase 4)
- [ ] Every `text="..."` attribute has corresponding `translation_tag`
- [ ] Every `label="..."` in custom components translatable
- [ ] Every `title="..."` translatable
- [ ] Every `description="..."` translatable
- [ ] String extraction script exists and is runnable
- [ ] Total translatable strings ≥ 1,000 (full coverage)

### Translations (Phase 5)
- [ ] `translations_de.xml` exists with 100% string coverage
- [ ] `translations_fr.xml` exists with 100% string coverage
- [ ] `translations_es.xml` exists with 100% string coverage
- [ ] All translation files parse as valid XML
- [ ] German translation displays correctly when selected
- [ ] French translation displays correctly when selected
- [ ] Spanish translation displays correctly when selected

### Final Verification
- [ ] `make -j` succeeds with no warnings
- [ ] `make test-run` all tests pass
- [ ] Manual test: switch languages in Settings, verify UI updates
- [ ] Manual test: restart app, verify language persists
- [ ] Code review completed for all phases

---

## Overview

Add internationalization (i18n) support to HelixScreen using LVGL 9.4's native translation system. This enables the UI to display in multiple languages while keeping English text readable in XML source files.

**Languages:** English (default), German, French, Spanish
**Approach:** LVGL native translation with XML catalogs, per-language files
**Scope:** Full migration of ~1,200 translatable strings

---

## Architecture

```
Translation Flow:
┌─────────────────────────────────────────────────────────────────┐
│  Startup:                                                       │
│  1. lv_init() → lv_translation_init()                          │
│  2. Load saved language from helixconfig.json                   │
│  3. If not English: load ui_xml/translations/translations_XX.xml│
│  4. lv_translation_set_language("XX")                          │
└─────────────────────────────────────────────────────────────────┘

│  Runtime:                                                       │
│  XML: <text_body text="Settings" translation_tag="Settings"/>  │
│           ↓                                                     │
│  LVGL: lv_tr("Settings") → "Einstellungen" (if German)         │
│           ↓                                                     │
│  UI: Label displays translated text                            │
└─────────────────────────────────────────────────────────────────┘
```

**Key Design Decisions:**
- English text stays directly in XML (`text="Settings"`) for author readability
- `translation_tag` attribute added alongside for localization lookup
- Tag value = English text (e.g., `translation_tag="Settings"`)
- Translation files only contain non-English mappings
- Hot-reload supported: changing language updates UI without restart

---

## File Structure

```
ui_xml/
├── translations/
│   ├── translations_de.xml   # German
│   ├── translations_fr.xml   # French
│   └── translations_es.xml   # Spanish
├── wizard_language.xml       # NEW: Language selection wizard step
└── [existing XML files with translation_tag attributes added]
```

**Translation file format:**
```xml
<!-- translations_de.xml -->
<translations languages="de">
  <translation tag="Settings" de="Einstellungen"/>
  <translation tag="Load" de="Laden"/>
  <translation tag="Animations" de="Animationen"/>
  <!-- ~1,200 entries -->
</translations>
```

---

## Components to Modify

### 1. LVGL Configuration
**File:** `lv_conf.h`
- Enable `LV_USE_TRANSLATION 1`

### 2. SettingsManager
**File:** `include/settings_manager.h`, `src/settings_manager.cpp`
- Add `language_` member and subject
- Add `get_language()`, `set_language(const std::string&)`
- Add `subject_language()` for UI binding
- Add `get_language_options()` for dropdown
- On `set_language()`: call `lv_translation_set_language()` for hot-reload

### 3. Config Persistence
**File:** `include/config.h`, `src/system/config.cpp`
- Add `language` field to JSON schema (default: "en")

### 4. Wizard Language Step
**New files:**
- `ui_xml/wizard_language.xml` - Language selection UI
- `include/ui_wizard_language.h` - Header
- `src/ui/ui_wizard_language.cpp` - Implementation

**Integration:**
- Add to wizard flow as Step 2 (after Connection)
- Update `ui_wizard.cpp` step count and navigation

### 5. Settings Panel
**File:** `ui_xml/settings_panel.xml`
- Add Language dropdown in APPEARANCE section
- Bind to `settings_language` subject

### 6. Custom Component Updates
**Files:** `ui_xml/setting_toggle_row.xml`, `ui_xml/setting_dropdown_row.xml`
- Add `translation_tag="$label"` to label elements
- Add `translation_tag="$description"` to description elements

### 7. Translation Loading
**File:** `src/application/application.cpp` or new `src/translation_manager.cpp`
- Load translation file for selected language at startup
- Skip loading if language is English (use tags directly)

### 8. XML Migration (ALL panels)
Add `translation_tag` attribute to every translatable string:

**Attributes to migrate:**
- `text="..."` → add `translation_tag="..."`
- `label="..."` → add `label_translation_tag="..."` (for custom components)
- `title="..."` → add `title_translation_tag="..."`
- `description="..."` → add `description_translation_tag="..."`
- `options="..."` → each option needs translation

**Files to migrate (~142 XML files):**
- All `ui_xml/*.xml` panel and component files
- Focus on user-facing strings only (skip bind_text subjects)

---

## Critical Files

| Category | Files |
|----------|-------|
| Config | `lv_conf.h`, `include/config.h`, `src/system/config.cpp` |
| Settings | `include/settings_manager.h`, `src/settings_manager.cpp` |
| Wizard | `ui_xml/wizard_language.xml`, `src/ui/ui_wizard_language.cpp`, `src/ui/ui_wizard.cpp` |
| Components | `ui_xml/setting_toggle_row.xml`, `ui_xml/setting_dropdown_row.xml` |
| Settings UI | `ui_xml/settings_panel.xml` |
| Translation Files | `ui_xml/translations/translations_{de,fr,es}.xml` |
| Application | `src/application/application.cpp` |

---

## Implementation Workflow

**Method:** TDD + Sub-agent delegation + Code reviews after each phase

Each phase follows:
1. Write failing tests first (TDD)
2. Delegate implementation to sub-agents
3. Code review before proceeding

---

### Phase 1: Infrastructure (Config + SettingsManager)

**Tests first (`tests/unit/test_translation_settings.cpp`):**
```cpp
// Config persistence
TEST_CASE("Config stores language setting", "[config][translation]") {
    Config config;
    config.set_language("de");
    REQUIRE(config.get_language() == "de");
    // Verify persists to JSON
}

// SettingsManager integration
TEST_CASE("SettingsManager language methods", "[settings][translation]") {
    auto& settings = SettingsManager::instance();
    settings.set_language("fr");
    REQUIRE(settings.get_language() == "fr");
    // Verify subject updated
}

TEST_CASE("SettingsManager language options", "[settings][translation]") {
    auto& settings = SettingsManager::instance();
    auto options = settings.get_language_options();
    REQUIRE(options.find("English") != std::string::npos);
    REQUIRE(options.find("Deutsch") != std::string::npos);
}
```

**Implementation (delegate to sub-agent):**
1. Enable `LV_USE_TRANSLATION` in `lv_conf.h`
2. Add language to Config schema
3. Extend SettingsManager with language methods
4. Create `ui_xml/translations/` directory structure
5. Add translation loading at startup

**Review checkpoint:** Code review after Phase 1 complete

---

### Phase 2: UI Integration (Wizard + Settings Panel)

**Tests first (`tests/unit/test_wizard_language.cpp`):**
```cpp
TEST_CASE("Wizard language step exists", "[wizard][translation]") {
    // Verify wizard has language step at position 2
}

TEST_CASE("Language wizard saves selection", "[wizard][translation]") {
    // Simulate user selecting German
    // Verify config updated
}
```

**Tests first (`tests/unit/test_settings_language.cpp`):**
```cpp
TEST_CASE("Settings panel shows language dropdown", "[settings][translation]") {
    // Verify dropdown exists in APPEARANCE section
}

TEST_CASE("Language change triggers hot-reload", "[settings][translation]") {
    // Change language via settings
    // Verify lv_translation_set_language called
}
```

**Implementation (delegate to sub-agent):**
1. Create `wizard_language.xml` and `ui_wizard_language.cpp`
2. Integrate into wizard flow as Step 2
3. Add Language dropdown to Settings panel
4. Wire up hot-reload on language change

**Review checkpoint:** Code review after Phase 2 complete

---

### Phase 3: Custom Components

**Tests first:**
```cpp
TEST_CASE("setting_toggle_row passes translation_tag", "[components][translation]") {
    // Verify label and description get translation_tag attributes
}

TEST_CASE("setting_dropdown_row passes translation_tag", "[components][translation]") {
    // Verify label and description get translation_tag attributes
}
```

**Implementation (delegate to sub-agent):**
1. Update `setting_toggle_row.xml` to add `translation_tag="$label"`, `translation_tag="$description"`
2. Update `setting_dropdown_row.xml` similarly
3. Update any other custom components with translatable text

**Review checkpoint:** Code review after Phase 3 complete

---

### Phase 4: String Migration

**Tests first:**
```cpp
TEST_CASE("All XML files have translation_tag for user-facing strings", "[xml][translation]") {
    // Script/test that scans XML files
    // Verifies every text/label/title/description has corresponding translation_tag
}
```

**Implementation (delegate to sub-agents - can parallelize):**
1. Create extraction script to identify all translatable strings
2. Add `translation_tag` attributes to all 142 XML files
3. Generate master string catalog

**Review checkpoint:** Code review after Phase 4 complete

---

### Phase 5: Translation Content

**Tests first:**
```cpp
TEST_CASE("German translations file is valid XML", "[translation]") {
    // Parse translations_de.xml, verify structure
}

TEST_CASE("All tags have German translations", "[translation]") {
    // Compare master string list vs translations_de.xml coverage
}
```

**Implementation (delegate to sub-agents - parallelize by language):**
1. Create `translations_de.xml` with all German translations
2. Create `translations_fr.xml` with all French translations
3. Create `translations_es.xml` with all Spanish translations

**Review checkpoint:** Final code review of complete translation system

---

## Sub-agent Delegation Strategy

| Phase | Agent Type | Task |
|-------|-----------|------|
| 1 | `general-purpose` | Config + SettingsManager implementation |
| 2a | `general-purpose` | Wizard language step |
| 2b | `general-purpose` | Settings panel language dropdown |
| 3 | `general-purpose` | Custom component updates |
| 4a | `general-purpose` | Extraction script |
| 4b-4f | `general-purpose` (parallel) | XML migration by panel group |
| 5a | `general-purpose` | German translations |
| 5b | `general-purpose` | French translations |
| 5c | `general-purpose` | Spanish translations |

---

## Verification Plan

### Build Verification
```bash
make -j  # Must compile with LV_USE_TRANSLATION=1
```

### Functional Testing
```bash
# Start in test mode with default language (English)
./build/bin/helix-screen --test -vv

# Verify:
# 1. Wizard shows Language step (step 2)
# 2. Language dropdown works in wizard
# 3. Settings panel shows Language dropdown
# 4. Changing language hot-reloads UI text
# 5. Language persists across restart
```

### Language Switching Test
1. Start app, go to Settings → APPEARANCE → Language
2. Change from English to German
3. Verify UI text updates immediately (hot-reload)
4. Restart app, verify German persists
5. Change back to English, verify text reverts

### String Coverage Verification
```bash
# Count translation tags in XML
grep -r "translation_tag" ui_xml/*.xml | wc -l
# Should be ~1,200+ entries
```

---

## Notes

- **Format strings stay untranslated:** "45%", "2h 15m", "23.5 mm" remain technical/universal
- **Subject-bound values stay dynamic:** Temperature, filenames, percentages don't need translation
- **LVGL fallback:** If tag not found, returns the tag itself (so English always works)
- **Translation event:** `LV_EVENT_TRANSLATION_LANGUAGE_CHANGED` broadcasts to all widgets on language change

---

## Review Checkpoints

| After Phase | Review Focus |
|-------------|--------------|
| Phase 1 | Config schema, SettingsManager API, LVGL integration |
| Phase 2 | Wizard flow, Settings UI, hot-reload behavior |
| Phase 3 | Component translation_tag passthrough |
| Phase 4 | XML migration completeness, no regressions |
| Phase 5 | Translation accuracy, XML validity |

Use `superpowers:code-reviewer` agent after each phase completion.
