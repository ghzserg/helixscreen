#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Resolve raw backtrace addresses to function names using symbol maps.
#
# Usage: resolve-backtrace.sh <version> <platform> <addr1> [addr2] ...
#
# Downloads the symbol map from R2 (cached locally) and resolves each
# hex address to the nearest function name + offset.
#
# Examples:
#   ./scripts/resolve-backtrace.sh 0.9.9 pi 0x00412abc 0x00401234
#   ./scripts/resolve-backtrace.sh 0.9.9 pi32 0x10400 0x10500 0x10600

set -euo pipefail

readonly CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/helixscreen/symbols"
readonly R2_BASE_URL="${HELIX_R2_URL:-https://releases.helixscreen.com}/symbols"

usage() {
    echo "Usage: $(basename "$0") <version> <platform> <addr1> [addr2] ..."
    echo ""
    echo "Resolves raw backtrace addresses to function names using symbol maps."
    echo ""
    echo "Arguments:"
    echo "  version   Release version (e.g., 0.9.9)"
    echo "  platform  Build platform (pi, pi32, ad5m, k1, k2)"
    echo "  addr*     Hex addresses to resolve (with or without 0x prefix)"
    echo ""
    echo "Environment:"
    echo "  HELIX_R2_URL    Override R2 base URL (default: https://releases.helixscreen.com)"
    echo "  HELIX_SYM_FILE  Use a local .sym file instead of downloading"
    exit 1
}

if [[ $# -lt 3 ]]; then
    usage
fi

VERSION="$1"
PLATFORM="$2"
shift 2

# Determine symbol file path
if [[ -n "${HELIX_SYM_FILE:-}" ]]; then
    SYM_FILE="$HELIX_SYM_FILE"
    if [[ ! -f "$SYM_FILE" ]]; then
        echo "Error: Symbol file not found: $SYM_FILE" >&2
        exit 1
    fi
else
    SYM_FILE="${CACHE_DIR}/v${VERSION}/${PLATFORM}.sym"

    if [[ ! -f "$SYM_FILE" ]]; then
        echo "Downloading symbol map for v${VERSION}/${PLATFORM}..." >&2
        mkdir -p "$(dirname "$SYM_FILE")"
        SYM_URL="${R2_BASE_URL}/v${VERSION}/${PLATFORM}.sym"
        if ! curl -fsSL -o "$SYM_FILE" "$SYM_URL"; then
            echo "Error: Failed to download symbol map from $SYM_URL" >&2
            echo "  Check version/platform or set HELIX_SYM_FILE for a local file." >&2
            rm -f "$SYM_FILE"
            exit 1
        fi
        echo "Cached: $SYM_FILE" >&2
    fi
fi

# Validate symbol file has content
if [[ ! -s "$SYM_FILE" ]]; then
    echo "Error: Symbol file is empty: $SYM_FILE" >&2
    exit 1
fi

# resolve_address <hex_addr>
# Scans the sorted symbol table (nm -nC output) to find the
# containing function. nm output format: "00000000004xxxxx T function_name"
resolve_address() {
    local addr_input="$1"
    # Normalize: strip 0x prefix, lowercase
    local addr_hex="${addr_input#0x}"
    addr_hex="${addr_hex#0X}"
    addr_hex=$(echo "$addr_hex" | tr '[:upper:]' '[:lower:]')

    # Convert to decimal for comparison
    local addr_dec
    addr_dec=$((16#$addr_hex))

    local best_name=""
    local best_addr=0
    local best_addr_hex=""

    # Read symbol file: each line is "ADDR TYPE NAME"
    # We only care about T/t (text/code) symbols
    while IFS=' ' read -r sym_addr sym_type sym_name rest; do
        # Skip non-text symbols
        case "$sym_type" in
            T|t|W|w) ;;
            *) continue ;;
        esac

        # Skip empty names
        [[ -z "$sym_name" ]] && continue

        # If there's extra text (demangled names with spaces), append it
        if [[ -n "$rest" ]]; then
            sym_name="$sym_name $rest"
        fi

        local sym_dec
        sym_dec=$((16#$sym_addr))

        if (( sym_dec <= addr_dec )); then
            best_name="$sym_name"
            best_addr=$sym_dec
            best_addr_hex="$sym_addr"
        else
            # Past our address — the previous symbol is the match
            break
        fi
    done < "$SYM_FILE"

    if [[ -n "$best_name" ]]; then
        local offset=$(( addr_dec - best_addr ))
        printf "0x%s → %s+0x%x\n" "$addr_hex" "$best_name" "$offset"
    else
        printf "0x%s → (unknown)\n" "$addr_hex"
    fi
}

# Check for addr2line fallback
LOCAL_BINARY=""
for candidate in \
    "build/bin/helix-screen" \
    "build/${PLATFORM}/bin/helix-screen"; do
    if [[ -f "$candidate" ]]; then
        LOCAL_BINARY="$candidate"
        break
    fi
done

echo "Resolving ${#@} address(es) against v${VERSION}/${PLATFORM}..."
echo ""

for addr in "$@"; do
    resolve_address "$addr"

    # If we have a local (unstripped) binary, also try addr2line for source info
    if [[ -n "$LOCAL_BINARY" ]]; then
        line_info=$(addr2line -e "$LOCAL_BINARY" -f -C "$addr" 2>/dev/null || true)
        if [[ -n "$line_info" ]] && ! echo "$line_info" | grep -q "??"; then
            echo "    $(echo "$line_info" | tail -1)"
        fi
    fi
done
