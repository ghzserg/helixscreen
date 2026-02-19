#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# serve-local-update.sh — Local update server for testing the helix-screen
#                          download → install → restart flow end-to-end.
#
# OVERVIEW
# --------
# Builds a platform package, generates a manifest.json with a fixed high test
# version (99.0.0) guaranteed to be newer than any real install, and serves
# both over HTTP. The running helix-screen binary sees the manifest version
# as newer than itself and triggers the full self-update path without
# touching VERSION.txt.
#
# The test version is always 99.0.0 so the manifest is always seen as newer
# regardless of what version is currently installed on the device. Use
# --no-bump to serve the exact VERSION.txt version instead.
#
# Supported platforms (mirrors the *-docker Makefile targets):
#   pi            Raspberry Pi 64-bit aarch64  (default)
#   pi32          Raspberry Pi 32-bit armhf
#   ad5m          FlashForge Adventurer 5M (armv7-a)
#   cc1           Centauri Carbon 1 (armv7-a)
#   k1            Creality K1
#   k1-dynamic    Creality K1 (dynamic linking)
#   k2            Creality K2
#   snapmaker-u1  Snapmaker U1
#
# QUICK START
# -----------
#   # 1. Set your device's address (once, add to ~/.zshrc or ~/.bashrc):
#   export HELIX_TEST_PI_USER=pi
#   export HELIX_TEST_PI_HOST=helixscreen.local   # or an IP address
#
#   # 2. First time only — configure the device to use dev channel:
#   ./scripts/serve-local-update.sh --configure-pi
#
#   # 3. Every subsequent test iteration:
#   ./scripts/serve-local-update.sh [--platform PLATFORM]
#
#   helix-screen will check the manifest, see a newer version, download
#   the tarball, run install.sh, and restart itself.
#
# ENVIRONMENT VARIABLES
# ---------------------
#   HELIX_TEST_PI_HOST   Device hostname or IP  (default: helixscreen.local)
#   HELIX_TEST_PI_USER   Device SSH username    (default: pi)
#
# OPTIONS
# -------
#   --platform PLATFORM  Target platform to build and serve (default: pi).
#                        See platform list above.
#   --configure-pi   SSH into the device, write dev channel + dev_url into
#                    ~/helixscreen/config/helixconfig.json, enable HELIX_DEBUG=1
#                    in helixscreen.env for debug logging, and restart the
#                    helixscreen service. Run once per device (or after a
#                    factory reset). Requires passwordless sudo + SSH key access.
#                    Note: uses Pi paths (~/helixscreen/). For other platforms
#                    configure helixconfig.json manually.
#   --no-bump        Serve the exact version from VERSION.txt instead of
#                    99.0.0. Useful if you manually set a higher version.
#   --no-build       Skip compile + package. Patches install.sh from the repo
#                    into the existing dist/ tarball so script-only changes
#                    (install.sh, no binary changes) can be tested immediately.
#   --port PORT      HTTP port to listen on (default: 8765).
#
# DEPENDENCIES
# ------------
#   - Docker (for *-docker cross-compilation targets)
#   - python3 (for the HTTP server and device helixconfig.json patch)
#   - ssh + ssh-agent or ~/.ssh/config with key for $HELIX_TEST_PI_USER@$HELIX_TEST_PI_HOST
#   - rsync (used by package.sh)
#
# NOTES
# -----
#   - The bumped version is only used for the tarball filename and manifest.
#     VERSION.txt is never modified.
#   - After install the device binary still reports VERSION.txt's version
#     (< 99.0.0), so the next run will again offer an update — intentional
#     for iteration.
#   - To reset the device back to the stable channel, re-run --configure-pi
#     after removing the dev_url key, or delete helixconfig.json on the device.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults (overridable via env) ────────────────────────────────────────────
PI_HOST="${HELIX_TEST_PI_HOST:-helixscreen.local}"
PI_USER="${HELIX_TEST_PI_USER:-pi}"
PORT=8765
BUILD=1
BUMP=1
CONFIGURE_PI=0
PLATFORM=pi

# ── Args ──────────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --configure-pi) CONFIGURE_PI=1; shift ;;
        --no-build)     BUILD=0; shift ;;
        --no-bump)      BUMP=0; shift ;;
        --platform)     PLATFORM=$2; shift 2 ;;
        --port)         PORT=$2; shift 2 ;;
        --help|-h)
            sed -n '2,/^set -/p' "$0" | grep '^#' | sed 's/^# \?//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Version ───────────────────────────────────────────────────────────────────
BASE_VERSION="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"  # e.g. 0.10.4

# Use a fixed high test version so the manifest is always newer than any real
# install, regardless of which branch or version is currently on the Pi.
if [[ $BUMP -eq 1 ]]; then
    TEST_VERSION="99.0.0"
else
    TEST_VERSION="$BASE_VERSION"
fi

# install.sh prepends "v" to the manifest version, so the manifest must use
# bare version numbers (e.g. "99.0.0") to avoid "vv99.0.0" in the installer.
VERSION_BARE="${TEST_VERSION}"
VERSION="v${TEST_VERSION}"
TARBALL_NAME="helixscreen-${PLATFORM}-${VERSION}.tar.gz"
TARBALL_PATH="$PROJECT_DIR/dist/$TARBALL_NAME"

# ── Local IP ──────────────────────────────────────────────────────────────────
LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null || \
           ip route get 1 2>/dev/null | awk '{print $7; exit}' || \
           echo "127.0.0.1")
BASE_URL="http://${LOCAL_IP}:${PORT}"

echo ""
echo "  serve-local-update"
echo "  ══════════════════"
echo "  Platform     : $PLATFORM"
echo "  Base version : $BASE_VERSION  (VERSION.txt — unchanged)"
echo "  Test version : $TEST_VERSION  $([ $BUMP -eq 1 ] && echo "(fixed high — always newer than installed)" || echo "(no bump)")"
echo "  Tarball      : $TARBALL_NAME"
echo "  Serving at   : $BASE_URL"
echo "  Device       : ${PI_USER}@${PI_HOST}"
echo ""

# ── Git pull ──────────────────────────────────────────────────────────────────
echo "[serve-local-update] Pulling latest changes..."
_SCRIPT_REV_BEFORE=$(git -C "$PROJECT_DIR" rev-parse HEAD:scripts/serve-local-update.sh 2>/dev/null || echo "")
if ! git -C "$PROJECT_DIR" pull --ff-only; then
    echo ""
    echo "ERROR: git pull failed (conflicts or diverged history)."
    echo "  Resolve conflicts, then re-run."
    exit 1
fi
_SCRIPT_REV_AFTER=$(git -C "$PROJECT_DIR" rev-parse HEAD:scripts/serve-local-update.sh 2>/dev/null || echo "")
if [[ "$_SCRIPT_REV_BEFORE" != "$_SCRIPT_REV_AFTER" ]]; then
    echo "[serve-local-update] Script updated — restarting with new version..."
    echo ""
    exec "$SCRIPT_DIR/serve-local-update.sh" "$@"
fi
echo ""

# ── Build ─────────────────────────────────────────────────────────────────────
if [[ $BUILD -eq 1 ]]; then
    echo "[serve-local-update] Compiling ${PLATFORM} binary (docker)..."
    make -C "$PROJECT_DIR" "${PLATFORM}-docker"
    echo ""
    echo "[serve-local-update] Packaging..."
    "$SCRIPT_DIR/package.sh" "${PLATFORM}" --version "$VERSION"
    echo ""
fi

if [[ ! -f "$TARBALL_PATH" ]]; then
    echo "ERROR: Tarball not found: $TARBALL_PATH"
    echo ""
    echo "  Expected: $TARBALL_PATH"
    echo "  Either run without --no-build, or build manually:"
    echo "    scripts/package.sh ${PLATFORM} --version $VERSION"
    exit 1
fi

# ── Patch install.sh into existing tarball (--no-build fast path) ─────────────
if [[ $BUILD -eq 0 ]]; then
    echo "[serve-local-update] Patching install.sh into tarball..."
    PATCH_DIR="$(mktemp -d)"
    tar -xzf "$TARBALL_PATH" -C "$PATCH_DIR"
    cp "$PROJECT_DIR/scripts/install.sh" "$PATCH_DIR/helixscreen/install.sh"
    chmod +x "$PATCH_DIR/helixscreen/install.sh"
    COPYFILE_DISABLE=1 tar -czf "$TARBALL_PATH" --owner=0 --group=0 -C "$PATCH_DIR" helixscreen
    rm -rf "$PATCH_DIR"
    echo "[serve-local-update] install.sh patched."
    echo ""
fi

# ── Manifest ──────────────────────────────────────────────────────────────────
MANIFEST_PATH="$PROJECT_DIR/dist/manifest.json"
cat > "$MANIFEST_PATH" <<EOF
{
  "version": "${VERSION_BARE}",
  "channel": "dev",
  "assets": {
    "${PLATFORM}": {
      "filename": "${TARBALL_NAME}",
      "url": "${BASE_URL}/${TARBALL_NAME}"
    }
  }
}
EOF
echo "[serve-local-update] Manifest → $MANIFEST_PATH"

# ── Configure Pi ──────────────────────────────────────────────────────────────
if [[ $CONFIGURE_PI -eq 1 ]]; then
    echo "[serve-local-update] Configuring ${PI_USER}@${PI_HOST} ..."
    ssh "${PI_USER}@${PI_HOST}" "python3 -c \"
import json, os, re

# Write dev channel + dev_url into helixconfig.json (read by Config::get_instance())
path = os.path.expanduser('~/helixscreen/config/helixconfig.json')
if not os.path.exists(path):
    print('  ERROR: helixconfig.json not found at', path)
    print('  Run the HelixScreen setup wizard first, then re-run --configure-pi.')
    raise SystemExit(1)
with open(path) as f:
    data = json.load(f)
data.setdefault('update', {})
data['update']['channel'] = 2
data['update']['dev_url'] = '${BASE_URL}/'
with open(path, 'w') as f:
    json.dump(data, f, indent=2)
print('  update/channel =', data['update']['channel'], '(dev)')
print('  update/dev_url =', data['update']['dev_url'])

# Enable HELIX_DEBUG=1 in helixscreen.env (enables -vv debug logging via launcher).
# Launcher checks INSTALL_DIR/config/ first, then /etc/helixscreen/.
DEFAULTS = '''# HelixScreen environment configuration
# See config/helixscreen.env in the repo for full documentation.
MOONRAKER_HOST=localhost
MOONRAKER_PORT=7125
HELIX_LOG_LEVEL=info
'''
env_candidates = [
    os.path.expanduser('~/helixscreen/config/helixscreen.env'),
    '/etc/helixscreen/helixscreen.env',
]
env_path = next((p for p in env_candidates if os.path.exists(p)), None)
if env_path is None:
    env_path = env_candidates[0]
    os.makedirs(os.path.dirname(env_path), exist_ok=True)
    content = DEFAULTS
    print('  Created', env_path, 'with defaults')
else:
    content = open(env_path).read()
    # If the file is missing essential defaults (was corrupted by a previous run),
    # restore them before appending HELIX_DEBUG.
    if 'MOONRAKER_HOST' not in content:
        content = DEFAULTS + content
        print('  Restored missing defaults in', env_path)
# Remove any existing HELIX_DEBUG line then append it enabled
content = re.sub(r'^#?\s*HELIX_DEBUG=.*\n?', '', content, flags=re.MULTILINE)
content = content.rstrip('\n') + '\nHELIX_DEBUG=1\n'
open(env_path, 'w').write(content)
print('  HELIX_DEBUG=1  (debug logging enabled in', env_path + ')')
\""
    echo ""
    echo "[serve-local-update] Restarting helix-screen on ${PI_USER}@${PI_HOST} ..."
    ssh "${PI_USER}@${PI_HOST}" "sudo systemctl restart helixscreen"
    echo "[serve-local-update] helix-screen restarted."
    echo "  To revert: remove update/dev_url from ~/helixscreen/config/helixconfig.json on the Pi."
    echo ""
fi

# ── Serve ─────────────────────────────────────────────────────────────────────
echo ""
echo "  Ready"
echo "  ─────────────────────────────────────────────────"
echo "  Manifest : ${BASE_URL}/manifest.json"
echo "  Tarball  : ${BASE_URL}/${TARBALL_NAME}"
echo ""
echo "  Device expects (set via --configure-pi if not already done):"
echo "    update/channel  = 2"
echo "    update/dev_url  = \"${BASE_URL}/\""
echo ""
echo "  Press Ctrl+C to stop."
echo "  ─────────────────────────────────────────────────"
echo ""

cd "$PROJECT_DIR/dist"
python3 -m http.server "$PORT"
