# Printer Default Configurations

This directory contains pre-configured `helixconfig.json` files for specific printer models.

## Usage

If you have a supported printer, copy the appropriate config to skip the setup wizard:

```bash
cp config/presets/adventurer-5m-pro.json helixconfig.json
```

The app will use this config directly without running the first-time setup wizard.

## Supported Printers

| File | Printer Model |
|------|---------------|
| `adventurer-5m-pro.json` | FlashForge Adventurer 5M Pro |

## Creating New Configs

To add support for a new printer:

1. Complete the setup wizard on the target printer
2. Copy the generated `helixconfig.json`
3. Sanitize any sensitive data (API keys, etc.)
4. Save as `config/presets/<printer-name>.json`

## Config Notes

These configs assume:
- Local Moonraker connection (`127.0.0.1:7125`)
- No API key required (local network)
- Standard Klipper heater/fan naming conventions
