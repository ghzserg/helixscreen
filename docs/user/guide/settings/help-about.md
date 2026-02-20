# Settings: Help & About

---

## Help & Support

Three quick links at the bottom of the Settings panel:

| Action | What It Does |
|--------|--------------|
| **Upload Debug Bundle** | Collects logs and system info for support (see below) |
| **Discord Community** | Join **discord.gg/helixscreen** for community help and feedback |
| **Documentation** | Visit **docs.helixscreen.org** for guides and reference |

### Debug Bundles

When you need help troubleshooting an issue:

1. Tap **Upload Debug Bundle** in Settings
2. The bundle collects your logs, system info, and configuration (no personal data)
3. Tap **Upload** to send the bundle securely
4. Share the resulting code with the HelixScreen team on Discord or GitHub

Debug bundles contain only technical information needed for troubleshooting — no passwords, API keys, or personal data.

---

## About

Tap **About HelixScreen** to open the About overlay with system information and update management.

| Item | Description |
|------|-------------|
| **Printer Name** | The name of your connected printer (set during setup wizard) |
| **Current Version** | Your installed HelixScreen version |
| **Update Channel** | Stable, Beta, or Dev — only visible when beta features are enabled |
| **Check for Updates** | Check for and install new versions (hidden on Android) |
| **Klipper** | Installed Klipper version (fetched from Moonraker) |
| **Moonraker** | Installed Moonraker version |
| **OS** | Operating system version |
| **Print Hours** | Total print hours tracked — tap to open the [History Dashboard](../advanced.md) |

### Enabling Beta Features

Tap the **Current Version** row seven times to toggle beta features — works like Android's "tap build number" developer mode.

When beta features are enabled:
- **Update Channel** selector appears (Stable / Beta / Dev)
- Additional items appear in the Advanced panel (Macro Browser, Input Shaping, Z-Offset Calibration, Timelapse, etc.)
- **Plugins** section appears in Settings
- Tap seven more times to disable

### Update Channels

| Channel | Description |
|---------|-------------|
| **Stable** | Recommended. Tested releases only. |
| **Beta** | Preview builds with new features. May have rough edges. |
| **Dev** | Development builds. Requires a configured `dev_url` in your config file. |

---

[Back to Settings](../settings.md) | [Prev: System](system.md) | [Next: LED Settings](led-settings.md)
