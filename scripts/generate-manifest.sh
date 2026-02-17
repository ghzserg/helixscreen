#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# generate-manifest.sh — Generate manifest.json from a directory of release tarballs.
# Shared by CI (release.yml) and local dev releases (dev-release.sh).

set -euo pipefail

# Default values
VERSION=""
TAG=""
NOTES=""
DIR=""
BASE_URL=""
OUTPUT=""

usage() {
    cat <<EOF
Usage: generate-manifest.sh --version VERSION --tag TAG --notes NOTES --dir DIR --base-url URL --output FILE

Generate a manifest.json from release tarballs in DIR.

Options:
  --version VERSION   Version string (e.g., "0.9.5")
  --tag TAG           Git tag (e.g., "v0.9.5")
  --notes NOTES       Release notes text
  --dir DIR           Directory containing helixscreen-{platform}-*.tar.gz files
  --base-url URL      Base URL for download links (e.g., "https://releases.helixscreen.org/dev")
  --output FILE       Output manifest.json path
  --help              Show this help message
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)  VERSION="$2";  shift 2 ;;
        --tag)      TAG="$2";      shift 2 ;;
        --notes)    NOTES="$2";    shift 2 ;;
        --dir)      DIR="$2";      shift 2 ;;
        --base-url) BASE_URL="$2"; shift 2 ;;
        --output)   OUTPUT="$2";   shift 2 ;;
        --help)     usage ;;
        *)
            echo "Error: Unknown option $1" >&2
            exit 1
            ;;
    esac
done

# Validate required arguments
missing=()
[[ -z "$VERSION" ]] && missing+=("--version")
[[ -z "$TAG" ]]     && missing+=("--tag")
[[ -z "$DIR" ]]     && missing+=("--dir")
[[ -z "$BASE_URL" ]] && missing+=("--base-url")
[[ -z "$OUTPUT" ]]  && missing+=("--output")

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Error: Missing required arguments: ${missing[*]}" >&2
    exit 1
fi

if [[ ! -d "$DIR" ]]; then
    echo "Error: Directory not found: $DIR" >&2
    exit 1
fi

# Check required tools
if ! command -v jq &>/dev/null; then
    echo "Error: jq not found. Please install it." >&2
    exit 1
fi

# Determine sha256 command
if command -v shasum &>/dev/null; then
    SHA256_CMD="shasum -a 256"
elif command -v sha256sum &>/dev/null; then
    SHA256_CMD="sha256sum"
else
    echo "Error: Neither shasum nor sha256sum found" >&2
    exit 1
fi

# Scan for platform tarballs
PLATFORMS=(pi pi32 ad5m cc1 k1)
FOUND_ANY=false
ASSETS_JSON="{}"

for plat in "${PLATFORMS[@]}"; do
    # Find tarball matching helixscreen-{platform}-*.tar.gz
    tarball=""
    for f in "$DIR"/helixscreen-"${plat}"-*.tar.gz; do
        if [[ -f "$f" ]]; then
            tarball="$f"
            break
        fi
    done

    if [[ -z "$tarball" ]]; then
        continue
    fi

    FOUND_ANY=true
    filename=$(basename "$tarball")
    sha256=$($SHA256_CMD "$tarball" | awk '{print $1}')
    url="${BASE_URL}/${filename}"

    # Add platform entry to assets JSON
    ASSETS_JSON=$(echo "$ASSETS_JSON" | jq \
        --arg plat "$plat" \
        --arg url "$url" \
        --arg sha256 "$sha256" \
        '.[$plat] = {url: $url, sha256: $sha256}')

    # Check for corresponding ZIP file (used by Moonraker type:zip updates)
    zipfile="$DIR/helixscreen-${plat}.zip"
    if [[ -f "$zipfile" ]]; then
        zip_sha256=$($SHA256_CMD "$zipfile" | awk '{print $1}')
        zip_url="${BASE_URL}/helixscreen-${plat}.zip"

        ASSETS_JSON=$(echo "$ASSETS_JSON" | jq \
            --arg plat "$plat" \
            --arg zip_url "$zip_url" \
            --arg zip_sha256 "$zip_sha256" \
            '.[$plat] += {zip_url: $zip_url, zip_sha256: $zip_sha256}')
    fi
done

if [[ "$FOUND_ANY" == "false" ]]; then
    echo "Error: No helixscreen-*.tar.gz tarballs found in $DIR" >&2
    exit 1
fi

# Generate timestamp
PUBLISHED_AT=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# Build final manifest
jq -n \
    --arg version "$VERSION" \
    --arg tag "$TAG" \
    --arg notes "${NOTES:-}" \
    --arg published_at "$PUBLISHED_AT" \
    --argjson assets "$ASSETS_JSON" \
    '{
        version: $version,
        tag: $tag,
        notes: $notes,
        published_at: $published_at,
        assets: $assets
    }' > "$OUTPUT"

echo "Generated $OUTPUT with platforms: $(echo "$ASSETS_JSON" | jq -r 'keys | join(", ")')"
