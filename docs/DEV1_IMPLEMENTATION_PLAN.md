# Dev 1 Implementation Plan: Systems Architecture & In-Memory Flight Recorder
**Author:** Anshul Gupta (24BYB0149)  
**Role:** Systems Lead & Flight Recorder Architect (Dev 1)  
**Course:** BCSE303P — Operating Systems Lab  
**Supervisor:** Dr. Porkodi S (Assistant Professor Senior Grade – 1)  
**Target Milestone:** Sprint 1 & Review 2 Baseline  

---

## 1. Role Scope & Mission Statement

As **Dev 1**, my mission is to build the core foundation of **Crash Archaeologist 2.0**:
1. **The In-Memory Flight Recorder:** A deterministic, lock-free/low-contention circular event ring buffer with zero-allocation steady-state recording.
2. **The Signal Trap & Freeze Subsystem:** Signal handlers (`SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`) that instantly freeze history and prevent post-crash data clobbering.
3. **State Collector:** Snapshotting the process memory mappings (`/proc/self/maps`), register/fault info (`si_addr`), and thread metadata.
4. **Master Orchestrator (`main.c`):** The top-level harness providing the command-line interface, subsystem initialization, and execution pipeline.
5. **Dev 2 & Dev 3 Contracts:** Providing the exact interfaces, header files, and thread-safe APIs that Adharshini (Dev 2 - Interceptors) and Rohan (Dev 3 - Causal Engine) rely upon.

---

## 2. API Contract with Dev 2 and Dev 3

```
+---------------------------------------------------------------------------------+
|                                 DEV 1 SUBSYSTEMS                                |
|                                                                                 |
|   +-----------------------+     +-----------------------+     +---------------+ |
|   |   include/event.h     |     |   include/buffer.h    |     |  collector.h  | |
|   | (Standard Event Type) |     | (Circular Ring Buffer)|     | (State & Sig) | |
|   +-----------------------+     +-----------------------+     +---------------+ |
+---------------------------------------------------------------------------------+
               |                                               |
       Calls `ring_buffer_record()`                     Reads frozen buffer via
               |                                        `ring_buffer_get_at()`
               v                                               v
+-------------------------------+               +-------------------------------+
|     DEV 2: Adharshini         |               |       DEV 3: Rohan            |
|  - Memory Hooks (malloc/free) |               |  - Backwards Correlator       |
|  - Syscall Hooks (open/read)  |               |  - Causal Graph & Heuristics  |
|  - Thread & Mutex Hooks       |               |  - Plain Text & JSON Reporter |
+-------------------------------+               +-------------------------------+
```

### 2.1 Exported to Dev 2 (Adharshini - Event Collection)
Dev 2 needs high-speed, thread-safe primitives to record events inside intercepted wrappers:
- `void ring_buffer_record(ring_buffer_t *rb, const crash_event_t *ev)`: Thread-safe, non-blocking event submission.
- `uint64_t get_timestamp_ns(void)`: Monotonic clock source with nanosecond resolution.
- `uint32_t get_current_tid(void)`: Fast thread ID resolution.

### 2.2 Exported to Dev 3 (Rohan - Analysis & Reporting)
Dev 3 analyzes the buffer *after* it has been frozen by Dev 1's signal handler:
- `size_t ring_buffer_get_count(const ring_buffer_t *rb)`: Number of valid preserved events.
- `int ring_buffer_get_at(const ring_buffer_t *rb, size_t index, crash_event_t *out_ev)`: Sequential access from oldest ($0$) to newest ($N-1$).
- `const process_state_t* state_collector_get_snapshot(void)`: Metadata containing crash signal, fault address, thread context, and memory map slices.

---

## 3. Component Breakdown & Technical Specifications

### Module 1.1: Event Model (`include/event.h`)
- **Structure Size & Cache Line Alignment:** Each `crash_event_t` is padded to 64 bytes (1 cache line) to prevent false sharing between concurrent threads writing to adjacent slots.
- **Fields:**
  - `uint64_t sequence_id`: Globally monotonic sequence identifier.
  - `uint64_t timestamp_ns`: Monotonic timestamp (`CLOCK_MONOTONIC`).
  - `uint32_t pid`: Process ID.
  - `uint32_t tid`: Thread ID (`gettid()` on Linux, `GetCurrentThreadId()` on Windows).
  - `event_category_t category`: Major category (Process/Thread, Syscall, Memory, IO, IPC, Signal).
  - `event_type_t type`: Granular event code (e.g., `EVENT_MEM_FREE`, `EVENT_SYS_OPEN`).
  - `uintptr_t addr`: Target pointer or address (e.g., buffer pointer, fault address).
  - `size_t size`: Bytes allocated/transferred.
  - `int32_t fd_or_status`: File descriptor, error code, or signal code.
  - `char label[32]`: Compact null-terminated symbol/tag.

### Module 1.2: Circular Ring Buffer (`include/buffer.h`, `src/buffer/ring_buffer.c`)
- **Capacity:** Default 8,192 slots (512 KB memory footprint), dynamically configurable up to 65,536 slots via CLI.
- **Allocation:** Pre-allocated single memory chunk via `calloc()` or `mmap()` during `ring_buffer_init()`.
- **Concurrency & Overwrite Mechanics:**
  - Uses atomic fetch-and-add on `next_seq` (`atomic_fetch_add` or `__sync_fetch_and_add`).
  - Slot indexing: `slot_idx = seq % capacity`.
  - Overwrites oldest entries seamlessly once buffer is full.
- **Freeze Semantics:**
  - `atomic_bool frozen`: Set to `true` by signal handler.
  - `ring_buffer_record()` performs an atomic check; if `frozen == true`, it exits immediately with zero side-effects.

### Module 1.3: Signal Trapping Subsystem (`include/signals.h`, `src/collector/signal_handler.c`)
- **POSIX Signal Interception:**
  - Configures `sigaction` with `SA_SIGINFO | SA_NODEFER | SA_RESETHAND`.
  - Handled signals: `SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGPIPE`.
  - Signal-safe alternate stack (`sigaltstack`) to ensure stack-overflow segfaults do not crash the handler.
- **Handler Execution Flow:**
  1. Flip `rb->frozen = true` (atomic memory barrier).
  2. Extract `info->si_signo`, `info->si_code`, `info->si_addr`.
  3. Emit an `EVENT_SIG_RECEIVED` crash marker event directly to the buffer's designated freeze slot.
  4. Invoke `state_collector_snapshot()`.
  5. Hand off control to the Analysis Engine (`analyze_crash()` from Dev 3).
  6. Generate crash report and exit gracefully or re-raise signal.

### Module 1.4: State Collector (`include/collector.h`, `src/collector/state_collector.c`)
- Captures post-mortem process state:
  - Parses `/proc/self/maps` on Linux to catalog mapped virtual memory regions (base address, permissions `rwxp`, mapping path).
  - Captures number of open file descriptors by reading `/proc/self/fd/`.
  - Stores captured state into `process_state_t` for Dev 3's correlator.

### Module 1.5: Main Harness & CLI (`src/main.c`)
- Command line arguments:
  - `--buffer-size <N>`: Customizes event capacity (default: 8192).
  - `--run-workload <name>`: Runs one of the 7 synthetic workloads directly through the harness.
  - `--output-report <path>`: Specifies destination path for generated report.
  - `--unit-tests`: Runs internal self-tests for the ring buffer and signal system.

---

## 4. Low-Overhead & Signal-Safety Engineering Rules

| Constraint | Dev 1 Rule & Implementation Mechanism |
| :--- | :--- |
| **Zero Heap Allocations on Tracing Path** | No `malloc()`, `calloc()`, or `realloc()` allowed inside `ring_buffer_record()`. All buffers are fixed-size pre-allocated at startup. |
| **Async-Signal-Safety** | Handlers cannot use non-reentrant glibc functions (`printf`, `malloc`). String and diagnostic printing uses direct `write()` to `STDERR_FILENO`. |
| **Low Lock Contention** | Event reservation uses atomic sequence increment (`fetch_and_add`). Readers only inspect after the buffer is frozen. |
| **Memory Barrier Integrity** | Atomic freeze flag utilizes `memory_order_seq_cst` or explicit memory fences to ensure no out-of-order event visibility. |

---

## 5. Step-by-Step Implementation Roadmap

```
Week 1 (Days 1–3)   : Interface Polish & Buffer Structures
                       - Finalize include/event.h & include/buffer.h
                       - Define process_state_t & collector.h

Week 1 (Days 4–7)   : In-Memory Ring Buffer Core
                       - Implement src/buffer/ring_buffer.c
                       - Implement unit test tests/unit/test_ring_buffer.c
                       - Verify 100,000+ event wrap-around and overwrite

Week 2 (Days 8–10)  : Signal Handler & Async-Freeze
                       - Implement src/collector/signal_handler.c with sigaction & sigaltstack
                       - Verify freeze guarantees on synthetic segfault

Week 2 (Days 11–14) : State Collector & Linux /proc Inspector
                       - Implement src/collector/state_collector.c
                       - Memory mapping parser (/proc/self/maps)

Week 3 (Days 15–18) : Master CLI Harness (src/main.c) & Integration
                       - Implement CLI options parsing and pipeline runner
                       - Connect mock collector and mock analyzer for end-to-end dry run

Week 3 (Days 19–21) : Joint Integration Testing with Dev 2 & Dev 3
                       - Wire Dev 2's interceptor hooks into ring_buffer_record()
                       - Wire Dev 3's correlator to inspect frozen ring_buffer snapshot
```

---

## 6. Verification & Test Results for Dev 1

### 6.1 Test Suite Status: ALL TESTS PASSED (8/8)

#### Ring Buffer Verification (`tests/unit/test_ring_buffer.c`)
- **`test_init_and_empty` [PASS]:** Confirms zero-initialized state, valid capacity sizing, empty boundary handling.
- **`test_sequential_record` [PASS]:** Validates monotonic sequential ID assignment and field preservation.
- **`test_wrap_around_overwrite` [PASS]:** Capacity 4 buffer filled with 6 events. Validates that the oldest surviving event is sequence 3, count is capped at 4, and FIFO eviction is mathematically correct.
- **`test_freeze_invariance` [PASS]:** Confirms that zero writes are permitted once the atomic freeze barrier is engaged.
- **`test_multithreaded_stress` [PASS]:** 4 threads concurrently emitting 10,000 total events. Sequence order remained strictly monotonic with 0 race conditions.

#### Signal & State Collector Verification (`tests/unit/test_signal_handler.c`)
- **`test_signal_naming` [PASS]:** Human-readable signal decoding for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGPIPE.
- **`test_state_collector_snapshot` [PASS]:** Process state snapshotting, TID/PID extraction, and virtual memory address mapping checks (`state_collector_is_addr_mapped`).
- **`test_handler_install_uninstall` [PASS]:** Safe registration and unregistration of crash preservation handlers.

---

## 7. Current Implementation Status Summary

| Component | File Location | Owner | Status |
| :--- | :--- | :--- | :--- |
| **Event Definitions** | `include/event.h` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Ring Buffer API** | `include/buffer.h` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Ring Buffer Core** | `src/buffer/ring_buffer.c` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Signal Trapping** | `src/collector/signal_handler.c` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Process State Collector**| `src/collector/state_collector.c` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Main CLI Driver** | `src/main.c` | Anshul Gupta (Dev 1) | **Complete & Verified** |
| **Unit Test Suite** | `tests/unit/test_ring_buffer.c` | Anshul Gupta (Dev 1) | **5/5 Passing** |
| **Signal Test Suite**| `tests/unit/test_signal_handler.c`| Anshul Gupta (Dev 1) | **3/3 Passing** |
| **Make Build System** | `Makefile` | Anshul Gupta (Dev 1) | **Complete & Verified** |

