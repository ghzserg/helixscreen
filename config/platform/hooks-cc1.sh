#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: Elegoo Centauri Carbon 1 (Open Centauri firmware)
#
# The CC1 runs an OpenWrt/BusyBox system with procd init. The Open Centauri
# firmware manages Klipper + Moonraker. HelixScreen renders directly to the
# framebuffer (/dev/fb0, 480x272, 32bpp ARGB8888).
#
# SoC: Allwinner R528 (sun8iw20), Cortex-A7 dual-core, 128MB RAM
# Display: 480x272 LCD, Allwinner disp subsystem (no standard backlight iface)
# Touch: Goodix gt9xxnew_ts on /dev/input/event1
# Storage: UDISK (6.5GB ext4) mounted at /opt and /root
# SSH access: root@<ip> (port 22 or bind-shell on 4567)

# Stop any competing screen UIs so HelixScreen has exclusive framebuffer access.
platform_stop_competing_uis() {
    # Stop any known competing third-party UIs
    for ui in guppyscreen GuppyScreen KlipperScreen klipperscreen featherscreen FeatherScreen; do
        if command -v killall >/dev/null 2>&1; then
            killall "$ui" 2>/dev/null || true
        else
            for pid in $(pidof "$ui" 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
        fi
    done

    # Kill python-based KlipperScreen if running
    # shellcheck disable=SC2009
    for pid in $(ps aux 2>/dev/null | grep -E 'python.*screen\.py' | grep -v grep | awk '{print $2}'); do
        echo "Killing KlipperScreen python process (PID $pid)"
        kill "$pid" 2>/dev/null || true
    done

    # Brief pause to let processes exit
    sleep 1
}

# The CC1 uses the Allwinner disp subsystem for backlight control.
# There is no standard /sys/class/backlight interface.
platform_enable_backlight() {
    return 0
}

# Open Centauri manages Klipper/Moonraker - they should be available by the
# time HelixScreen starts.
platform_wait_for_services() {
    return 0
}

platform_pre_start() {
    export HELIX_CACHE_DIR="/opt/helixscreen/cache"
    return 0
}

platform_post_stop() {
    return 0
}
