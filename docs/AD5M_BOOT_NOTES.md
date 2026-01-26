# AD5M Boot Process Notes

## Build and Deploy

```bash
# Build for AD5M (requires Docker/Colima)
make ad5m-docker

# Deploy to AD5M (uses ad5m.local by default)
make deploy-ad5m

# Deploy to specific IP (when mDNS isn't working)
AD5M_HOST=192.168.1.67 make deploy-ad5m

# Reboot AD5M
ssh root@192.168.1.67 "/sbin/reboot"

# Watch logs
ssh root@192.168.1.67 'tail -f /var/log/messages | grep helix'
```

**Notes:**
- Native `make ad5m` often fails - use Docker via `make ad5m-docker`
- Output: `build/ad5m/bin/helix-screen`
- Uses `scp -O` for legacy SCP protocol (BusyBox has no sftp-server)

---

## ForgeX Configuration

### Required Settings
1. **GUPPY mode** in `/opt/config/mod_data/variables.cfg`:
   ```
   display = 'GUPPY'
   ```

2. **Disable GuppyScreen** init scripts:
   ```bash
   chmod -x /opt/config/mod/.root/S80guppyscreen /opt/config/mod/.root/S35tslib
   ```

3. **Disable stock UI** in `/opt/auto_run.sh` - comment out ffstartup-arm line

---

## Backlight Control

### How It Works
- AD5M uses Allwinner DISP2 driver with custom ioctls on `/dev/disp`
- No standard `/sys/class/backlight` interface
- ForgeX provides `backlight.py` wrapper inside chroot

| Ioctl | Code | Purpose |
|-------|------|---------|
| SET_BRIGHTNESS | 0x102 | Set brightness 0-255 |
| GET_BRIGHTNESS | 0x103 | Read current brightness |
| BACKLIGHT_ENABLE | 0x104 | Turn backlight on |
| BACKLIGHT_DISABLE | 0x105 | Turn backlight off |

### Manual Test
```bash
# Check brightness
/usr/sbin/chroot /data/.mod/.forge-x /root/printer_data/py/backlight.py

# Set brightness
/usr/sbin/chroot /data/.mod/.forge-x /root/printer_data/py/backlight.py 90
```

---

## Smart screen.sh Patch

### The Problem
ForgeX's S99root does a required backlight 0→100 cycle for display init. Klipper's
`reset_screen` delayed_gcode later dims to 10%. We need to allow the first but block the second.

### The Solution
Patch `/opt/config/mod/.shell/screen.sh` backlight case:
```bash
backlight)
    # Skip non-100 backlight changes when HelixScreen is controlling display
    if [ -f /tmp/helixscreen_active ] && [ "$2" != "100" ]; then
        exit 0
    fi
    ...
```

This allows `backlight 100` (needed for S99root init) but blocks other values (Klipper eco dimming).

**Installer handles this automatically** via `patch_forgex_screen_sh()` function.

---

## Brightness Inversion Bug

### Symptom
Brightness slider works backwards - higher values = dimmer screen. Affects both UI slider
AND raw backlight.py commands.

### Cause
Allwinner DISP2 driver can enter an inverted PWM state. Unknown root cause, but appears
related to the sequence of ENABLE/SET_BRIGHTNESS ioctls during initialization.

### Fix
**BacklightBackendAllwinner** now resets driver state on init:
1. DISABLE (clears PWM state)
2. Brief delay
3. ENABLE + SET_BRIGHTNESS(max)

This is done once during BacklightBackend construction in DisplayManager::init().

### Manual Fix (if needed)
```bash
/usr/sbin/chroot /data/.mod/.forge-x /root/printer_data/py/backlight.py 0
sleep 1
/usr/sbin/chroot /data/.mod/.forge-x /root/printer_data/py/backlight.py 90
```

---

## Display Timeouts

Two independent settings in `helixconfig.json`:

| Setting | Purpose | Default |
|---------|---------|---------|
| `dim_sec` | Time before dimming | 600 (10 min) |
| `dim_brightness` | Brightness % when dimmed | 30 |
| `sleep_sec` | Time before full blank | 1800 (30 min) |

---

## Boot Sequence Summary

1. ForgeX splash shown with backlight ON
2. HelixScreen init creates `/tmp/helixscreen_active` flag
3. S99root runs `backlight 0` → **BLOCKED**, `backlight 100` → **ALLOWED**
4. HelixScreen starts, BacklightBackendAllwinner resets driver state
5. DisplayManager sets brightness to 100%
6. Klipper becomes ready (~15-25s), delayed_gcode fires 3s later
7. Klipper's `backlight 10` → **BLOCKED** by smart patch
8. 20-second safety timer restores user's configured brightness

---

## Key Files

**Repository:**
- `src/api/backlight_backend.cpp` - BacklightBackendAllwinner with reset
- `src/application/display_manager.cpp` - 20s delayed brightness timer
- `scripts/lib/installer/forgex.sh` - smart screen.sh patch

**On AD5M:**
- `/opt/config/mod/.shell/screen.sh` - patched for HelixScreen
- `/opt/helixscreen/config/helixconfig.json` - display settings
- `/tmp/helixscreen_active` - flag file (created on start, removed on stop)
- `/etc/init.d/S90helixscreen` - init script (the one that runs at boot)
