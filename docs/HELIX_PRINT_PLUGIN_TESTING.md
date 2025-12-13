# Helix Print Plugin - Testing & Integration Plan

This document outlines the testing plan for the `helix_print` Moonraker plugin once it's installed on a Klipper printer.

## Prerequisites

- [ ] Plugin installed on test printer (192.168.1.112 or 192.168.1.67)
- [ ] `[helix_print]` section added to `moonraker.conf`
- [ ] Moonraker restarted after installation
- [ ] HelixScreen built from `feature/helix-print-plugin` branch

## Phase 1: Plugin Installation Verification

### 1.1 Verify Plugin Loads

```bash
# Check Moonraker logs for plugin initialization
journalctl -u moonraker -n 100 | grep -i helix

# Expected output:
# [helix_print] Component loaded
# [helix_print] Temp directory: .helix_temp
# [helix_print] Symlink directory: .helix_print
```

### 1.2 Test Plugin Status Endpoint

```bash
# Query plugin status via HTTP
curl http://192.168.1.112:7125/server/helix/status

# Expected response:
# {"result": {"enabled": true, "version": "1.0.0", ...}}
```

### 1.3 Verify HelixScreen Detection

```bash
# Run HelixScreen with verbose logging
./build/bin/helix-screen --moonraker 192.168.1.112 -p print-select -vv 2>&1 | grep helix

# Expected output:
# [Moonraker API] helix_print plugin detected (enabled=true)
# [PrintSelectPanel] helix_print plugin available
```

## Phase 2: Modified Print Flow Testing

### 2.1 Basic Modified Print Test

**Test Case**: Start a print with bed leveling disabled

1. Navigate to Print Select panel
2. Select a G-code file
3. Toggle "Skip Bed Leveling" option
4. Start print
5. Verify:
   - [ ] Print starts successfully
   - [ ] `print_stats.filename` shows original filename (not `.helix_temp/...`)
   - [ ] Modified G-code actually skips bed leveling commands

**Verification Commands**:
```bash
# Check current print filename
curl http://192.168.1.112:7125/printer/objects/query?print_stats

# Check symlink was created
ssh pi@helixpi.local "ls -la ~/printer_data/gcodes/.helix_print/"

# Check temp file exists
ssh pi@helixpi.local "ls -la ~/printer_data/gcodes/.helix_temp/"
```

### 2.2 History Entry Verification

**Test Case**: Verify history shows original filename after print completes

1. Complete or cancel a modified print
2. Check Moonraker history:
   ```bash
   curl http://192.168.1.112:7125/server/history/list?limit=5
   ```
3. Verify:
   - [ ] `filename` field shows original name (e.g., `benchy.gcode`)
   - [ ] `auxiliary_data` contains modification metadata
   - [ ] HelixScreen history panel shows correct filename

### 2.3 Cleanup Verification

**Test Case**: Verify temp files are cleaned up

1. After print completes, check:
   ```bash
   # Symlink should be deleted immediately
   ssh pi@helixpi.local "ls -la ~/printer_data/gcodes/.helix_print/"

   # Temp file should be scheduled for cleanup (check after 24h or configured delay)
   ssh pi@helixpi.local "ls -la ~/printer_data/gcodes/.helix_temp/"
   ```

2. Verify database tracking:
   ```bash
   # Check plugin database for cleanup schedule
   curl http://192.168.1.112:7125/server/helix/pending_cleanup
   ```

## Phase 3: Edge Case Testing

### 3.1 Concurrent Print Handling

**Test Case**: Attempt to start second modified print while one is running

- Expected: Error or queue behavior (TBD based on implementation)

### 3.2 Print Cancellation

**Test Case**: Cancel a modified print mid-way

1. Start modified print
2. Cancel via HelixScreen or Mainsail
3. Verify:
   - [ ] Symlink deleted
   - [ ] History entry patched with original filename
   - [ ] Temp file scheduled for cleanup

### 3.3 Moonraker Restart During Print

**Test Case**: Restart Moonraker while modified print is running

1. Start modified print
2. `sudo systemctl restart moonraker`
3. Verify:
   - [ ] Plugin recovers state from database
   - [ ] Print continues (Klipper handles this)
   - [ ] Cleanup scheduled correctly after print ends

### 3.4 Duplicate Filename Handling

**Test Case**: Start modified print of same file twice

1. Print `benchy.gcode` with modifications
2. While printing, try to start another modified `benchy.gcode`
3. Verify:
   - [ ] Second print handled gracefully (error or unique naming)

## Phase 4: Fallback Testing

### 4.1 Plugin Unavailable Fallback

**Test Case**: Verify legacy flow works when plugin not installed

1. Test against Moonraker without plugin (192.168.1.67 if plugin only on .112)
2. Start modified print from HelixScreen
3. Verify:
   - [ ] Warning logged: "helix_print plugin not installed, using legacy flow"
   - [ ] Print starts via legacy upload flow
   - [ ] Filename shows `.helix_temp/mod_xxx_name.gcode` (expected for legacy)

### 4.2 Plugin Disabled Fallback

**Test Case**: Plugin installed but `enabled: false` in config

1. Set `enabled: False` in `moonraker.conf`
2. Restart Moonraker
3. Verify HelixScreen falls back to legacy flow

## Phase 5: Performance Testing

### 5.1 Large File Handling

**Test Case**: Modified print of large G-code file (>100MB)

1. Select large G-code file
2. Apply modifications
3. Measure:
   - [ ] Time to start print (should be similar to normal)
   - [ ] Memory usage during transfer
   - [ ] No timeout errors

### 5.2 Rapid Start/Cancel Cycles

**Test Case**: Rapidly start and cancel modified prints

1. Start modified print
2. Cancel immediately
3. Repeat 5 times
4. Verify:
   - [ ] No orphaned temp files
   - [ ] No database corruption
   - [ ] All symlinks cleaned up

## Test Matrix

| Test | Printer .112 | Printer .67 | Notes |
|------|--------------|-------------|-------|
| Plugin detection | | | |
| Basic modified print | | | |
| History patching | | | |
| Cleanup (symlink) | | | |
| Cleanup (temp file) | | | |
| Print cancellation | | | |
| Fallback (no plugin) | N/A | | Test on .67 |
| Large file | | | |

## Known Issues / TODO

- [ ] Metadata copying (thumbnails) from original to temp file
- [ ] Handle case where original file is deleted during print
- [ ] Add modification details to history `auxiliary_data`
- [ ] Configurable cleanup delay (currently hardcoded 24h)

## Post-Testing Checklist

Before merging to main:

- [ ] All Phase 1-4 tests pass on at least one printer
- [ ] No regressions in legacy flow
- [ ] Unit tests pass (`make test-moonraker`)
- [ ] No memory leaks (run with `--memory-report`)
- [ ] Documentation updated (README in moonraker-plugin/)
- [ ] Git history clean (squash if needed)

## Merge Preparation

```bash
# From the worktree
cd /Users/pbrown/Code/Printing/helixscreen-helix-print

# Ensure all changes committed
git status

# Review changes vs main
git log main..HEAD --oneline

# Create PR or merge directly
git checkout main
git merge feature/helix-print-plugin
git push origin main

# Clean up worktree (optional, after merge)
cd /Users/pbrown/Code/Printing/helixscreen
git worktree remove ../helixscreen-helix-print
```
