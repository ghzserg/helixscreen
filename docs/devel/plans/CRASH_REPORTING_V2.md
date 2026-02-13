# Crash Reporting V2: Improved Diagnostics

**Status:** Proposed
**Date:** 2026-02-13
**Triggered by:** First real field crash (#65) — SIGSEGV on Pi32, only 2 backtrace frames, both in shared libraries, zero actionable information.

## Problem Statement

Our crash handler works — it captured the signal, wrote a crash file, auto-reported to GitHub via telemetry, and the issue appeared. That's great. But the report was useless:

1. **Only 2 backtrace frames** — `backtrace()` on ARM32 without frame pointers can't unwind through optimized code
2. **Both addresses in shared libraries** (0xf7xxxxxx) — no frames from our binary at all
3. **No log context** — the GitHub auto-report template doesn't include the log tail
4. **No fault details** — we don't capture *what* address caused the SIGSEGV or the register state
5. **No runtime context** — after 53 minutes of uptime, we have no idea what the user was doing

This spec defines five improvements, ordered by impact and implementation complexity.

---

## Current Architecture (Reference)

**Signal handler** (`crash_handler.cpp`):
- Registers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE via `sigaction()` with `SA_RESETHAND`
- Writes line-oriented `config/crash.txt` using only async-signal-safe functions
- Captures backtrace via glibc `backtrace()` (up to 64 frames)
- Static pre-allocated buffers — no heap, no mutex, no spdlog

**Crash reporter** (`crash_reporter.cpp`):
- On next startup, parses `crash.txt` + collects system info (platform, RAM, CPU, log tail)
- Auto-sends to `crash.helixscreen.org/v1/report` (CF Worker)
- Generates QR-code-friendly GitHub issue URL as fallback
- Saves `crash_report.txt` for manual retrieval

**Crash file format:**
```
signal:11
name:SIGSEGV
version:0.9.17
timestamp:1739437100
uptime:3174
bt:0x920bac
bt:0xf7101290
```

**Telemetry integration:** TelemetryManager reads crash.txt independently, enqueues as event if telemetry opted in. CrashReporter owns the file lifecycle (deletion after user interaction).

**Symbol resolution:** Offline via `scripts/resolve-backtrace.sh` — downloads `.sym` from R2 bucket, maps addresses to function names.

---

## Improvement 1: Frame Pointers in Release Builds

**Impact:** High — fixes the root cause of bad ARM32 backtraces
**Effort:** Trivial (one line in Makefile)
**Risk:** Negligible

### Problem

GCC with `-O2` omits frame pointers by default. On ARM32, `backtrace()` uses frame pointer chain walking to unwind the stack. No frame pointers = no unwind = useless backtrace. We already use `-fno-omit-frame-pointer` for ASAN builds but not for release.

### Solution

Add `-fno-omit-frame-pointer` to release `CFLAGS` and `CXXFLAGS` in the Makefile.

### Impact on Binary

- **Size:** Zero increase. The flag doesn't add instructions — it tells the compiler to keep R11 (ARM) / RBP (x86) as a dedicated frame pointer register instead of using it for general allocation.
- **Performance:** One fewer register available to the optimizer. Typical impact is 1-3% in compute-heavy code. For a UI application doing mostly I/O and rendering (where LVGL does the heavy lifting in its own code), this is undetectable.
- **Applies to:** Our code only. Submodule flags (`SUBMODULE_CFLAGS`/`SUBMODULE_CXXFLAGS`) are separate — LVGL and libhv will continue to build as before. This means frames *within* LVGL may still be missing, but we'll at least see our own call chain leading into the library.

### Implementation

```makefile
# Add after existing CFLAGS/CXXFLAGS definitions
CFLAGS += -fno-omit-frame-pointer
CXXFLAGS += -fno-omit-frame-pointer
```

### Consideration: Submodule Frame Pointers

We could also add `-fno-omit-frame-pointer` to `SUBMODULE_CFLAGS`/`SUBMODULE_CXXFLAGS` to get frames through LVGL. Trade-off: LVGL has tight rendering loops where one fewer register *might* matter on Pi32 (512MB Pi Zero-class devices). Recommend starting with our code only and adding submodule frame pointers if we still see gaps.

---

## Improvement 2: Capture Faulting Address, Fault Code, and Register State

**Impact:** High — tells us *what* crashed and *where*, even with incomplete backtraces
**Effort:** Moderate (platform-specific ucontext handling)
**Risk:** Low (additive, async-signal-safe)

### Problem

Current handler uses basic `sigaction` without `SA_SIGINFO`. We get the signal number but nothing else:
- No faulting address (`si_addr`) — was it a null deref at 0x0? A wild pointer at 0xdeadbeef? A stack overflow?
- No fault code (`si_code`) — was the page unmapped (`SEGV_MAPERR`) or permission denied (`SEGV_ACCERR`)?
- No register state — on ARM32, the link register (LR) alone tells you the return address (the caller), which is often the most important frame

### Solution

Switch to `SA_SIGINFO` handler signature and capture additional crash metadata.

### New Crash File Fields

```
signal:11
name:SIGSEGV
version:0.9.17
timestamp:1739437100
uptime:3174
fault_addr:0x00000000
fault_code:1
fault_code_name:SEGV_MAPERR
reg_pc:0x00920bac
reg_sp:0xbe8ff420
reg_lr:0x0091a3c0
bt:0x920bac
bt:0xf7101290
```

New fields:
- `fault_addr` — the address that caused the fault (`siginfo_t.si_addr`). Null pointer = `0x00000000`. Stack overflow typically shows an address near the stack boundary.
- `fault_code` — numeric signal sub-code (`siginfo_t.si_code`)
- `fault_code_name` — human-readable: `SEGV_MAPERR` (unmapped), `SEGV_ACCERR` (permission), `BUS_ADRALN` (alignment), `FPE_INTDIV` (divide by zero), etc.
- `reg_pc` — program counter at crash (the faulting instruction)
- `reg_sp` — stack pointer (helps detect stack overflow — compare with stack boundaries)
- `reg_lr` — link register / return address (ARM only — the caller of the faulting function)

### Platform-Specific Register Access

Registers live in `ucontext_t.uc_mcontext`, which differs per architecture:

```cpp
static void write_register_state(int fd, const ucontext_t* uctx) {
#if defined(__arm__)
    // ARM32 (Pi32)
    write_field(fd, "reg_pc", uctx->uc_mcontext.arm_pc);
    write_field(fd, "reg_sp", uctx->uc_mcontext.arm_sp);
    write_field(fd, "reg_lr", uctx->uc_mcontext.arm_lr);
#elif defined(__aarch64__)
    // ARM64 (Pi 4/5 64-bit)
    write_field(fd, "reg_pc", uctx->uc_mcontext.pc);
    write_field(fd, "reg_sp", uctx->uc_mcontext.sp);
    // ARM64 doesn't expose LR directly in mcontext on all kernels
    // x30 is the link register
    write_field(fd, "reg_lr", uctx->uc_mcontext.regs[30]);
#elif defined(__x86_64__)
    // x86_64 (AD5M, dev machines)
    write_field(fd, "reg_pc", uctx->uc_mcontext.gregs[REG_RIP]);
    write_field(fd, "reg_sp", uctx->uc_mcontext.gregs[REG_RSP]);
    write_field(fd, "reg_bp", uctx->uc_mcontext.gregs[REG_RBP]);
#elif defined(__APPLE__) && defined(__x86_64__)
    write_field(fd, "reg_pc", uctx->uc_mcontext->__ss.__rip);
    write_field(fd, "reg_sp", uctx->uc_mcontext->__ss.__rsp);
    write_field(fd, "reg_bp", uctx->uc_mcontext->__ss.__rbp);
#elif defined(__APPLE__) && defined(__aarch64__)
    write_field(fd, "reg_pc", uctx->uc_mcontext->__ss.__pc);
    write_field(fd, "reg_sp", uctx->uc_mcontext->__ss.__sp);
    write_field(fd, "reg_lr", uctx->uc_mcontext->__ss.__lr);
#endif
}
```

### Signal Handler Signature Change

```cpp
// Before:
static void signal_handler(int sig);
sigaction sa;
sa.sa_handler = signal_handler;

// After:
static void signal_handler(int sig, siginfo_t* info, void* ucontext);
struct sigaction sa;
sa.sa_sigaction = signal_handler;
sa.sa_flags = SA_RESETHAND | SA_SIGINFO;
```

### Fault Code Name Mapping

A static lookup table in the signal handler (no heap allocation). Must be async-signal-safe — use a fixed array of structs:

```cpp
struct FaultCodeName {
    int signal;
    int code;
    const char* name;
};

static constexpr FaultCodeName fault_code_names[] = {
    {SIGSEGV, SEGV_MAPERR, "SEGV_MAPERR"},     // Address not mapped
    {SIGSEGV, SEGV_ACCERR, "SEGV_ACCERR"},     // Invalid permissions
    {SIGBUS,  BUS_ADRALN,  "BUS_ADRALN"},      // Invalid address alignment
    {SIGBUS,  BUS_ADRERR,  "BUS_ADRERR"},      // Non-existent physical address
    {SIGFPE,  FPE_INTDIV,  "FPE_INTDIV"},      // Integer divide by zero
    {SIGFPE,  FPE_FLTDIV,  "FPE_FLTDIV"},      // Float divide by zero
    {SIGFPE,  FPE_FLTOVF,  "FPE_FLTOVF"},      // Float overflow
};
```

### Async-Signal-Safety

All new code in the handler uses the same pattern as existing code:
- Pre-allocated static buffers for hex conversion
- `write()` syscall for output
- No heap allocation, no library calls beyond POSIX
- `siginfo_t` and `ucontext_t` are provided by the kernel, already on the stack

### Parser and Reporter Updates

- `crash_handler::read_crash_file()` — parse new `fault_*` and `reg_*` fields into JSON
- `CrashReporter::report_to_json()` — include new fields in CF Worker POST payload
- `CrashReporter::generate_github_url()` — add fault address to issue title/body
- `CrashReporter::report_to_text()` — add fault/register section to human-readable report
- `resolve-backtrace.sh` — accept `reg_pc` and `reg_lr` as additional addresses to resolve

### Updated GitHub Issue Format

```markdown
## Crash Summary

| Field | Value |
|-------|-------|
| **Signal** | 11 (SIGSEGV) |
| **Fault** | SEGV_MAPERR at 0x00000000 |
| **Version** | 0.9.17 |
| **Uptime** | 3174s |
| **Timestamp** | 2026-02-13T08:58:20Z |

## Registers

| Register | Value |
|----------|-------|
| **PC** | 0x00920bac |
| **SP** | 0xbe8ff420 |
| **LR** | 0x0091a3c0 |

## System Info
...
```

Now we'd immediately know: null pointer dereference (fault at 0x0), and the LR gives us the calling function even if `backtrace()` fails to unwind.

---

## Improvement 3: Crash Breadcrumb Ring Buffer

**Impact:** High — provides runtime context showing what happened before the crash
**Effort:** Significant (new subsystem, integration points across codebase)
**Risk:** Low-medium (must be lock-free for signal handler access)

### Problem

A crash after 53 minutes tells us nothing about what the user was doing. Were they on the print status screen? Did the WebSocket just reconnect? Did a panel transition trigger a use-after-free? Log tail at WARN level won't capture this — and we can't ask users to run at DEBUG level permanently (too verbose, too much disk I/O).

### Solution: Structured Breadcrumb Buffer

A fixed-size, always-on ring buffer that records high-level application events regardless of log level. Not a general-purpose logger — a lightweight event trail.

### Design

```cpp
// include/system/crash_breadcrumbs.h

namespace crash_breadcrumbs {

// Event categories — each is a single character tag for compact storage
enum class Category : char {
    Navigation = 'N',    // Panel/overlay push/pop
    WebSocket  = 'W',    // Connect, disconnect, reconnect, error
    Print      = 'P',    // State transitions (idle→printing, etc.)
    API        = 'A',    // Moonraker API call failures
    System     = 'S',    // Init, shutdown, config load, display mode
    Input      = 'I',    // Touch/encoder events (debounced, not every touch)
    Memory     = 'M',    // Allocation failures, pool exhaustion
    Error      = 'E',    // Any caught exception or error condition
};

// Add a breadcrumb. Thread-safe (lock-free). Always-on.
// Format: timestamp_offset + category + message (truncated to fit slot)
void add(Category cat, const char* fmt, ...);

// Dump buffer contents to file descriptor (async-signal-safe)
// Called from signal handler. Returns number of bytes written.
int dump_to_fd(int fd);

// Initialize with app start time (for relative timestamps)
void init(time_t start_time);

} // namespace crash_breadcrumbs
```

### Memory Layout

```
┌─────────────────────────────────────────────────┐
│ Crash Breadcrumb Ring Buffer (64 KB)            │
├─────────────────────────────────────────────────┤
│ Slot 0:  [+0003s] N panel:home                  │  128 bytes per slot
│ Slot 1:  [+0005s] W connected 192.168.1.100     │  = 512 slots in 64KB
│ Slot 2:  [+0005s] A server_info ok              │
│ Slot 3:  [+0008s] P idle→standby                │
│ ...                                              │
│ Slot 511: [+3172s] N overlay:print-status        │
│ ← write_index points here                        │
└─────────────────────────────────────────────────┘
```

**Why 64KB?**
- 512 slots × 128 bytes = 65,536 bytes
- At ~1 event/second (heavy usage), that's ~8.5 minutes of history
- Typical usage is much sparser (1 event every few seconds), giving 20-30+ minutes
- 64KB is negligible on all target platforms (even Pi Zero has 512MB RAM)
- Fixed allocation — no heap pressure during operation

### Slot Format (128 bytes, fixed)

```
Bytes 0-3:    Relative timestamp (uint32_t seconds since app start, wraps at ~136 years)
Byte 4:       Category char ('N', 'W', 'P', etc.)
Byte 5:       Null separator
Bytes 6-126:  Message (null-terminated, truncated with '…' if too long)
Byte 127:     Null terminator (always)
```

### Thread Safety: Lock-Free Design

```cpp
// Static allocation — no heap
static constexpr size_t SLOT_SIZE = 128;
static constexpr size_t SLOT_COUNT = 512;
static constexpr size_t BUFFER_SIZE = SLOT_SIZE * SLOT_COUNT;

static char s_buffer[BUFFER_SIZE];
static std::atomic<uint32_t> s_write_index{0};  // Monotonically increasing
static time_t s_start_time = 0;

void add(Category cat, const char* fmt, ...) {
    uint32_t idx = s_write_index.fetch_add(1, std::memory_order_relaxed);
    uint32_t slot = idx % SLOT_COUNT;
    char* p = &s_buffer[slot * SLOT_SIZE];

    // Write timestamp (relative seconds)
    uint32_t elapsed = static_cast<uint32_t>(time(nullptr) - s_start_time);
    memcpy(p, &elapsed, sizeof(elapsed));

    // Write category
    p[4] = static_cast<char>(cat);
    p[5] = '\0';

    // Write message (truncated to fit)
    va_list args;
    va_start(args, fmt);
    vsnprintf(p + 6, SLOT_SIZE - 7, fmt, args);
    va_end(args);
    p[SLOT_SIZE - 1] = '\0';
}
```

**Thread safety guarantees:**
- `fetch_add` is atomic — two threads never write to the same slot
- Each slot is written by exactly one thread at a time
- The signal handler only *reads* via `dump_to_fd()` — it doesn't call `add()`
- A slot being mid-write when the signal fires = partial data, not corruption. The null terminator at byte 127 is always present (static zero-init), so the dump reads a truncated-but-safe string.

### Signal Handler Integration

```cpp
// In signal_handler(), after writing backtrace:
write_line(fd, "--- breadcrumbs ---");
crash_breadcrumbs::dump_to_fd(fd);
```

`dump_to_fd()` is async-signal-safe:
- Reads `s_write_index` (atomic load)
- Iterates slots in chronological order (oldest first)
- Uses `write()` syscall only
- Pre-formats each line into a small stack buffer

### Crash File Format Addition

```
signal:11
name:SIGSEGV
...
bt:0x920bac
bt:0xf7101290
--- breadcrumbs ---
bc:+0003:N:panel:home
bc:+0005:W:connected 192.168.1.100
bc:+0005:A:server_info ok
bc:+0008:P:idle→standby
bc:+0045:N:overlay:print-status
bc:+0052:I:touch print-select
bc:+3170:W:reconnecting (timeout)
bc:+3172:N:overlay:print-status
bc:+3174:E:nullptr in lv_obj_get_style_prop
```

### Integration Points

Where to add breadcrumbs across the codebase:

| Location | Category | Example Breadcrumb |
|----------|----------|--------------------|
| `NavigationManager::push_panel()` | Navigation | `panel:home` |
| `NavigationManager::push_overlay()` | Navigation | `overlay:print-status` |
| `NavigationManager::go_back()` | Navigation | `back (to panel:home)` |
| `MoonrakerAPI::on_ws_open()` | WebSocket | `connected 192.168.1.100` |
| `MoonrakerAPI::on_ws_close()` | WebSocket | `disconnected code=1006` |
| `MoonrakerAPI::on_ws_error()` | WebSocket | `error: timeout` |
| `PrinterState::set_print_state()` | Print | `idle→printing` |
| `PrinterState::set_print_phase()` | Print | `phase:heating→leveling` |
| `Application::init()` | System | `init display=fbdev 800x480` |
| `SettingsManager::init()` | System | `config loaded` |
| `DisplayManager::set_brightness()` | System | `brightness 80%` |
| `UpdateQueue::post()` (on error/drop) | Error | `queue full, dropping update` |
| API response errors | API | `gcode/script 500: Thermal runaway` |
| `CrashHandler::install()` | System | `crash handler installed` |

**Not breadcrumbed** (too noisy):
- Every touch event (debounce: only log distinct UI interactions)
- Temperature updates (constant stream)
- Individual LVGL draw calls
- Successful API responses (only failures)

### Macro for Convenience

```cpp
// Shorthand macros to reduce call-site verbosity
#define CRUMB_NAV(fmt, ...)   crash_breadcrumbs::add(crash_breadcrumbs::Category::Navigation, fmt, ##__VA_ARGS__)
#define CRUMB_WS(fmt, ...)    crash_breadcrumbs::add(crash_breadcrumbs::Category::WebSocket, fmt, ##__VA_ARGS__)
#define CRUMB_PRINT(fmt, ...) crash_breadcrumbs::add(crash_breadcrumbs::Category::Print, fmt, ##__VA_ARGS__)
#define CRUMB_API(fmt, ...)   crash_breadcrumbs::add(crash_breadcrumbs::Category::API, fmt, ##__VA_ARGS__)
#define CRUMB_SYS(fmt, ...)   crash_breadcrumbs::add(crash_breadcrumbs::Category::System, fmt, ##__VA_ARGS__)
#define CRUMB_ERR(fmt, ...)   crash_breadcrumbs::add(crash_breadcrumbs::Category::Error, fmt, ##__VA_ARGS__)
```

### Performance

- `add()` cost: one atomic increment + `time()` syscall + `vsnprintf()` into stack buffer + `memcpy` to slot. ~200-500ns per call.
- At 1 breadcrumb/second average: ~0.0005% CPU overhead
- Zero heap allocation after init
- No I/O during normal operation (all in-memory)
- No contention (lock-free, each writer gets unique slot)

---

## Improvement 4: Include Log Tail in Auto-Reported GitHub Issues

**Impact:** Medium — immediately useful context even at WARN level
**Effort:** Small
**Risk:** None

### Problem

The CrashReporter already collects the last 50 lines of log via `get_log_tail()`, and includes them in the CF Worker POST and `crash_report.txt`. But the auto-generated GitHub issue (from the telemetry pipeline) only includes signal, version, backtrace, and system info — no log tail.

Even at WARN level, the log tail can contain:
- WebSocket disconnect/reconnect warnings
- API timeout warnings
- Memory warnings
- Startup configuration warnings
- Any `spdlog::warn()` or `spdlog::error()` output

### Solution

Add the log tail to the GitHub issue body template, collapsed in a `<details>` block to keep the issue readable.

### Updated Issue Body Template

```markdown
## Crash Summary

| Field | Value |
|-------|-------|
| **Signal** | 11 (SIGSEGV) |
| **Version** | 0.9.17 |
| **Uptime** | 3174s |
| **Timestamp** | 2026-02-13T08:58:20Z |

## System Info

| Field | Value |
|-------|-------|
| **Platform** | pi32 |
| **RAM** | 7644 MB |
| **CPU** | 4 cores |

## Backtrace

```
0x920bac
0xf7101290
```

<details>
<summary>Log tail (last 50 lines)</summary>

```
[2026-02-13 08:57:45.123] [warn] WebSocket reconnecting (attempt 3)
[2026-02-13 08:57:48.456] [warn] API timeout: printer.objects.query
[2026-02-13 08:58:19.789] [warn] ...
```

</details>

---
*Auto-reported by HelixScreen crash handler*
```

### URL Length Consideration

The GitHub issue URL (for QR code fallback) has a ~2000 char limit. Log tail will NOT fit in the URL — it's only included when reporting via the CF Worker HTTP POST, which creates the issue server-side with no URL length constraint. The QR fallback URL remains minimal (signal + version + backtrace only).

### Implementation

Modify the telemetry worker's issue creation template to include `log_tail` from the crash report JSON. The data is already being sent — the worker just isn't including it in the issue body.

---

## Improvement 5: On-Device Symbol Map for Pre-Resolved Backtraces

**Impact:** Medium — makes crash reports immediately readable without manual resolution
**Effort:** Moderate (build pipeline + packaging + crash file writer changes)
**Risk:** Low

### Problem

Raw addresses like `0x920bac` require offline resolution via `resolve-backtrace.sh`, which downloads symbols from R2. This works for us as developers but:
- The auto-reported GitHub issue shows raw addresses (useless at a glance)
- The CF Worker could resolve symbols, but it would need to know the binary version and platform, download the right `.sym`, and parse it — complexity we don't want in a worker
- Users reading `crash_report.txt` on their device see only hex addresses

### Solution

Ship a compressed, function-boundaries-only symbol map alongside the binary. The crash reporter (on next startup, NOT in the signal handler) resolves addresses before sending/displaying.

### Symbol Map Size Analysis

Current full symbol map for Pi32 v0.9.17:
- **Full map:** 6.1 MB (30,878 symbols — includes objects, weak refs, etc.)
- **Function symbols only (T/t):** 18,694 entries, 4.0 MB raw text
- **Compressed (gzip -9):** Estimated ~350-450 KB (symbol names are highly compressible — lots of shared prefixes like `lv_`, `spdlog::`, `helix::`)
- **Our functions only** (excluding LVGL, libhv, spdlog): Estimated ~3,000-4,000 entries, ~60-80 KB compressed

### Recommended Approach: Full Function Map, Compressed

Ship the full function symbol map (~400 KB compressed). Reasons:
- Crashes often happen in library code called from our code — resolving LVGL/libhv frames is valuable
- 400 KB is negligible (the Pi32 binary package is 49 MB)
- Simpler build pipeline (no filtering step)

### File Location and Format

```
~/helixscreen/config/symbols.gz       # Compressed symbol map
```

Format (same as `nm -nC` output, one line per function):
```
00010abc T _start
00010b00 T main
00010c40 T helix::Application::init()
...
```

### Resolution Flow

```
Signal handler writes crash.txt (raw addresses)
         ↓
Next startup: CrashReporter.collect_report()
         ↓
If symbols.gz exists:
    Decompress to memory
    Binary search each backtrace address
    Store resolved names in CrashReport
         ↓
Report includes both raw + resolved:
    bt:0x920bac → helix::PrinterState::update_temps()
```

### Build Pipeline Integration

```makefile
# In release target, after generating .sym:
symbols-compressed: symbols
	grep -E '^[0-9a-f]+ [Tt] ' $(TARGET).sym | gzip -9 > $(TARGET).symbols.gz
	@echo "Compressed symbols: $$(du -h $(TARGET).symbols.gz | cut -f1)"
```

Include `symbols.gz` in release tarballs alongside the binary.

### Crash Report with Resolved Symbols

```markdown
## Backtrace

| # | Address | Function |
|---|---------|----------|
| 0 | 0x920bac | helix::PrinterState::update_temps()+0x4c |
| 1 | 0x91a3c0 | helix::MoonrakerAPI::on_message()+0x1a0 |
| 2 | 0xf7101290 | ??? (outside binary) |
```

Addresses outside the binary's address range (shared libraries) show `???` — they can't be resolved without the library's symbols, which is fine. Our code is what matters.

### Signal Handler Stays Dumb

The signal handler does NOT read symbols.gz. It continues to write raw addresses only. Symbol resolution happens in CrashReporter on next startup, where we have full access to the heap, filesystem, and zlib.

---

## Implementation Order

| Phase | What | Files Changed | Depends On |
|-------|------|---------------|------------|
| **1** | Frame pointers in release | `Makefile` | Nothing |
| **2** | Fault address + registers + fault code | `crash_handler.cpp`, `crash_handler.h`, `crash_reporter.cpp`, parser, tests | Nothing |
| **3** | Log tail in GitHub issues | Telemetry CF Worker template | Nothing |
| **4** | Breadcrumb ring buffer | New files + integration across ~10-15 source files | Nothing (but benefits from Phase 2 testing) |
| **5** | On-device symbol map | `Makefile`, `crash_reporter.cpp`, release packaging | Nothing |

Phases 1-3 are independent and can be done in any order or in parallel. Phase 4 is the largest effort. Phase 5 is independent but less urgent (we can always resolve offline).

**Recommended execution:**
1. Phase 1 first (trivial, immediate impact on next crash)
2. Phase 2 + Phase 3 together (moderate effort, big diagnostic improvement)
3. Phase 4 (breadcrumbs — biggest effort, biggest long-term payoff)
4. Phase 5 (symbol map — nice to have, can defer)

---

## Testing Strategy

### Phase 1 (Frame Pointers)
- Build Pi32 binary, verify `backtrace()` returns full stack in a test signal handler
- Compare frame count with and without the flag (should go from ~2 to 15+ frames)
- Benchmark: run test suite with and without, verify no measurable regression

### Phase 2 (Fault Address + Registers)
- Existing test infrastructure: `test_crash_handler.cpp` has mock crash file tests
- Add: parse tests for new fields (fault_addr, fault_code, fault_code_name, reg_*)
- Add: write_mock_crash_file() includes new fields
- Add: platform-specific register capture test (trigger SIGSEGV in test, verify registers captured)
- Add: fault code name mapping tests (all known codes)
- Verify: crash file parser gracefully handles old-format files (missing new fields = null/empty, not parse failure)

### Phase 3 (Log Tail in Issues)
- Update CF Worker tests to verify issue body includes log tail section
- Test URL generation still respects 2000 char limit (log tail excluded from QR URL)

### Phase 4 (Breadcrumbs)
- Unit test ring buffer: single-threaded add + dump
- Thread-safety test: N threads adding concurrently, verify no corruption
- Signal-safety test: dump_to_fd() during active writes (stress test)
- Integration test: mock crash file with breadcrumb section, verify parser handles it
- Capacity test: overflow buffer (>512 entries), verify oldest entries are overwritten
- Empty buffer test: dump_to_fd() with zero entries
- Format test: verify breadcrumb line format matches `bc:+NNNN:C:message` pattern

### Phase 5 (Symbol Map)
- Build pipeline: verify symbols.gz is generated and included in release tarball
- Resolution: known address → known function name
- Out-of-range address → `???`
- Missing symbols.gz → graceful fallback (raw addresses, no error)
- Corrupt symbols.gz → graceful fallback

---

## Backward Compatibility

All changes are additive to the crash file format. The parser MUST handle:
- **Old format** (v0.9.17 and earlier): No fault_*, reg_*, or breadcrumb fields → treated as absent, not an error
- **New format**: All new fields present
- **Mixed**: Some new fields present, others missing (e.g., platform without register support)

The CrashReporter, telemetry worker, and `resolve-backtrace.sh` must all tolerate missing fields gracefully.

---

## Open Questions

1. **Breadcrumb buffer size:** 64 KB (512 slots) is a starting point. Should we make this configurable, or is fixed-size simpler and sufficient?
2. **Breadcrumb verbosity on AD5M:** The AD5M (x86_64, plenty of RAM) could use a larger buffer. Worth having platform-specific sizes?
3. **Symbol map updates:** When the user updates HelixScreen, the symbol map must match the binary version. The update process already replaces the binary — should symbols.gz live next to the binary or in a version-tagged path?
4. **Memory-mapped symbol file:** Instead of decompressing to heap, could mmap the symbol file for zero-copy lookup? Only matters if the file is large.
5. **Core dumps:** Should we also enable core dumps where possible (`ulimit -c unlimited` + configured path)? Core dumps give complete state but are large (50-100MB+) and hard to retrieve from field devices.
