# HelixPrint - Moonraker Plugin

A Moonraker component that handles modified G-code files while preserving original file attribution in Klipper's `print_stats` and Moonraker's history.

## Quick Install (Recommended)

**One-liner install** - run this on your printer:

```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/moonraker-plugin/remote-install.sh | bash
```

This automatically:
- Clones the plugin to `~/helix_print`
- Creates the Moonraker symlink
- Adds `[helix_print]` config section
- Adds `[update_manager helix_print]` for auto-updates
- Restarts Moonraker

## Problem

When a touchscreen UI (like HelixScreen) modifies G-code files before printing (e.g., disabling bed leveling), the modifications create temporary files that pollute:
- Klipper's `print_stats.filename` shows the temp filename
- Moonraker's print history records the temp filename
- Statistics aggregate under temp filenames instead of originals

## Solution

This plugin provides a single API endpoint that:
1. Saves modified G-code to a temp directory
2. Creates a symlink with the original filename pointing to the temp file
3. Starts the print via the symlink (so Klipper sees the original name)
4. Patches history entries to record the original filename
5. Automatically cleans up temp files after a configurable delay

## Manual Installation

If you prefer manual installation:

```bash
# Clone or copy the plugin
cd /path/to/helixscreen/moonraker-plugin

# Run the installer
./install.sh

# Or specify Moonraker path explicitly
./install.sh /home/pi/moonraker
```

The installer creates a symlink from Moonraker's components directory to `helix_print.py`.

### Update Manager (Auto-Updates)

To enable automatic updates via Moonraker's update manager, add to `moonraker.conf`:

```ini
[update_manager helix_print]
type: git_repo
origin: https://github.com/prestonbrown/helixscreen.git
path: ~/helix_print
primary_branch: main
managed_services: moonraker
```

## Configuration

Add to your `moonraker.conf`:

```ini
[helix_print]
# Enable/disable the plugin (default: True)
enabled: True

# Directory for storing modified G-code (relative to gcodes root)
temp_dir: .helix_temp

# Directory for symlinks (relative to gcodes root)
symlink_dir: .helix_print

# Time in seconds to keep temp files after print completes (default: 86400 = 24 hours)
cleanup_delay: 86400
```

Restart Moonraker after configuration:
```bash
sudo systemctl restart moonraker
```

## API Reference

### Check Plugin Status

```http
GET /server/helix/status
```

Response:
```json
{
  "result": {
    "enabled": true,
    "temp_dir": ".helix_temp",
    "symlink_dir": ".helix_print",
    "cleanup_delay": 86400,
    "active_prints": 0,
    "version": "1.0.0"
  }
}
```

### Start Modified Print

```http
POST /server/helix/print_modified
Content-Type: application/json

{
  "original_filename": "benchy.gcode",
  "modified_content": "<full G-code content with modifications>",
  "modifications": ["bed_leveling_disabled", "z_tilt_disabled"],
  "copy_metadata": true
}
```

Response:
```json
{
  "result": {
    "original_filename": "benchy.gcode",
    "print_filename": ".helix_print/benchy.gcode",
    "temp_filename": ".helix_temp/mod_1702400000_benchy.gcode",
    "status": "printing"
  }
}
```

#### Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `original_filename` | string | Yes | - | Path to the original G-code file (relative to gcodes root) |
| `modified_content` | string | Yes | - | Complete modified G-code content |
| `modifications` | array | No | `[]` | List of modification identifiers (for logging/history) |
| `copy_metadata` | bool | No | `true` | Whether to copy thumbnails from original |

#### Errors

| Code | Description |
|------|-------------|
| 400 | Original file not found |
| 500 | Failed to write temp file or create symlink |
| 503 | Plugin is disabled |

## How It Works

```
┌─────────────────────────────────────────────────────────────────┐
│ Client (HelixScreen)                                            │
│ POST /server/helix/print_modified                               │
│ { original_filename, modified_content, modifications }          │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│ helix_print Plugin                                              │
├─────────────────────────────────────────────────────────────────┤
│ 1. Validate original file exists                                │
│ 2. Save modified content → .helix_temp/mod_123_benchy.gcode     │
│ 3. Copy thumbnails from original                                │
│ 4. Create symlink: .helix_print/benchy.gcode → temp file        │
│ 5. Start print: SDCARD_PRINT_FILE FILENAME=".helix_print/..."   │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│ Klipper                                                         │
│ print_stats.filename = ".helix_print/benchy.gcode"              │
│ (Follows symlink, reads modified content)                       │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼ (on completion)
┌─────────────────────────────────────────────────────────────────┐
│ helix_print Plugin (event handler)                              │
├─────────────────────────────────────────────────────────────────┤
│ 1. Patch history: filename → "benchy.gcode"                     │
│ 2. Store modifications in auxiliary_data                        │
│ 3. Delete symlink immediately                                   │
│ 4. Schedule temp file deletion (24h default)                    │
└─────────────────────────────────────────────────────────────────┘
```

## File Locations

After installation:
```
~/printer_data/gcodes/
├── benchy.gcode              # Original file (unchanged)
├── .helix_temp/
│   └── mod_1702400000_benchy.gcode  # Modified content
├── .helix_print/
│   └── benchy.gcode          # Symlink → .helix_temp/mod_...
└── .thumbs/
    ├── benchy-32x32.png      # Original thumbnails
    └── mod_1702400000_benchy-32x32.png  # Symlink → original
```

## Uninstallation

```bash
./install.sh --uninstall
```

Then remove the `[helix_print]` section from `moonraker.conf` and restart Moonraker.

## Troubleshooting

### Plugin not loading

1. Check Moonraker logs: `journalctl -u moonraker -f`
2. Verify symlink exists: `ls -la ~/moonraker/moonraker/components/helix_print.py`
3. Ensure `[helix_print]` section is in `moonraker.conf`

### API returns 404

The plugin may not be loaded. Check:
```bash
curl http://localhost:7125/server/info
```
Look for `helix_print` in the components list.

### Symlinks not working

Some file systems don't support symlinks. The plugin requires a POSIX-compliant filesystem for the gcodes directory.

## License

GPL-3.0-or-later

## Contributing

This plugin is part of the HelixScreen project. Issues and PRs welcome at:
https://github.com/prestonbrown/helixscreen
