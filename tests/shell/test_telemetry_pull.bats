#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for scripts/telemetry-pull.sh
# Validates arg parsing, prerequisite checks, auth, date validation,
# dry-run, incremental caching, force mode, and default dates.

SCRIPT="scripts/telemetry-pull.sh"

setup() {
    source tests/shell/helpers.bash

    export HELIX_TELEMETRY_ADMIN_KEY="test-key-abc123"

    # Create a fake project structure so the script can resolve PROJECT_ROOT
    export TEST_PROJECT="$BATS_TEST_TMPDIR/project"
    mkdir -p "$TEST_PROJECT/scripts"
    cp "$SCRIPT" "$TEST_PROJECT/scripts/telemetry-pull.sh"
    chmod +x "$TEST_PROJECT/scripts/telemetry-pull.sh"

    SCRIPT_PATH="$TEST_PROJECT/scripts/telemetry-pull.sh"
}

# --- Helper: create a mock curl that simulates the API ---

# Build a curl mock that returns 200 for auth check and configurable list responses.
# Usage: setup_mock_api [key_lines]
#   key_lines: newline-separated keys to return from list endpoint (empty = no events)
setup_mock_api() {
    local keys="${1:-}"
    mkdir -p "$BATS_TEST_TMPDIR/bin"

    # Build the JSON response for list endpoint
    local json_keys=""
    if [ -n "$keys" ]; then
        local first=true
        while IFS= read -r k; do
            [ -z "$k" ] && continue
            if $first; then
                json_keys="{\"key\":\"$k\"}"
                first=false
            else
                json_keys="$json_keys,{\"key\":\"$k\"}"
            fi
        done <<< "$keys"
    fi

    cat > "$BATS_TEST_TMPDIR/bin/curl" << 'MOCK_OUTER'
#!/bin/bash
KEYS_JSON='KEYS_PLACEHOLDER'
# Parse args to figure out what curl is being asked to do
output_file=""
write_out=""
silent=false
url=""
args=("$@")
i=0
while [ $i -lt ${#args[@]} ]; do
    case "${args[$i]}" in
        -o) output_file="${args[$((i+1))]}"; i=$((i+2)) ;;
        -w) write_out="${args[$((i+1))]}"; i=$((i+2)) ;;
        -s|-f|-sf|-fs) i=$((i+1)) ;;
        -H) i=$((i+2)) ;;  # skip header
        *) url="${args[$i]}"; i=$((i+1)) ;;
    esac
done

# Auth check: returns http_code via -w
if [[ "$write_out" == *"http_code"* ]]; then
    if [ -n "$output_file" ]; then
        echo -n "" > "$output_file"
    fi
    echo -n "200"
    exit 0
fi

# List endpoint
if [[ "$url" == *"/v1/events/list"* ]]; then
    echo "{\"keys\":[$KEYS_JSON],\"truncated\":false}"
    exit 0
fi

# Get endpoint (download)
if [[ "$url" == *"/v1/events/get"* ]]; then
    if [ -n "$output_file" ]; then
        echo '{"event":"test","ts":1234567890}' > "$output_file"
    else
        echo '{"event":"test","ts":1234567890}'
    fi
    exit 0
fi

echo "MOCK_CURL: unhandled request: $url" >&2
exit 1
MOCK_OUTER

    # Substitute the keys JSON into the mock
    sed -i.bak "s|KEYS_PLACEHOLDER|$json_keys|" "$BATS_TEST_TMPDIR/bin/curl"
    rm -f "$BATS_TEST_TMPDIR/bin/curl.bak"
    chmod +x "$BATS_TEST_TMPDIR/bin/curl"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
}

# Create a curl mock that returns 401 for the auth check
setup_mock_api_auth_fail() {
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    cat > "$BATS_TEST_TMPDIR/bin/curl" << 'EOF'
#!/bin/bash
# Check if this is the auth check (uses -w for http_code)
for arg in "$@"; do
    if [[ "$arg" == *"http_code"* ]]; then
        echo -n "401"
        exit 0
    fi
done
echo "mock"
exit 0
EOF
    chmod +x "$BATS_TEST_TMPDIR/bin/curl"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
}

# Create a curl mock that returns 500 for the auth check
setup_mock_api_server_error() {
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    cat > "$BATS_TEST_TMPDIR/bin/curl" << 'EOF'
#!/bin/bash
for arg in "$@"; do
    if [[ "$arg" == *"http_code"* ]]; then
        echo -n "500"
        exit 0
    fi
done
echo "mock"
exit 0
EOF
    chmod +x "$BATS_TEST_TMPDIR/bin/curl"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
}

# ===== Arg parsing =====

@test "help flag shows usage and exits 0" {
    run bash "$SCRIPT_PATH" --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"Usage: telemetry-pull.sh"* ]]
    [[ "$output" == *"HELIX_TELEMETRY_ADMIN_KEY"* ]]
}

@test "short help flag -h shows usage" {
    run bash "$SCRIPT_PATH" -h
    [ "$status" -eq 0 ]
    [[ "$output" == *"Usage: telemetry-pull.sh"* ]]
}

@test "unknown option fails with error" {
    run bash "$SCRIPT_PATH" --bogus
    [ "$status" -ne 0 ]
    [[ "$output" == *"Unknown option: --bogus"* ]]
}

@test "--since without value fails" {
    run bash "$SCRIPT_PATH" --since
    [ "$status" -ne 0 ]
    [[ "$output" == *"--since requires a YYYY-MM-DD argument"* ]]
}

@test "--until without value fails" {
    run bash "$SCRIPT_PATH" --until
    [ "$status" -ne 0 ]
    [[ "$output" == *"--until requires a YYYY-MM-DD argument"* ]]
}

# ===== Prerequisite checks =====

@test "missing curl produces error" {
    # Build a minimal PATH that has everything EXCEPT curl
    mkdir -p "$BATS_TEST_TMPDIR/nocurl_bin"
    for cmd in bash jq date python3 mkdir wc tr sed cat echo rm cd pwd dirname; do
        local real_path
        real_path=$(command -v "$cmd" 2>/dev/null) || continue
        ln -sf "$real_path" "$BATS_TEST_TMPDIR/nocurl_bin/$cmd"
    done
    export PATH="$BATS_TEST_TMPDIR/nocurl_bin"

    run bash "$SCRIPT_PATH" --since 2025-01-01 --until 2025-01-02
    [ "$status" -ne 0 ]
    [[ "$output" == *"curl is not installed"* ]]
}

@test "missing jq produces error" {
    # Build a minimal PATH with curl but WITHOUT jq
    mkdir -p "$BATS_TEST_TMPDIR/nojq_bin"
    for cmd in bash curl date python3 mkdir wc tr sed cat echo rm pwd dirname; do
        local real_path
        real_path=$(command -v "$cmd" 2>/dev/null) || continue
        ln -sf "$real_path" "$BATS_TEST_TMPDIR/nojq_bin/$cmd"
    done
    export PATH="$BATS_TEST_TMPDIR/nojq_bin"

    run bash "$SCRIPT_PATH" --since 2025-01-01 --until 2025-01-02
    [ "$status" -ne 0 ]
    [[ "$output" == *"jq is not installed"* ]]
}

# ===== Auth validation =====

@test "missing HELIX_TELEMETRY_ADMIN_KEY fails" {
    unset HELIX_TELEMETRY_ADMIN_KEY

    run bash "$SCRIPT_PATH" --since 2025-01-01 --until 2025-01-02
    [ "$status" -ne 0 ]
    [[ "$output" == *"HELIX_TELEMETRY_ADMIN_KEY environment variable is required"* ]]
}

@test "empty HELIX_TELEMETRY_ADMIN_KEY fails" {
    export HELIX_TELEMETRY_ADMIN_KEY=""

    run bash "$SCRIPT_PATH" --since 2025-01-01 --until 2025-01-02
    [ "$status" -ne 0 ]
    [[ "$output" == *"HELIX_TELEMETRY_ADMIN_KEY environment variable is required"* ]]
}

# ===== Date validation =====

@test "invalid --since date format fails" {
    setup_mock_api
    run bash "$SCRIPT_PATH" --since "01-15-2025" --until "2025-01-20"
    [ "$status" -ne 0 ]
    [[ "$output" == *"--since must be in YYYY-MM-DD format"* ]]
}

@test "invalid --until date format fails" {
    setup_mock_api
    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "Jan-20-2025"
    [ "$status" -ne 0 ]
    [[ "$output" == *"--until must be in YYYY-MM-DD format"* ]]
}

@test "--since after --until fails" {
    setup_mock_api
    run bash "$SCRIPT_PATH" --since "2025-06-15" --until "2025-01-01"
    [ "$status" -ne 0 ]
    [[ "$output" == *"--since (2025-06-15) is after --until (2025-01-01)"* ]]
}

@test "valid dates pass validation" {
    setup_mock_api
    run bash "$SCRIPT_PATH" --since "2025-01-01" --until "2025-01-01"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Telemetry pull: 2025-01-01 to 2025-01-01"* ]]
}

# ===== Auth check (HTTP response) =====

@test "401 response produces admin key rejected error" {
    setup_mock_api_auth_fail
    run bash "$SCRIPT_PATH" --since "2025-01-01" --until "2025-01-01"
    [ "$status" -ne 0 ]
    [[ "$output" == *"Admin API key rejected"* ]]
}

@test "500 response produces API health check error" {
    setup_mock_api_server_error
    run bash "$SCRIPT_PATH" --since "2025-01-01" --until "2025-01-01"
    [ "$status" -ne 0 ]
    [[ "$output" == *"API health check failed"* ]]
}

# ===== Dry run =====

@test "dry run shows would-download message without creating files" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15" --dry-run
    [ "$status" -eq 0 ]
    [[ "$output" == *"dry run"* ]]
    [[ "$output" == *"Would download"* ]]
    [[ "$output" == *"nothing was actually downloaded"* ]]

    # No files should have been created in the data dir
    [ ! -d "$TEST_PROJECT/.telemetry-data/events/2025/01/15" ]
}

@test "dry run reports correct download count" {
    setup_mock_api "events/2025/01/15/1705312000000-aaa111.json
events/2025/01/15/1705312000000-bbb222.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15" --dry-run
    [ "$status" -eq 0 ]
    [[ "$output" == *"Downloaded:        2"* ]]
}

# ===== Incremental caching =====

@test "skips existing non-empty files without --force" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    # Pre-create the file with content
    local_dir="$TEST_PROJECT/.telemetry-data/events/2025/01/15"
    mkdir -p "$local_dir"
    echo '{"cached":"true"}' > "$local_dir/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Skipped (cached):  1"* ]]

    # File should still have original content (not overwritten)
    local content
    content=$(cat "$local_dir/1705312000000-abc123.json")
    [[ "$content" == *"cached"* ]]
}

@test "does not skip empty files" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    # Pre-create an empty file
    local_dir="$TEST_PROJECT/.telemetry-data/events/2025/01/15"
    mkdir -p "$local_dir"
    touch "$local_dir/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Downloaded:        1"* ]]
    [[ "$output" == *"Skipped (cached):  0"* ]]
}

# ===== Force mode =====

@test "force re-downloads existing files" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    # Pre-create the file with old content
    local_dir="$TEST_PROJECT/.telemetry-data/events/2025/01/15"
    mkdir -p "$local_dir"
    echo '{"old":"data"}' > "$local_dir/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15" --force
    [ "$status" -eq 0 ]
    [[ "$output" == *"Downloaded:        1"* ]]
    # Should NOT show skipped
    [[ "$output" == *"Skipped (cached):  0"* ]]

    # File should be overwritten with mock data
    local content
    content=$(cat "$local_dir/1705312000000-abc123.json")
    [[ "$content" == *"test"* ]]
}

# ===== Default dates =====

@test "default dates use last 30 days to today" {
    setup_mock_api

    run bash "$SCRIPT_PATH"
    [ "$status" -eq 0 ]

    # Should contain today's date as the until date
    local today
    today=$(date +%Y-%m-%d)
    [[ "$output" == *"$today"* ]]

    # Compute expected since date (30 days ago)
    local since
    if date -d "30 days ago" +%Y-%m-%d >/dev/null 2>&1; then
        since=$(date -d "30 days ago" +%Y-%m-%d)
    else
        since=$(date -j -v-30d +%Y-%m-%d)
    fi
    [[ "$output" == *"$since"* ]]
}

# ===== Multi-day range =====

@test "multi-day range checks each date" {
    setup_mock_api

    run bash "$SCRIPT_PATH" --since "2025-01-14" --until "2025-01-16"
    [ "$status" -eq 0 ]
    [[ "$output" == *"events/2025/01/14/"* ]]
    [[ "$output" == *"events/2025/01/15/"* ]]
    [[ "$output" == *"events/2025/01/16/"* ]]
}

# ===== Summary output =====

@test "summary shows total files found" {
    setup_mock_api "events/2025/01/15/1705312000000-aaa111.json
events/2025/01/15/1705312000000-bbb222.json
events/2025/01/15/1705312000000-ccc333.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Total files found: 3"* ]]
    [[ "$output" == *"Downloaded:        3"* ]]
}

@test "no events found shows appropriate message" {
    setup_mock_api ""

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15"
    [ "$status" -eq 0 ]
    [[ "$output" == *"No events found"* ]]
    [[ "$output" == *"Total files found: 0"* ]]
}

# ===== Output directory =====

@test "creates output directory structure from key paths" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15"
    [ "$status" -eq 0 ]

    # The directory should have been created
    [ -d "$TEST_PROJECT/.telemetry-data/events/2025/01/15" ]
    # The file should exist with content
    [ -s "$TEST_PROJECT/.telemetry-data/events/2025/01/15/1705312000000-abc123.json" ]
}

# ===== Flag combinations =====

@test "force and dry-run together shows would-download for existing files" {
    setup_mock_api "events/2025/01/15/1705312000000-abc123.json"

    # Pre-create file
    local_dir="$TEST_PROJECT/.telemetry-data/events/2025/01/15"
    mkdir -p "$local_dir"
    echo '{"cached":"true"}' > "$local_dir/1705312000000-abc123.json"

    run bash "$SCRIPT_PATH" --since "2025-01-15" --until "2025-01-15" --force --dry-run
    [ "$status" -eq 0 ]
    [[ "$output" == *"Would download"* ]]
    [[ "$output" == *"nothing was actually downloaded"* ]]

    # Original file should be untouched
    local content
    content=$(cat "$local_dir/1705312000000-abc123.json")
    [[ "$content" == *"cached"* ]]
}

@test "same day for --since and --until is valid" {
    setup_mock_api
    run bash "$SCRIPT_PATH" --since "2025-03-01" --until "2025-03-01"
    [ "$status" -eq 0 ]
    [[ "$output" == *"Telemetry pull: 2025-03-01 to 2025-03-01"* ]]
}
