# Wizard Hardware Discovery - Test Plan

**Last Updated:** 2025-11-02
**Status:** Ready for Phase 3 (Real Printer Testing)
**Target Printer:** FlashForge Adventurer 5M Pro @ 192.168.1.67:7125

---

## Overview

This document provides a comprehensive testing plan for the wizard hardware discovery feature, which automatically detects and populates printer hardware (heaters, sensors, fans, LEDs) from Moonraker after successful connection.

## Completed Work

### Phase 1: Hardware Discovery Trigger ✅
- Wizard triggers `discover_printer()` after successful connection (step 2)
- Connection stays alive throughout wizard steps 4-7
- **Location:** `src/ui_wizard_connection.cpp:216-230`

### Phase 2: Dynamic Dropdown Population ✅
- All 4 wizard hardware screens (steps 4-7) use dynamic dropdowns from MoonrakerClient
- Hardware filtering implemented:
  - **Bed:** Filter by "bed" substring → finds "heater_bed", "heater_generic bed"
  - **Hotend:** Filter by "extruder"/"hotend" → finds "extruder", "extruder1", etc.
  - **Fans:** Separated by type (hotend fans vs part cooling)
  - **LEDs:** Show all discovered LEDs
- Fixed critical layout bug: LV_SIZE_CONTENT → flex_grow for nested flex children
- Updated config template with all wizard fields
- **Locations:** `src/ui_wizard_{bed,hotend,fan,led}_select.cpp`

### Phase 2.1: Keyboard Integration ✅
- Fixed keyboard registration in wizard printer identification screen
- Implemented universal screen-slide solution (works everywhere)
- Added smooth 200ms animations (ease-out on show, ease-in on hide)
- Both virtual and physical keyboard input work correctly
- **Commit:** 48890bf

### Phase 3: Mock Backend ✅
- `MoonrakerClientMock` with 7 printer profiles (Voron, K1, FlashForge, etc.)
- Factory pattern in main.cpp:759-766
- Testing: `./build/bin/helix-ui-proto --test --wizard`

---

## Phase 3: Real Printer Testing

### Prerequisites

**Printer Details:**
- Model: FlashForge Adventurer 5M Pro
- IP Address: 192.168.1.67
- Moonraker Port: 7125 (default)
- Expected Hardware: See FlashForge AD5M mock profile for baseline

**Build Requirements:**
```bash
make -j  # Ensure build is current
```

**Config Setup:**
- helixconfig.json will be created on first run
- Or manually create with: `{"moonraker": {"host": "192.168.1.67", "port": 7125}}`

---

### Test Session 1: Connection & Discovery

**Objective:** Verify connection test and hardware discovery trigger

**Steps:**
1. Start wizard at step 2 (connection test):
   ```bash
   ./build/bin/helix-ui-proto --wizard --wizard-step 2 -vv
   ```

2. Enter printer details:
   - IP: `192.168.1.67`
   - Port: `7125`
   - Click "Test Connection"

3. **Expected Results:**
   - Status changes to "Testing..."
   - Connection succeeds (green checkmark)
   - Status shows "✓ Connected to printer"
   - Next button becomes enabled
   - Log shows:
     ```
     [Moonraker] Connected to ws://192.168.1.67:7125/websocket
     [Moonraker] Discovering printer hardware...
     [Moonraker] Found X heaters, Y sensors, Z fans, W LEDs
     ```

4. **Verification:**
   - Check logs for `discover_printer()` call
   - Verify all 4 hardware arrays populated (check debug logs)
   - Confirm connection stays open (no disconnect messages)

5. **Edge Cases to Test:**
   - Invalid IP → expect error message
   - Invalid port → expect error message
   - Printer offline → expect timeout error
   - Firewall blocking → expect connection timeout

---

### Test Session 2: Bed Selection (Step 4)

**Objective:** Verify bed heater/sensor dropdowns populated from real hardware

**Steps:**
1. Navigate to step 4 (after successful connection in step 2):
   ```bash
   ./build/bin/helix-ui-proto --wizard --wizard-step 4 -vv
   ```

2. **Expected Results:**
   - **Bed Heater dropdown** shows:
     - All discovered heaters containing "bed" (e.g., "heater_bed")
     - "None" option at bottom
   - **Bed Sensor dropdown** shows:
     - All discovered sensors (no filtering)
     - "None" option at bottom

3. **Verification:**
   - Compare against mock FlashForge AD5M profile
   - Log shows filtering logic: `Filter for heater.find("bed")`
   - Select options and verify saved to config:
     ```bash
     cat helixconfig.json | jq '.printer.bed_heater, .printer.bed_sensor'
     ```

4. **Edge Cases:**
   - Printer with no heated bed → only "None" appears
   - Custom bed names ("heater_generic custom_bed") → verify matches filter
   - Multiple bed heaters → all appear in dropdown

---

### Test Session 3: Hotend Selection (Step 5)

**Objective:** Verify hotend heater/sensor dropdowns populated correctly

**Steps:**
1. Navigate to step 5:
   ```bash
   ./build/bin/helix-ui-proto --wizard --wizard-step 5 -vv
   ```

2. **Expected Results:**
   - **Hotend Heater dropdown** shows:
     - All heaters containing "extruder" or "hotend"
     - "None" option
   - **Hotend Sensor dropdown** shows:
     - Sensors containing "extruder" or "hotend"
     - "None" option

3. **Verification:**
   - Multi-extruder printers: verify extruder, extruder1, extruder2, etc. all appear
   - Log shows filtering: `heater.find("extruder") || heater.find("hotend")`
   - Selections saved to config

4. **Edge Cases:**
   - Single extruder → "extruder" appears
   - Multi-extruder (IDEX, toolchangers) → all extruders appear
   - Custom hotend names → verify filter catches them

---

### Test Session 4: Fan Selection (Step 6)

**Objective:** Verify fan dropdowns correctly separated (hotend vs part cooling)

**Steps:**
1. Navigate to step 6:
   ```bash
   ./build/bin/helix-ui-proto --wizard --wizard-step 6 -vv
   ```

2. **Expected Results:**
   - **Hotend Fan dropdown** shows:
     - Fans containing "heater_fan" or "hotend_fan"
     - "None" option
   - **Part Cooling Fan dropdown** shows:
     - Fans containing "fan" BUT NOT "heater_fan" or "hotend_fan"
     - "None" option

3. **Verification:**
   - Standard config: "fan" in part cooling, "heater_fan hotend" in hotend fan
   - No overlap between dropdowns (same fan shouldn't appear in both)
   - Log shows filtering logic (negative matching for part fan)

4. **Edge Cases:**
   - Printer with only part cooling fan → hotend fan shows "None"
   - Custom fan names ("fan_generic chamber") → verify appears in correct dropdown
   - Multiple part cooling fans → all appear

---

### Test Session 5: LED Selection (Step 7)

**Objective:** Verify LED dropdown shows all discovered LEDs

**Steps:**
1. Navigate to step 7:
   ```bash
   ./build/bin/helix-ui-proto --wizard --wizard-step 7 -vv
   ```

2. **Expected Results:**
   - **Main LED dropdown** shows:
     - All LEDs discovered (no filtering)
     - "None" option

3. **Verification:**
   - Compare against `client->get_leds()` output
   - Log shows all LED objects: "led *", "neopixel *", "dotstar *"
   - Selection saved to config

4. **Edge Cases:**
   - Printer with no LEDs → only "None" appears
   - Multiple LED strips → all appear
   - Different LED types (neopixel, dotstar) → all appear

---

### Test Session 6: Full Wizard Flow

**Objective:** Complete wizard end-to-end with real printer

**Steps:**
1. Start wizard from beginning:
   ```bash
   ./build/bin/helix-ui-proto --wizard
   ```

2. Complete all steps:
   - Step 1: WiFi setup (skip if not needed)
   - Step 2: Connection test (192.168.1.67:7125)
   - Step 3: Printer identification (name + type)
   - Step 4: Bed selection
   - Step 5: Hotend selection
   - Step 6: Fan selection
   - Step 7: LED selection
   - Step 8: Summary/finish

3. **Verification:**
   - All selections persisted in helixconfig.json
   - Config valid JSON
   - All paths populated:
     ```json
     {
       "moonraker": {"host": "...", "port": 7125},
       "printer": {
         "name": "...",
         "type": "...",
         "bed_heater": "...",
         "bed_sensor": "...",
         "hotend_heater": "...",
         "hotend_sensor": "...",
         "hotend_fan": "...",
         "part_cooling_fan": "...",
         "main_led": "..."
       }
     }
     ```

4. **Edge Cases:**
   - Going back and changing selections → verify updates config
   - Skipping optional selections (select "None") → verify config updated
   - Completing wizard multiple times → verify config overwrites correctly

---

## Known Issues & Limitations

### Hardware Filtering Fragility
**Issue:** String matching uses `find()` not regex - could match partial names incorrectly
**Impact:** Custom/unusual names might not filter correctly
**Mitigation:** Test with various printer configs, improve regex if needed

### No Re-Discovery During Wizard
**Issue:** Hardware list cached from initial discovery (step 2), not refreshed
**Impact:** If hardware config changes mid-wizard, won't reflect
**Mitigation:** Document as "restart wizard if printer config changes"

### Empty Sensor List for Bed
**Issue:** Bed sensor dropdown shows ALL sensors (no filtering)
**Impact:** User sees extruder/chamber sensors when selecting bed sensor
**Mitigation:** Consider adding filter or labeling sensors by purpose

### No Visual Feedback for Empty Lists
**Issue:** If no hardware found, dropdown just shows "None"
**Impact:** User might not know if discovery failed or hardware truly absent
**Mitigation:** Add status text: "No bed heaters found" vs "Select bed heater"

---

## Test Data Collection

### Logs to Capture
```bash
# Capture full debug output
./build/bin/helix-ui-proto --wizard -vv 2>&1 | tee test_$(date +%Y%m%d_%H%M%S).log
```

### Grep Patterns for Analysis
```bash
# Hardware discovery
grep "Moonraker.*discover\|Found.*heaters" test.log

# Dropdown population
grep "Wizard.*dropdown\|options_str" test.log

# Connection status
grep "Connection.*changed\|WebSocket" test.log

# Config saves
grep "Saved.*config\|Writing config" test.log
```

### Screenshot Checklist
- [ ] Step 2: Connection test success
- [ ] Step 4: Bed selection with real hardware
- [ ] Step 5: Hotend selection with real hardware
- [ ] Step 6: Fan selection with real hardware
- [ ] Step 7: LED selection with real hardware
- [ ] Final helixconfig.json contents

---

## Success Criteria

### Phase 3 Complete When:
- ✅ Connection test succeeds with real printer
- ✅ Hardware discovery logs show all 4 arrays populated
- ✅ All 4 hardware selection screens show real data (not just "None")
- ✅ Hardware filtering works correctly (bed vs hotend, fans separated)
- ✅ Selections saved to helixconfig.json with correct paths
- ✅ Full wizard flow completes without errors
- ✅ Edge cases tested (empty lists, multi-extruder, custom names)

### Phase 4: Next Steps
Once real printer testing succeeds:
1. Document any discovered issues in GitHub issues
2. Implement remaining wizard steps (WiFi, summary)
3. Test with multiple printer types (Voron, Creality K1, etc.)
4. Add wizard auto-detection via mDNS (future enhancement)

---

## Quick Reference Commands

```bash
# Mock testing (development)
./build/bin/helix-ui-proto --test --wizard

# Real printer testing
./build/bin/helix-ui-proto --wizard --wizard-step 2 -vv

# Jump to specific step (after initial connection)
./build/bin/helix-ui-proto --wizard --wizard-step 4 -vv

# Check current config
cat helixconfig.json | jq '.'

# Reset wizard (delete config)
rm helixconfig.json

# Build with debug symbols
make clean && make -j CXXFLAGS="-g -O0"
```

---

## Contact & Support

**Issues:** Report in GitHub issues with:
- Printer model/firmware version
- Full debug log (`-vv` output)
- helixconfig.json contents (redact IPs if public)
- Screenshots of unexpected behavior

**Test Results:** Document in HANDOFF.md or create issue with label `testing`
