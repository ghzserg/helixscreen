# Upgrading HelixScreen

This guide helps you upgrade HelixScreen to a newer version.

---

## Quick Upgrade

**Raspberry Pi / Creality K1:**
```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh -s -- --update
```

**Adventurer 5M** (no HTTPS support - two-step process):
```bash
# On your computer (replace vX.Y.Z with actual version):
VERSION=vX.Y.Z  # Check latest at https://github.com/prestonbrown/helixscreen/releases/latest
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m-${VERSION}.tar.gz"
# Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
scp -O helixscreen-ad5m-${VERSION}.tar.gz root@<printer-ip>:/data/

# On the printer (use the bundled install.sh):
# Forge-X:
/opt/helixscreen/install.sh --local /data/helixscreen-ad5m-*.tar.gz --update
# Klipper Mod:
/root/printer_software/helixscreen/install.sh --local /data/helixscreen-ad5m-*.tar.gz --update
```

This preserves your settings and updates to the latest version.

---

## If the Setup Wizard Keeps Appearing

After upgrading, if HelixScreen keeps showing the setup wizard on every boot, your configuration file format may have changed in a way that's incompatible with the new version.

### Quick Fix

The easiest solution is to delete your config file and let the wizard create a new one:

**MainsailOS (Pi):**
```bash
sudo rm /opt/helixscreen/helixconfig.json
sudo systemctl restart helixscreen
```

**Adventurer 5M (Forge-X):**
```bash
rm /opt/helixscreen/helixconfig.json
/etc/init.d/S90helixscreen restart
```

**Adventurer 5M (Klipper Mod):**
```bash
rm /root/printer_software/helixscreen/helixconfig.json
/etc/init.d/S80helixscreen restart
```

**Creality K1 (Simple AF):**
```bash
rm /usr/data/helixscreen/helixconfig.json
/etc/init.d/S99helixscreen restart
```

After restarting, the wizard will guide you through setup again. Your printer settings (Klipper, Moonraker) are not affected - only HelixScreen's display preferences need to be reconfigured.

### Alternative: Factory Reset from UI

If you can access the settings panel before the wizard appears:

1. Navigate to **Settings** (gear icon in sidebar)
2. Scroll down to **Factory Reset**
3. Tap **Factory Reset** and confirm

This clears all HelixScreen settings and restarts the wizard.

---

## What Settings Are Affected

When you reset, you'll need to reconfigure:

- WiFi connection (if not using Ethernet)
- Moonraker connection (usually auto-detected)
- Display preferences (brightness, theme, sleep timeout)
- Sound settings
- Safety preferences (E-Stop confirmation)

Your Klipper configuration, Moonraker settings, print history, and G-code files are **not affected** - they're stored separately.

---

## Upgrade to Specific Version

**Raspberry Pi / Creality K1:**
```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh -s -- --update --version v1.2.0
```

**Adventurer 5M:** Download the specific version tarball from [GitHub Releases](https://github.com/prestonbrown/helixscreen/releases), then use `--local` as shown above.

---

## Checking Your Version

**On the touchscreen:** Settings → scroll down → Version row shows current version

**Via SSH:**
```bash
# Pi:
/opt/helixscreen/bin/helix-screen --version

# K1:
/usr/data/helixscreen/bin/helix-screen --version

# AD5M (Forge-X):
/opt/helixscreen/bin/helix-screen --version

# AD5M (Klipper Mod):
/root/printer_software/helixscreen/bin/helix-screen --version
```

---

## Getting Help

If you encounter issues after upgrading:

1. Ask in the [HelixScreen Discord](https://discord.gg/rZ9dB74V) for quick help
2. Check [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common problems
3. View logs for error messages:
   - **Pi:** `sudo journalctl -u helixscreen -n 50`
   - **AD5M / K1:** `tail -50 /tmp/helixscreen.log`
4. Open an issue on [GitHub](https://github.com/prestonbrown/helixscreen/issues) with your version and any error messages

---

*Back to: [Installation Guide](INSTALL.md) | [User Guide](USER_GUIDE.md)*
