#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AD5M Boot Diagnostic Script
# Captures system state for debugging boot/networking issues.
#
# Usage:
#   sh debug-ad5m-boot.sh           # Run manually, output to terminal
#   sh debug-ad5m-boot.sh --boot    # Run at boot, save to persistent file
#
# The --boot flag saves output to /opt/helixscreen/config/boot-diagnostics.log
# so diagnostics are captured even if networking dies after reboot.
#
# Quick setup for automatic boot capture:
#   wget -O /opt/helixscreen/debug-boot.sh <URL>
#   chmod +x /opt/helixscreen/debug-boot.sh
#   # Add to end of /etc/init.d/S90helixscreen (before the final exit):
#   #   /opt/helixscreen/debug-boot.sh --boot &

DIAG_LOG="/opt/helixscreen/config/boot-diagnostics.log"

if [ "$1" = "--boot" ]; then
    # Wait for boot to settle so we capture final state
    sleep 30
    mkdir -p "$(dirname "$DIAG_LOG")"
    exec > "$DIAG_LOG" 2>&1
fi

echo "===== HelixScreen AD5M Boot Diagnostics ====="
echo "Date: $(date)"
echo ""

echo "--- Forge-X Version ---"
cat /opt/config/mod/version.txt 2>/dev/null || echo "(not found)"
echo ""

echo "--- HelixScreen Version ---"
/opt/helixscreen/bin/helix-screen --version 2>/dev/null || echo "(not found)"
echo ""

echo "--- Display Mode ---"
grep "display" /opt/config/mod_data/variables.cfg 2>/dev/null || echo "(not found)"
echo ""

echo "--- Boot Flags ---"
for f in /tmp/custom_boot_f /tmp/helixscreen_active /tmp/not_first_launch_f \
         /tmp/wifi_connected_f /tmp/ethernet_connected_f /tmp/net_ip; do
    if [ -f "$f" ]; then
        content=$(cat "$f" 2>/dev/null)
        if [ -n "$content" ]; then
            echo "  EXISTS: $f  ($content)"
        else
            echo "  EXISTS: $f"
        fi
    else
        echo "  MISSING: $f"
    fi
done
echo ""

echo "--- Ethernet Config (from stock firmware JSON) ---"
CONFIG_FILE=$(ls /opt/config/Adventurer5M*.json 2>/dev/null | head -1)
if [ -f "$CONFIG_FILE" ]; then
    grep -E "ethernet|wifi|network" "$CONFIG_FILE" 2>/dev/null || echo "(no network fields)"
else
    echo "(config file not found)"
fi
echo ""

echo "--- Network Interfaces ---"
/sbin/ifconfig eth0 2>/dev/null || echo "eth0: not found"
echo ""
/sbin/ifconfig wlan0 2>/dev/null || echo "wlan0: not found"
echo ""

echo "--- Network Daemons ---"
echo "wpa_supplicant: $(pidof wpa_supplicant 2>/dev/null || echo 'not running')"
echo "wpa_cli:        $(pidof wpa_cli 2>/dev/null || echo 'not running')"
echo "udhcpc:         $(pidof udhcpc 2>/dev/null || echo 'not running')"
echo ""

echo "--- Moonraker Status ---"
wget -q -O - --timeout=3 http://127.0.0.1:7125/server/info 2>/dev/null || echo "Moonraker unreachable"
echo ""
echo ""

echo "--- Running Processes (filtered) ---"
ps w 2>/dev/null | grep -E "helix|guppy|moonraker|klippy|klipper|wpa_|udhcpc|firmwareExe|ffstartup" | grep -v grep
echo ""

echo "--- Init Scripts ---"
ls -la /etc/init.d/S90helixscreen 2>/dev/null || echo "S90helixscreen: not found"
ls -la /opt/helixscreen/platform/hooks.sh 2>/dev/null || echo "platform hooks: not found"
echo ""

echo "--- Disabled Services ---"
cat /opt/helixscreen/config/.disabled_services 2>/dev/null || echo "(none recorded)"
echo ""

echo "--- HelixScreen Service Script Permissions ---"
ls -la /opt/config/mod/.root/S35tslib /opt/config/mod/.root/S80guppyscreen 2>/dev/null
echo ""

echo "--- Memory ---"
free -m 2>/dev/null || cat /proc/meminfo 2>/dev/null | head -5
echo ""

echo "--- Boot Log (Forge-X) ---"
cat /data/logFiles/boot.log 2>/dev/null || echo "(empty or not found)"
echo ""

echo "--- HelixScreen Log (last 30 lines) ---"
tail -30 /tmp/helixscreen.log 2>/dev/null || echo "(not found)"
echo ""

echo "--- WiFi Log ---"
tail -20 /data/logFiles/wifi.log 2>/dev/null || echo "(not found)"
echo ""

echo "--- Kernel Messages (network/touch related) ---"
dmesg 2>/dev/null | grep -iE "8821cu|wlan|eth0|dhcp|udhcpc|link|carrier|oom|killed|helixscreen" | tail -30
echo ""

echo "===== End Diagnostics ====="
