# Crash Archaeologist 2.0: Person 3 Integration & Architecture Handoff Guide

> **Target Audience:** Team Members (Member 1: Systems & Ring Buffer Lead; Member 2: Instrumentation & Hooking Specialist) and their AI coding assistants.
> **Author:** Person 3 (Causal Graph, Root-Cause Reconstruction, Reporting, Workloads & Benchmarking Lead)
> **Status:** Milestone Completed & Standalone-Verified | Ready for Integration

---

## 1. System Overview & Ownership Matrix

**Crash Archaeologist 2.0** is a low-overhead, in-memory flight recorder for C/C++ Linux applications. It continuously captures operating system and runtime events into a pre-allocated circular ring buffer, freezes state immediately upon intercepting fatal POSIX signals (`SIGSEGV`, `SIGBUS`, `SIGPIPE`, `SIGABRT`), and performs post-mortem causal analysis to diagnose failure root causes and output structured reports.

### 1.1 Division of Labor & Ownership

```
+---------------------------------------------------------------------------------------------------+
|                                      TEAM OWNERSHIP BREAKDOWN                                     |
+------------------------------------+------------------------------------+-------------------------+
| Member 1 (Preservation & CLI)      | Member 2 (Interception Hooks)      | Member 3 (Person 3)     |
+------------------------------------+------------------------------------+-------------------------+
| - include/event.h & buffer.h       | - src/collector/mem_hook.c         | - src/analyzer/         |
| - src/buffer/ring_buffer.c         | - src/collector/thread_hook.c      |   * correlator.c        |
| - src/collector/signal_handler.c   | - src/collector/syscall_hook.c     |   * causal_graph.c      |
| - src/collector/state_collector.c  | - src/collector/ipc_hook.c         |   * root_cause.c        |
| - src/main.c                       |                                    |   * analyzer_internal.h |
|                                    |                                    | - src/reporter/         |
|                                    |                                    |   * report_generator.c  |
|                                    |                                    | - tests/unit/           |
|                                    |                                    | - tests/workloads/      |
|                                    |                                    | - scripts/benchmark_... |
+------------------------------------+------------------------------------+-------------------------+
```

### 1.2 The Execution & Analysis Pipeline

```
[Target Program Execution]
            │
            ▼
[Member 2 Hooks: mem_hook, thread_hook, syscall_hook, ipc_hook]
            │ (Emits zero-allocation crash_event_t into memory)
            ▼
[Member 1 Flight Recorder: ring_buffer.c (Capacity N=8192)]
            │ (Continuous O(1) overwrites in virtual memory)
            ▼
      [Crash Signal Encountered: SIGSEGV, SIGBUS, SIGPIPE, SIGABRT]
            │
            ▼
[Member 1 Signal Handler: signal_handler.c]
            │ 1. Freezes ring buffer: ring_buffer_freeze(&g_ring_buffer)
            │ 2. Logs terminal EVENT_SIG_RECEIVED into buffer or context
            ▼
[Person 3 Pipeline: analyze_crash(&g_ring_buffer, &result)]
            │
            ├─► 1. correlator.c: Extracts snapshot & sorts by sequence_id / timestamp_ns
            │
            ├─► 2. causal_graph.c: Evaluates Wait-For-Graph & detects directed lock cycles
            │
            ├─► 3. root_cause.c: Matches failure heuristics (UAF, Segfault, Deadlock, etc.)
            │      and bounds causal chain to 16 events
            ▼
[Person 3 Reporter: generate_crash_report(&result, output_file)]
            │
            ├─► Human-readable plain text (.txt / stdout)
            └─► Machine-parseable RFC-8259 JSON (.json)
```

---

## 2. Person 3 Implemented Components

All files below are located in the repository, compile with zero warnings under `-Wall -Wextra -O2`, and are fully unit-tested:

### 2.1 `src/analyzer/correlator.c`
- **Purpose:** Extracts events from `ring_buffer_t` via public accessor APIs (`ring_buffer_get_count()` and `ring_buffer_get_at()`), sorts them chronologically, and builds causal chains.
- **Key Logic:**
  - **Zero Assumptions on Buffer Indexing:** Because circular buffer implementations may index newest-first, oldest-first, or head-relative, `correlator_extract_timeline()` explicitly runs `qsort()` ordered by `sequence_id` (primary) and `timestamp_ns` (secondary).
  - **Signal Locator:** `correlator_find_terminal_signal()` scans backwards from newest events for `EVENT_SIG_RECEIVED` or `category == EVENT_CAT_SIGNAL`.
  - **Causal Chain Construction:** `correlator_build_causal_chain()` filters events matching the diagnosed fault address range, faulting TID, or lock address, strictly bounding the output chain length to `max_chain` (16 entries).

### 2.2 `src/analyzer/causal_graph.c`
- **Purpose:** Tracks mutex lock ownership and thread wait states, constructing a directed Wait-For-Graph (WFG) to detect circular deadlocks.
- **Key Logic:**
  - **Explicit Mutex Filter:** Strictly processes `EVENT_SYNC_MUTEX_LOCK` and `EVENT_SYNC_MUTEX_UNLOCK`.
  - **Wait-For State Machine:**
    - When thread $T$ calls lock on unowned lock $M$: $T$ acquires $M$.
    - When thread $T$ calls lock on lock $M$ already held by $T_{owner} \ne T$: Thread $T$ is marked **blocked waiting** on $T_{owner}$ for lock $M$ ($T \xrightarrow{M} T_{owner}$).
    - When thread $T$ unlocks $M$: Lock $M$ becomes unowned, and any wait-for edge on $M$ is immediately cleared.
  - **DFS Cycle Detection:** Path-based DFS traversal reconstructs directed dependency cycles ($T_1 \xrightarrow{M_1} T_2 \dots \xrightarrow{M_k} T_1$).
  - **Bounded Safety:** Thread table (`MAX_GRAPH_THREADS = 64`), lock table (`MAX_GRAPH_LOCKS = 128`), and cycle depth (`MAX_CYCLE_DEPTH = 16`). Oversized cycles are detected and safely capped without array overruns.

### 2.3 `src/analyzer/root_cause.c`
- **Purpose:** Implements public entry point `analyze_crash(const ring_buffer_t *rb, crash_analysis_result_t *out_result)`.
- **Diagnostic Precedence Architecture:**
  - `memset(out_result, 0, sizeof(*out_result))` guarantees all fields are initialized on every return path.
  - If a fatal crash signal is detected: evaluates signal-dependent heuristics in priority order:
    1. **Use-After-Free (`CAUSE_USE_AFTER_FREE`):** Fault address matches an address block previously freed via `EVENT_MEM_FREE` without intervening reallocation. (Confidence: 95%).
    2. **Null / Low-Address Segfault (`CAUSE_SEGFAULT`):** Fault address $< 0x1000$. (Confidence: 98%).
    3. **IPC Broken Pipe (`CAUSE_IPC_FAILURE`):** Fatal `SIGPIPE` (signal 13) or pipe write error. (Confidence: 90%).
    4. **Resource Exhaustion (`CAUSE_RESOURCE_EXHAUSTION`):** Syscall `open` failures (`EMFILE`). (Confidence: 92%).
    5. **Unmapped High-Address Segfault (`CAUSE_SEGFAULT`):** Memory fault without prior free record. (Confidence: 70%).
  - If no fatal signal exists (e.g. hang or unhandled freeze):
    1. **Circular Deadlock (`CAUSE_DEADLOCK`):** Evaluates `causal_graph_detect_deadlock()`. (Confidence: 95%).
    2. **Resource Exhaustion (`CAUSE_RESOURCE_EXHAUSTION`):** $>100$ open FDs without close. (Confidence: 92%).
    3. **Memory Leak (`CAUSE_MEMORY_LEAK`):** $\ge 10$ allocations with 0 frees. (Confidence: 85%).
  - Inconclusive Fallback: `CAUSE_UNKNOWN` (Confidence: 5%).

### 2.4 `src/reporter/report_generator.c`
- **Purpose:** Implements public entry point `generate_crash_report(const crash_analysis_result_t *result, const char *output_file)`.
- **Format Determination:**
  - `output_file == NULL`: Prints human-readable banner report to `stdout`.
  - `output_file` ending with `.json` (case-insensitive): Emits RFC-8259-compliant JSON with string escaping (`\"`, `\\`, control chars).
  - Otherwise: Writes plain-text banner report to the target file.

---

## 3. Shared Header Declarations & Expected Semantics

The analyzer relies exclusively on the public types declared in `include/`:

### 3.1 `include/event.h`
```c
typedef enum {
    EVENT_CAT_PROCESS_THREAD = 1,
    EVENT_CAT_SYSCALL        = 2,
    EVENT_CAT_MEMORY         = 3,
    EVENT_CAT_IO             = 4,
    EVENT_CAT_IPC            = 5,
    EVENT_CAT_SIGNAL         = 6
} event_category_t;

typedef enum {
    EVENT_THREAD_CREATE = 100, EVENT_THREAD_EXIT, EVENT_THREAD_SWITCH,
    EVENT_MEM_ALLOC = 200, EVENT_MEM_FREE, EVENT_MEM_REALLOC, EVENT_MEM_MMAP, EVENT_MEM_MUNMAP,
    EVENT_SYS_OPEN = 300, EVENT_SYS_CLOSE, EVENT_SYS_READ, EVENT_SYS_WRITE,
    EVENT_IPC_PIPE_WRITE = 400, EVENT_IPC_PIPE_READ, EVENT_IPC_SOCKET_SEND, EVENT_IPC_SOCKET_RECV,
    EVENT_SYNC_MUTEX_LOCK, EVENT_SYNC_MUTEX_UNLOCK,
    EVENT_SIG_RECEIVED = 500
} event_type_t;

typedef struct {
    uint64_t sequence_id;       /* Global monotonic incrementing counter */
    uint64_t timestamp_ns;      /* Nanoseconds timestamp */
    uint32_t pid;               /* Process ID */
    uint32_t tid;               /* Thread ID */
    event_category_t category;  /* Event Category */
    event_type_t type;          /* Event Type */
    uintptr_t addr;             /* Memory address, pointer, or lock address */
    size_t size;                /* Allocation size or byte count */
    int32_t fd_or_status;       /* Signal number, file descriptor, or return code */
    char label[32];             /* Descriptive symbol/string */
} crash_event_t;
```

### 3.2 `include/buffer.h`
```c
typedef struct {
    crash_event_t *events;
    size_t capacity;
    size_t head;
    size_t count;
    uint64_t next_seq;
    bool frozen;
} ring_buffer_t;

size_t ring_buffer_get_count(const ring_buffer_t *rb);
int ring_buffer_get_at(const ring_buffer_t *rb, size_t index, crash_event_t *out_ev);
```

### 3.3 `include/engine.h`
```c
typedef struct {
    root_cause_type_t probable_cause;
    float confidence_level;         /* Scale: 0.0 to 1.0 */
    uint32_t faulting_tid;
    uintptr_t faulting_addr;
    char explanation[256];
    crash_event_t causal_chain[16]; /* Fixed-size bounded causal sequence */
    size_t causal_chain_len;
} crash_analysis_result_t;

int analyze_crash(const ring_buffer_t *rb, crash_analysis_result_t *out_result);
int generate_crash_report(const crash_analysis_result_t *result, const char *output_file);
```

---

## 4. Heuristic Diagnostic Support & Upstream Requirements Matrix

| Diagnosis (`root_cause_type_t`) | Required Evidence / Events in Ring Buffer | Current Detection Logic | Required Upstream Instrumentation | Limitations & Confidence Notes |
| :--- | :--- | :--- | :--- | :--- |
| **`CAUSE_SEGFAULT`** | Terminal `EVENT_SIG_RECEIVED` with signal `SIGSEGV` or `SIGBUS`. | `ev->addr < 0x1000` (NULL dereference) or unmapped address with no prior free record. | Member 1 `signal_handler.c` extracts `info->si_addr` and logs `EVENT_SIG_RECEIVED`. | Confidence: 98% for low/null page; 70% for unmapped high address without allocator context. |
| **`CAUSE_USE_AFTER_FREE`** | `EVENT_MEM_ALLOC`, `EVENT_MEM_FREE`, then fatal `EVENT_SIG_RECEIVED` on matching address. | Crash address falls in range `[free_ev.addr, free_ev.addr + size]` with no intervening realloc. | Member 2 `mem_hook.c` records `malloc()` and `free()`; Member 1 logs crash signal. | Confidence: 95%. Requires that free event was logged within ring buffer history window. |
| **`CAUSE_DEADLOCK`** | `EVENT_SYNC_MUTEX_LOCK` records exhibiting circular wait dependencies across active threads. | Wait-For-Graph cycle detection in `causal_graph.c`. | Member 2 `thread_hook.c` intercepts `pthread_mutex_lock()` and `unlock()`. | Confidence: 95%. Deadlock detection evaluates when no fatal crash signal occurred. |
| **`CAUSE_RESOURCE_EXHAUSTION`** | `EVENT_SYS_OPEN` returning negative status (`fd_or_status < 0` / `EMFILE`), or $>100$ unclosed FDs. | Counts net open descriptors and checks for negative syscall return codes. | Member 2 `syscall_hook.c` logs `open()` attempts and return codes / `errno`. | Confidence: 92%. |
| **`CAUSE_IPC_FAILURE`** | `EVENT_SIG_RECEIVED` with `SIGPIPE` (13), or `EVENT_IPC_PIPE_WRITE` returning error. | Traps `SIGPIPE` or write failures on disconnected pipe/socket. | Member 2 `ipc_hook.c` or Member 1 signal handler trapping `SIGPIPE`. | Confidence: 90%. Windows CRT returns `EINVAL` rather than delivering `SIGPIPE`. |
| **`CAUSE_MEMORY_LEAK`** | Monotonic accumulation of `EVENT_MEM_ALLOC` ($\ge 10$) with zero `EVENT_MEM_FREE`. | Computes net heap delta and deallocation ratio across preserved window. | Member 2 `mem_hook.c` logs memory allocations. | Confidence: 85%. Heuristic detection; cannot observe allocations that occurred before buffer start. |
| **`CAUSE_DATA_RACE`** | **UNSUPPORTED BY CURRENT SCHEMA** | None. Raw CPU memory loads and stores cannot be observed by dynamic library hooks. | Requires compiler-assisted watchpoints (TSan) or kernel hardware debug registers. | **Honest Limitation:** Standard library flight recording cannot observe instruction-level data races. |

---

## 5. Verification & Test Suite Summary

### 5.1 Unit Tests (`tests/unit/`)
Executed via `mingw32-make test` (Total: **19 / 19 tests passing**):

- **`test_analyzer.c` (6 tests):**
  1. `test_empty_history`: Graceful handling of zero events (`CAUSE_UNKNOWN`, conf 0.0).
  2. `test_low_address_segfault`: Reconstructs NULL pointer dereference (`CAUSE_SEGFAULT`, conf 0.98).
  3. `test_use_after_free`: Reconstructs UAF from matching free event (`CAUSE_USE_AFTER_FREE`, conf 0.95).
  4. `test_inconclusive_history`: Distinguishes benign syscalls from failure patterns.
  5. `test_report_generation`: Validates plain-text and JSON file output and stdout emission.
  6. `test_null_safety`: Asserts NULL ring buffer or NULL result pointers never crash.
- **`test_causal_graph.c` (13 tests):**
  1. `test_empty_event_history`: Empty history safety.
  2. `test_two_thread_abba_cycle`: 2-thread AB-BA deadlock cycle detection.
  3. `test_non_cyclic_lock_sequence`: Sequential locking does not trigger false deadlock.
  4. `test_incomplete_lock_history`: Unlocks without locks or disjoint locks handle gracefully.
  5. `test_multi_thread_multi_lock_cycle`: 3-thread circular deadlock ($T_1 \to T_2 \to T_3 \to T_1$).
  6. `test_end_to_end_analyzer_deadlock`: Full `analyze_crash()` integration with Wait-For-Graph.
  7. `test_lock_released_before_next_acquisition`: Lock reuse after release creates no edge.
  8. `test_thread_waiting_then_unlocked`: Resolved contention clears wait edges.
  9. `test_unlock_without_acquisition`: Truncated histories starting with unlocks fail safely.
  10. `test_cycle_near_max_depth`: Depth-16 circular deadlock cycle.
  11. `test_capacity_overflow_safe`: 70 threads (> 64 limit) degrades safely without buffer overflow.
  12. `test_truncated_incomplete_cycle`: Truncated cycle lacks evidence, avoids false positives.
  13. `test_simultaneous_deadlock_and_competing_signal`: Fatal SIGSEGV takes precedence over background deadlock.

### 5.2 Synthetic Workloads (`tests/workloads/`)
Compiled via `mingw32-make workloads`:
- `test_segfault.c`: Traps SIGSEGV via null pointer dereference.
- `test_uaf.c`: Writes to freed heap block (documents allocator page-mapping behavior).
- `test_memleak.c`: Bounded 20 KB leak (20 blocks x 1024 bytes) without exhaustion.
- `test_race.c`: Concurrent counter increments (documents non-deterministic lost update UB).
- `test_deadlock.c`: AB-BA deadlock with detached **2-second safety watchdog** exiting with status 124.
- `test_resource_exhaustion.c`: Opens FDs up to limit and **explicitly closes all 1024 descriptors** before exit.
- `test_ipc_failure.c`: Writes to closed pipe (documents Windows CRT `EINVAL` vs POSIX `SIGPIPE`).

### 5.3 Standalone Analyzer Benchmark (`scripts/benchmark_overhead.py`)
- **Measured Throughput:** Over **5,800 full post-crash reconstructions per second** on an 8,192-event buffer (average latency: **0.17 ms** per analysis).
- **Overhead Status:** Runtime recording overhead (<5% target) is explicitly **pending** Member 1's ring buffer and Member 2's runtime interposition hooks.

---

## 6. Upstream Integration Checklist for Members 1 and 2

To achieve complete end-to-end flight recording and analysis, Members 1 and 2 must fulfill the following contract:

### Member 1 (Buffer & Signal Preservation):
- [ ] **Lock-Free / Atomic Circular Overwrites:** Ensure `ring_buffer_record()` writes monotonically using `head = (head + 1) % capacity` without heap allocations.
- [ ] **Signal-Safe Freezing:** `ring_buffer_freeze(&g_ring_buffer)` must atomically set `frozen = true` in `signal_handler.c` before inspection.
- [ ] **Terminal Signal Event:** The signal handler must log `EVENT_SIG_RECEIVED` as the final event with `ev->addr = (uintptr_t)info->si_addr`, `ev->fd_or_status = signo`, and `ev->tid = gettid()`.
- [ ] **Snapshot Iterator API:** Implement `ring_buffer_get_count()` and `ring_buffer_get_at()` adhering to `include/buffer.h`.

### Member 2 (Interception Hooks):
- [ ] **Memory Hooks (`mem_hook.c`):** Log `EVENT_MEM_ALLOC` on `malloc`/`calloc`/`mmap` and `EVENT_MEM_FREE` on `free`/`munmap` with accurate `addr` and `size`.
- [ ] **Thread & Mutex Hooks (`thread_hook.c`):** Log `EVENT_SYNC_MUTEX_LOCK` (upon entering lock acquisition) and `EVENT_SYNC_MUTEX_UNLOCK` (upon releasing lock) with `ev->addr = (uintptr_t)mutex`.
- [ ] **Syscall Hooks (`syscall_hook.c`):** Log `EVENT_SYS_OPEN` and `EVENT_SYS_CLOSE` with return code or `errno` in `fd_or_status`.
- [ ] **IPC Hooks (`ipc_hook.c`):** Log pipe and socket writes and reads.

---

## 7. Step-by-Step Integration Verification Commands

Once Members 1 and 2 push their implementations, run the following verification pipeline:

```bash
# 1. Compile the complete project and unit test suites
make clean
make all
make test
make workloads

# 2. Run the standalone analyzer benchmark
python scripts/benchmark_overhead.py

# 3. Verify report generation
python -m json.tool reports/test_crash_report.json

# 4. Execute target workload under Crash Archaeologist
./bin/crash_archaeologist ./bin/test_uaf
```

---

## 8. AI Prompt for Upstream Team Members

*Copy and paste the prompt below into an AI assistant when integrating Member 1 or Member 2 components:*

```text
You are an AI coding assistant working on the GitHub repository AnZul77/OS_PROJECT for Crash Archaeologist 2.0.
Before modifying any files:
1. Read docs/PERSON_3_HANDOFF.md, include/event.h, include/buffer.h, and include/engine.h to understand the architecture, data structures, and Person 3's analyzer requirements.
2. Run `git status` and inspect current branch changes.
3. Preserve all existing code in src/analyzer/, src/reporter/, tests/, and include/ without modifying Person 3's causal graph or public header declarations.
4. Implement your designated components (Member 1: ring_buffer.c, signal_handler.c, main.c; OR Member 2: mem_hook.c, thread_hook.c, syscall_hook.c, ipc_hook.c).
5. Ensure your hooks record crash_event_t structures conforming exactly to Section 3 of docs/PERSON_3_HANDOFF.md (monotonic sequence_id, nanosecond timestamps, thread IDs, address ranges, and correct event categories/types).
6. Verify your implementation by running:
     mingw32-make test
     mingw32-make workloads
7. Provide empirical test output for all claims and do not claim unsupported features (e.g. data race detection).
```
