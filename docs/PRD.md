# Product Requirements Document (PRD)
## Project: CRASH ARCHAEOLOGIST 2.0
### An OS-Level Flight Recorder for Post-Crash Root-Cause Reconstruction

- **Academic Context:** BCSE303P — Operating Systems Lab, Fall Semester 2026–2027
- **Institution:** School of Computer Science Engineering and Information Systems (SCOPE)
- **Faculty Guide:** Dr. Porkodi S, Assistant Professor Senior Grade – 1
- **Team:**
  - Anshul Gupta (24BYB0149)
  - Adharshini Rajesh (24BYB0150)
  - Rohan Gopal Nayak (24BYB0132)
- **Status:** Approved Baseline (Post-Review 1)

---

## 1. Problem Statement & Motivation
Modern operating systems and debugging tools (GDB, Valgrind, AddressSanitizer, kernel core dumps) excel at diagnosing single-point failures at the instant of failure. However:
1. **Core dumps are terminal snapshots:** They capture *where* an application died (instruction pointer, registers, stack frames), but erase the historical sequence of prior valid or non-fatal operations that led to the fault.
2. **Traditional debuggers induce severe overhead:** Tools like Valgrind introduce 10x–50x runtime slowdowns, altering race conditions and timing-dependent bugs (Heisenbugs).
3. **Multi-threaded and IPC context loss:** When Thread B crashes due to data corrupted by Thread A 500 milliseconds earlier, post-mortem analysis cannot infer causality without continuous historical recording.

**Crash Archaeologist 2.0** solves this by providing a low-overhead, in-memory continuous flight recorder that captures system events into a circular buffer, freezes state immediately upon a crash signal, and performs post-mortem causal reconstruction to explain the root cause.

---

## 2. Core Functional Requirements (FR)

### FR-1: Continuous Low-Overhead Recording (Normal Operation)
- **FR-1.1:** Must record OS events into a pre-allocated fixed-size circular ring buffer ($N = 8,192$ to $65,536$ event slots).
- **FR-1.2:** Zero disk I/O or heap reallocations during steady-state execution to minimize runtime latency overhead (<5% execution time overhead target).
- **FR-1.3:** Atomic sequence numbering and thread-safe event emission.

### FR-2: Event Collection & Interception
The recorder must track events across five key OS subsystems:
- **Thread & Process Lifecycle:** `pthread_create`, `pthread_join`, `pthread_exit`, TID switches.
- **Memory Management:** `malloc`, `calloc`, `realloc`, `free`, `mmap`, `munmap`.
- **System Calls & I/O:** `open`, `close`, `read`, `write`, return codes, and error numbers (`errno`).
- **Synchronization Primitives:** Mutex lock requests, acquisitions, releases, and contention wait events.
- **Inter-Process Communication (IPC):** Pipe read/write, UNIX socket transmit/receive, message queues.

### FR-3: Failure Detection & Buffer Freezing
- **FR-3.1:** Intercept fatal POSIX signals (`SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGPIPE`).
- **FR-3.2:** Execute async-signal-safe freeze of the circular buffer, preventing subsequent thread activity from overwriting crash-relevant history.
- **FR-3.3:** Capture fault metadata via `siginfo_t` (`si_signo`, `si_code`, `si_addr`, faulting TID/PID).

### FR-4: Post-Crash State Collection
- Read `/proc/self/maps` (or virtual memory region table) to verify whether `si_addr` was unmapped, mapped read-only, or within heap/stack bounds.
- Snapshot open file descriptors and active threads.

### FR-5: Event Correlation & Causal Reconstruction
- **FR-5.1:** Backtrack from crash timestamp $T_{crash}$ along the causal timeline.
- **FR-5.2:** Heuristic detection for target failure modes:
  1. **Segmentation Fault:** Unmapped memory dereference or NULL pointer dereference.
  2. **Use-After-Free (UAF):** Faulting address matches or falls within an address block previously marked with `EVENT_MEM_FREE`.
  3. **Memory Leak:** Monotonic net allocation growth without corresponding releases across monitored epochs.
  4. **Data Race:** Interleaved unsynchronized read/write memory operations without lock ownership.
  5. **Deadlock:** Cycle detection in resource-allocation graphs / blocked lock acquisition chains.
  6. **Resource Exhaustion:** Rapid burst of allocations or file descriptor allocations followed by `ENOMEM` or `EMFILE`.
  7. **IPC Failure:** EPIPE / broken pipe errors or closed endpoints preceding child termination.

### FR-6: Human-Readable & Structured Reporting
- Generate formatted text report (`reports/crash_report_<timestamp>.txt`) and JSON output (`reports/crash_report_<timestamp>.json`).
- Report must output:
  - Header: Target process name, PID, faulting TID, timestamp, crash signal.
  - Diagnosis: Probable root cause (e.g. `USE_AFTER_FREE`) and confidence score (0%–100%).
  - Detailed Explanation: Human-readable paragraph explaining why the cause was determined.
  - Causal Timeline: Chronological order of the most relevant events preceding the crash.

---

## 3. Non-Functional Requirements (NFR)

- **NFR-1 (Performance / Low Overhead):** Steady-state flight recording must incur < 5% CPU overhead on representative multithreaded benchmarks.
- **NFR-2 (Memory Footprint):** Memory overhead constrained to a fixed buffer footprint (typically 2MB–8MB depending on event buffer slot capacity).
- **NFR-3 (Reliability / Safety):** The recorder must never crash the target application itself; signal handlers must adhere strictly to signal safety standards.
- **NFR-4 (Portability & Modularity):** Compatible with POSIX / Linux standards (tested on Ubuntu / WSL2 / standard GCC C99/C11 environments).
- **NFR-5 (Reproducibility):** Dedicated synthetic test suite covering all 7 target failure workloads with 100% automated reproduction.

---

## 4. Acceptance Criteria & Verification Matrix

| Feature | Acceptance Criteria | Test Target |
| :--- | :--- | :--- |
| **Ring Buffer** | Overwrites oldest events without locking deadlocks; freezes instantly on signal. | `tests/unit/test_ring_buffer.c` |
| **Segfault Detection** | Identifies invalid/null pointer dereference with confidence $\ge 90\%$. | `tests/workloads/test_segfault.c` |
| **Use-After-Free** | Accurately identifies prior `free()` invocation of faulting address with confidence $\ge 95\%$. | `tests/workloads/test_uaf.c` |
| **Memory Leak** | Detects persistent un-freed allocations after thread exit with confidence $\ge 85\%$. | `tests/workloads/test_memleak.c` |
| **Deadlock** | Flags circular lock dependency and identifies stuck threads with confidence $\ge 90\%$. | `tests/workloads/test_deadlock.c` |
| **Resource Exhaustion** | Detects FD or memory limit exhaustion (`EMFILE`/`ENOMEM`) with confidence $\ge 95\%$. | `tests/workloads/test_resource_exhaustion.c` |
| **IPC Failure** | Pinpoints broken pipe / socket disconnect leading to process death with confidence $\ge 90\%$. | `tests/workloads/test_ipc_failure.c` |
