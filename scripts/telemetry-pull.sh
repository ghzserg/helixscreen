#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pull telemetry event data via the telemetry worker's admin API.
# Requires HELIX_TELEMETRY_ADMIN_KEY env var (NOT the ingest key in the binary).
# Events are stored at: events/YYYY/MM/DD/{epochMs}-{random6hex}.json

set -euo pipefail

# Auto-load credentials from project root if available
SCRIPT_DIR_EARLY="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$SCRIPT_DIR_EARLY/../.env.telemetry"
if [[ -f "$ENV_FILE" ]] && [[ -z "${HELIX_TELEMETRY_ADMIN_KEY:-}" ]]; then
    # shellcheck source=/dev/null
    source "$ENV_FILE"
fi

API_BASE="https://telemetry.helixscreen.org"
DATA_DIR=".telemetry-data/events"

# Defaults
SINCE=""
UNTIL=""
FORCE=false
DRY_RUN=false

usage() {
    cat <<'EOF'
Usage: telemetry-pull.sh [OPTIONS]

Pull telemetry events from the HelixScreen telemetry API.

Requires HELIX_TELEMETRY_ADMIN_KEY environment variable.

Options:
  --since YYYY-MM-DD   Start date (default: 30 days ago)
  --until YYYY-MM-DD   End date (default: today)
  --force              Re-download files even if they exist locally
  --dry-run            Show what would be downloaded without downloading
  -h, --help           Show this help message

Environment:
  HELIX_TELEMETRY_ADMIN_KEY   Admin API key (required, NOT the ingest key)
EOF
    exit 0
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --since)
            [[ -n "${2:-}" ]] || die "--since requires a YYYY-MM-DD argument"
            SINCE="$2"
            shift 2
            ;;
        --until)
            [[ -n "${2:-}" ]] || die "--until requires a YYYY-MM-DD argument"
            UNTIL="$2"
            shift 2
            ;;
        --force)
            FORCE=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

# Validate prerequisites
command -v curl >/dev/null 2>&1 || die "curl is not installed"
command -v jq >/dev/null 2>&1 || die "jq is not installed. Install with: brew install jq"

# Validate admin key
[[ -n "${HELIX_TELEMETRY_ADMIN_KEY:-}" ]] || die "HELIX_TELEMETRY_ADMIN_KEY environment variable is required"

# Validate date format
validate_date() {
    local label="$1" val="$2"
    if ! [[ "$val" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
        die "$label must be in YYYY-MM-DD format, got: $val"
    fi
    if date -d "$val" +%s >/dev/null 2>&1; then
        return 0
    elif date -j -f "%Y-%m-%d" "$val" +%s >/dev/null 2>&1; then
        return 0
    else
        die "$label is not a valid date: $val"
    fi
}

# Cross-platform date math
date_to_epoch() {
    local d="$1"
    if date -d "$d" +%s >/dev/null 2>&1; then
        date -d "$d" +%s
    else
        date -j -f "%Y-%m-%d" "$d" +%s
    fi
}

epoch_to_ymd() {
    local e="$1"
    if date -d "@$e" +%Y-%m-%d >/dev/null 2>&1; then
        date -d "@$e" +%Y-%m-%d
    else
        date -j -r "$e" +%Y-%m-%d
    fi
}

# Resolve defaults
if [[ -z "$SINCE" ]]; then
    if date -d "30 days ago" +%Y-%m-%d >/dev/null 2>&1; then
        SINCE=$(date -d "30 days ago" +%Y-%m-%d)
    else
        SINCE=$(date -j -v-30d +%Y-%m-%d)
    fi
fi

if [[ -z "$UNTIL" ]]; then
    UNTIL=$(date +%Y-%m-%d)
fi

validate_date "--since" "$SINCE"
validate_date "--until" "$UNTIL"

since_epoch=$(date_to_epoch "$SINCE")
until_epoch=$(date_to_epoch "$UNTIL")

if [[ "$since_epoch" -gt "$until_epoch" ]]; then
    die "--since ($SINCE) is after --until ($UNTIL)"
fi

# Find project root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$PROJECT_ROOT/$DATA_DIR"

echo "Telemetry pull: $SINCE to $UNTIL"
if $DRY_RUN; then
    echo "(dry run - no files will be downloaded)"
fi
echo "Output: $OUTPUT_DIR"
echo ""

# Quick auth check
http_code=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-API-Key: $HELIX_TELEMETRY_ADMIN_KEY" \
    "$API_BASE/v1/events/list?prefix=events/&limit=1")
if [[ "$http_code" == "401" ]]; then
    die "Admin API key rejected (HTTP 401). Check HELIX_TELEMETRY_ADMIN_KEY."
elif [[ "$http_code" != "200" ]]; then
    die "API health check failed (HTTP $http_code). Is the worker deployed?"
fi

# Generate list of dates to check
dates=()
current_epoch="$since_epoch"
while [[ "$current_epoch" -le "$until_epoch" ]]; do
    dates+=("$(epoch_to_ymd "$current_epoch")")
    current_epoch=$((current_epoch + 86400))
done

total_downloaded=0
total_skipped=0
total_errors=0
total_files=0

for d in "${dates[@]}"; do
    prefix="events/${d:0:4}/${d:5:2}/${d:8:2}/"

    echo "Checking $prefix ..."

    # List all objects for this date prefix (handle pagination)
    all_keys=""
    cursor=""
    while true; do
        list_url="$API_BASE/v1/events/list?prefix=$prefix&limit=1000"
        if [[ -n "$cursor" ]]; then
            list_url="${list_url}&cursor=${cursor}"
        fi

        list_response=$(curl -sf \
            -H "X-API-Key: $HELIX_TELEMETRY_ADMIN_KEY" \
            "$list_url" 2>&1) || {
            echo "  WARNING: Failed to list objects for $prefix"
            total_errors=$((total_errors + 1))
            break
        }

        page_keys=$(echo "$list_response" | jq -r '.keys[].key // empty' 2>/dev/null) || page_keys=""
        if [[ -n "$page_keys" ]]; then
            if [[ -n "$all_keys" ]]; then
                all_keys="$all_keys"$'\n'"$page_keys"
            else
                all_keys="$page_keys"
            fi
        fi

        truncated=$(echo "$list_response" | jq -r '.truncated // false' 2>/dev/null)
        if [[ "$truncated" == "true" ]]; then
            cursor=$(echo "$list_response" | jq -r '.cursor // empty' 2>/dev/null)
            if [[ -z "$cursor" ]]; then
                echo "  WARNING: Truncated but no cursor, stopping pagination"
                break
            fi
        else
            break
        fi
    done

    if [[ -z "$all_keys" ]]; then
        echo "  No events found."
        continue
    fi

    date_count=$(echo "$all_keys" | wc -l | tr -d ' ')
    echo "  Found $date_count file(s)."

    while IFS= read -r key; do
        [[ -z "$key" ]] && continue
        total_files=$((total_files + 1))

        local_path="$OUTPUT_DIR/${key#events/}"

        # Check if already downloaded
        if [[ -f "$local_path" ]] && [[ -s "$local_path" ]] && ! $FORCE; then
            total_skipped=$((total_skipped + 1))
            continue
        fi

        if $DRY_RUN; then
            echo "  Would download: $key"
            total_downloaded=$((total_downloaded + 1))
            continue
        fi

        mkdir -p "$(dirname "$local_path")"

        # Download via admin API
        encoded_key=$(python3 -c "import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=''))" "$key")
        if curl -sf \
            -H "X-API-Key: $HELIX_TELEMETRY_ADMIN_KEY" \
            -o "$local_path" \
            "$API_BASE/v1/events/get?key=$encoded_key"; then
            total_downloaded=$((total_downloaded + 1))
        else
            echo "  WARNING: Failed to download $key"
            total_errors=$((total_errors + 1))
            rm -f "$local_path"
        fi
    done <<< "$all_keys"
done

echo ""
echo "--- Summary ---"
echo "Total files found: $total_files"
echo "Downloaded:        $total_downloaded"
echo "Skipped (cached):  $total_skipped"
if [[ "$total_errors" -gt 0 ]]; then
    echo "Errors:            $total_errors"
fi
if $DRY_RUN; then
    echo "(dry run - nothing was actually downloaded)"
fi
