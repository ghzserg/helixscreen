#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Resolve raw backtrace addresses to function names using symbol maps.
#
# Usage:
#   resolve-backtrace.sh <version> <platform> <addr1> [addr2] ...
#   resolve-backtrace.sh --base <load_base> <version> <platform> <addr1> ...
#   resolve-backtrace.sh --crash-file <crash.txt> [platform]
#
# Downloads the symbol map from R2 (cached locally) and resolves each
# hex address to the nearest function name + offset.
#
# Examples:
#   ./scripts/resolve-backtrace.sh 0.9.9 pi 0x00412abc 0x00401234
#   ./scripts/resolve-backtrace.sh --base 0xaaaab0449000 0.9.19 pi 0xaaaab04a1234
#   ./scripts/resolve-backtrace.sh --crash-file config/crash.txt
#   ./scripts/resolve-backtrace.sh --crash-file config/crash.txt pi

set -euo pipefail

readonly CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/helixscreen/symbols"
readonly R2_BASE_URL="${HELIX_R2_URL:-https://releases.helixscreen.org}/symbols"

LOAD_BASE=0
AUTO_DETECT_BASE=false
CRASH_FILE=""

usage() {
    echo "Usage: $(basename "$0") [options] <version> <platform> <addr1> [addr2] ..."
    echo "       $(basename "$0") --crash-file <crash.txt> [platform]"
    echo ""
    echo "Resolves raw backtrace addresses to function names using symbol maps."
    echo ""
    echo "Options:"
    echo "  --base <hex>         ELF load base (ASLR offset) to subtract from addresses"
    echo "  --crash-file <path>  Parse crash.txt directly (extracts version, backtrace, load_base)"
    echo ""
    echo "Arguments:"
    echo "  version   Release version (e.g., 0.9.9)"
    echo "  platform  Build platform (pi, pi32, ad5m, k1, k2)"
    echo "  addr*     Hex addresses to resolve (with or without 0x prefix)"
    echo ""
    echo "Environment:"
    echo "  HELIX_R2_URL    Override R2 base URL (default: https://releases.helixscreen.org)"
    echo "  HELIX_SYM_FILE  Use a local .sym file instead of downloading"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") 0.9.19 pi 0x00412abc 0x00401234"
    echo "  $(basename "$0") --base 0xaaaab0449000 0.9.19 pi 0xaaaab04a1234 0xaaaab04b5678"
    echo "  $(basename "$0") --crash-file ~/helixscreen/config/crash.txt"
    exit 1
}

# Parse options
while [[ $# -gt 0 ]]; do
    case "${1:-}" in
        --base)
            if [[ $# -lt 2 ]]; then
                echo "Error: --base requires a hex address argument" >&2
                exit 1
            fi
            base_hex="${2#0x}"
            base_hex="${base_hex#0X}"
            LOAD_BASE=$((16#$base_hex))
            shift 2
            ;;
        --crash-file)
            if [[ $# -lt 2 ]]; then
                echo "Error: --crash-file requires a file path argument" >&2
                exit 1
            fi
            CRASH_FILE="$2"
            shift 2
            ;;
        --help|-h)
            usage
            ;;
        *)
            break
            ;;
    esac
done

# Crash file mode: extract version, platform, backtrace, load_base from file
if [[ -n "$CRASH_FILE" ]]; then
    if [[ ! -f "$CRASH_FILE" ]]; then
        echo "Error: Crash file not found: $CRASH_FILE" >&2
        exit 1
    fi

    # Extract version
    VERSION=$(grep "^version:" "$CRASH_FILE" | cut -d: -f2 | tr -d '[:space:]')
    if [[ -z "$VERSION" ]]; then
        echo "Error: No version found in crash file" >&2
        exit 1
    fi

    # Extract platform from file, or use command-line override
    FILE_PLATFORM=$(grep "^platform:" "$CRASH_FILE" | cut -d: -f2 | tr -d '[:space:]')
    if [[ $# -ge 1 ]]; then
        PLATFORM="$1"
        shift
    elif [[ -n "$FILE_PLATFORM" ]]; then
        PLATFORM="$FILE_PLATFORM"
    else
        echo "Error: No platform in crash file — specify as argument" >&2
        exit 1
    fi

    # Extract load_base if present and not overridden by --base
    if (( LOAD_BASE == 0 )); then
        file_base=$(grep "^load_base:" "$CRASH_FILE" | cut -d: -f2 | tr -d '[:space:]')
        if [[ -n "$file_base" ]]; then
            base_hex="${file_base#0x}"
            base_hex="${base_hex#0X}"
            LOAD_BASE=$((16#$base_hex))
            echo "Using load_base from crash file: $file_base" >&2
        fi
    fi

    # Extract backtrace addresses
    ADDRS=()
    while IFS= read -r line; do
        addr=$(echo "$line" | cut -d: -f2 | tr -d '[:space:]')
        if [[ -n "$addr" ]]; then
            ADDRS+=("$addr")
        fi
    done < <(grep "^bt:" "$CRASH_FILE")

    if [[ ${#ADDRS[@]} -eq 0 ]]; then
        echo "Error: No backtrace addresses found in crash file" >&2

        # Fall back to registers
        reg_pc=$(grep "^reg_pc:" "$CRASH_FILE" | cut -d: -f2 | tr -d '[:space:]')
        reg_lr=$(grep "^reg_lr:" "$CRASH_FILE" | cut -d: -f2 | tr -d '[:space:]')
        if [[ -n "$reg_pc" ]]; then
            echo "Using PC/LR registers as fallback" >&2
            ADDRS+=("$reg_pc")
            [[ -n "$reg_lr" ]] && ADDRS+=("$reg_lr")
        else
            exit 1
        fi
    fi

    echo "Parsed crash file: v${VERSION}/${PLATFORM}, ${#ADDRS[@]} addresses" >&2
    set -- "${ADDRS[@]}"
else
    # Normal mode: version platform addr...
    if [[ $# -lt 3 ]]; then
        usage
    fi
    VERSION="$1"
    PLATFORM="$2"
    shift 2
fi

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

# =============================================================================
# Auto-detect ASLR load base by matching _start and main to backtrace frames
# =============================================================================
auto_detect_load_base() {
    local -a addrs=("$@")

    # Get _start and main addresses from symbol file
    local start_line main_line
    start_line=$(grep ' T _start$' "$SYM_FILE" | head -1)
    main_line=$(grep ' T main$' "$SYM_FILE" | head -1)

    if [[ -z "$start_line" ]] || [[ -z "$main_line" ]]; then
        return 1
    fi

    local start_file_hex start_file_dec main_file_hex main_file_dec
    start_file_hex=$(echo "$start_line" | awk '{print $1}')
    main_file_hex=$(echo "$main_line" | awk '{print $1}')
    start_file_dec=$((16#$start_file_hex))
    main_file_dec=$((16#$main_file_hex))

    # The distance between _start and main in the file should match in the backtrace
    local expected_gap=$(( start_file_dec - main_file_dec ))

    # Try each pair of backtrace addresses to see if any pair has the same gap
    for (( i=0; i<${#addrs[@]}; i++ )); do
        local addr_i_hex="${addrs[$i]#0x}"
        addr_i_hex="${addr_i_hex#0X}"
        local addr_i_dec=$((16#$addr_i_hex))

        for (( j=i+1; j<${#addrs[@]}; j++ )); do
            local addr_j_hex="${addrs[$j]#0x}"
            addr_j_hex="${addr_j_hex#0X}"
            local addr_j_dec=$((16#$addr_j_hex))

            local gap=$(( addr_j_dec - addr_i_dec ))

            # Check if this pair matches main→_start gap
            if (( gap == expected_gap )); then
                # addr_i = main, addr_j = _start
                local candidate_base=$(( addr_i_dec - main_file_dec ))
                if (( candidate_base > 0 )); then
                    printf '%d' "$candidate_base"
                    return 0
                fi
            fi

            # Check reverse: addr_i = _start, addr_j = main
            local neg_gap=$(( addr_i_dec - addr_j_dec ))
            if (( neg_gap == expected_gap )); then
                local candidate_base=$(( addr_j_dec - main_file_dec ))
                if (( candidate_base > 0 )); then
                    printf '%d' "$candidate_base"
                    return 0
                fi
            fi
        done
    done

    # Fallback: try matching individual addresses to _start or main
    # (less reliable, but works if only one is in the backtrace)
    for addr_raw in "${addrs[@]}"; do
        local addr_hex="${addr_raw#0x}"
        addr_hex="${addr_hex#0X}"
        local addr_dec=$((16#$addr_hex))

        # Try as _start
        local base_candidate=$(( addr_dec - start_file_dec ))
        if (( base_candidate > 0 )); then
            # Verify: does main also land on a symbol?
            local main_runtime=$(( base_candidate + main_file_dec ))
            for verify_addr in "${addrs[@]}"; do
                local v_hex="${verify_addr#0x}"
                v_hex="${v_hex#0X}"
                local v_dec=$((16#$v_hex))
                if (( v_dec == main_runtime )); then
                    printf '%d' "$base_candidate"
                    return 0
                fi
            done
        fi

        # Try as main
        base_candidate=$(( addr_dec - main_file_dec ))
        if (( base_candidate > 0 )); then
            local start_runtime=$(( base_candidate + start_file_dec ))
            for verify_addr in "${addrs[@]}"; do
                local v_hex="${verify_addr#0x}"
                v_hex="${v_hex#0X}"
                local v_dec=$((16#$v_hex))
                if (( v_dec == start_runtime )); then
                    printf '%d' "$base_candidate"
                    return 0
                fi
            done
        fi
    done

    return 1
}

# Auto-detect load base if not provided
if (( LOAD_BASE == 0 )); then
    detected_base=$(auto_detect_load_base "$@" || true)
    if [[ -n "$detected_base" ]] && (( detected_base > 0 )); then
        LOAD_BASE=$detected_base
        AUTO_DETECT_BASE=true
        printf "Auto-detected ASLR load base: 0x%x (matched _start + main in backtrace)\n" "$LOAD_BASE" >&2
    fi
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

    # Subtract ASLR load base if provided
    local orig_addr_hex="$addr_hex"
    if (( LOAD_BASE > 0 )); then
        addr_dec=$(( addr_dec - LOAD_BASE ))
        addr_hex=$(printf '%x' "$addr_dec")
    fi

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
        if (( LOAD_BASE > 0 )); then
            printf "0x%s (file: 0x%s) → %s+0x%x\n" "$orig_addr_hex" "$addr_hex" "$best_name" "$offset"
        else
            printf "0x%s → %s+0x%x\n" "$addr_hex" "$best_name" "$offset"
        fi
    else
        if (( LOAD_BASE > 0 )); then
            printf "0x%s (file: 0x%s) → (unknown)\n" "$orig_addr_hex" "$addr_hex"
        else
            printf "0x%s → (unknown)\n" "$addr_hex"
        fi
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
if (( LOAD_BASE > 0 )); then
    if [[ "$AUTO_DETECT_BASE" == "true" ]]; then
        printf "ASLR load base: 0x%x (auto-detected from _start/main)\n" "$LOAD_BASE"
    else
        printf "ASLR load base: 0x%x (will subtract from addresses)\n" "$LOAD_BASE"
    fi
fi
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
