# CRASH ARCHAEOLOGIST 2.0
> **An OS-Level Flight Recorder for Post-Crash Root-Cause Reconstruction**

**Course:** BCSE303P — Operating Systems Lab  
**Academic Term:** Fall Semester 2026–2027  
**School:** School of Computer Science Engineering and Information Systems (SCOPE)  
**Supervisor / Guide:** Dr. Porkodi S (Assistant Professor Senior Grade – 1)  

### Team Members
- **Anshul Gupta** (24BYB0149)
- **Adharshini Rajesh** (24BYB0150)
- **Rohan Gopal Nayak** (24BYB0132)

---

## 1. Executive Summary & Core Principle
Traditional crash inspection mechanisms (core dumps, `gdb` backtraces, log files) capture the **terminal state** of a program when it crashes. However, they lack the historical context of non-fatal anomalies that transpired immediately prior to termination.

**Crash Archaeologist** acts as an in-memory, zero-copy/low-overhead **OS flight recorder**:
$$\text{Capture} \longrightarrow \text{Preserve} \longrightarrow \text{Correlate} \longrightarrow \text{Reconstruct} \longrightarrow \text{Explain}$$

1. **Capture:** Low-overhead circular ring buffer recording thread lifecycle, syscalls, memory management (`brk`/`mmap`/`malloc`/`free`), IPC, and synchronization primitives.
2. **Preserve:** Freezes the ring buffer upon intercepting fatal signals (`SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`) or exit anomalies.
3. **Correlate:** Maps thread IDs, timestamps, memory addresses, and file descriptors to link cross-thread and cross-process interactions.
4. **Reconstruct:** Builds a directed causal timeline leading backwards from the point of failure.
5. **Explain:** Emits a structured diagnostic report with probable root cause, timeline slice, and confidence score.

---

## 2. Supported Failure Modes (Workload Taxonomy)
The system is designed and evaluated against the specific failure classes identified in Review 1:

| Failure Mode | Manifestation | Archeologist Detection Heuristic |
| :--- | :--- | :--- |
| **Segmentation Fault** | `SIGSEGV` (Invalid dereference / unmapped page) | Intercepts signal; maps faulting address against virtual memory maps and recent pointer events. |
| **Use-After-Free (UAF)** | Accessing heap chunk after deallocation | Correlates crash pointer address with prior `free()` / `munmap()` event in ring buffer. |
| **Memory Leak** | Unbounded RSS growth, unreleased buffers | Tracks cumulative allocation delta vs freed addresses per thread over time. |
| **Data Race** | Concurrent unsynchronized read/write | Detects interleaved shared-memory access from multiple threads without intervening mutex lock/unlock. |
| **Deadlock** | Mutual thread wait lockup | Cycle detection on lock-acquisition events and blocked syscalls (`futex`, `pthread_mutex_lock`). |
| **Resource Exhaustion** | `EMFILE` (FD exhaustion), `ENOMEM`, thread exhaustion | Records failing syscall return codes preceded by rapid descriptor/allocation spikes. |
| **IPC Failure** | Broken pipes (`SIGPIPE`), socket disconnect, queue overflow | Captures dropped IPC write calls, orphaned FIFO/pipe endpoints, and IPC error codes. |

---

## 3. Project Architecture & Directory Layout

```
os_project/
├── .gitignore
├── README.md
├── Makefile
├── docs/
│   ├── ARCHITECTURE.md
│   └── SPECIFICATION.md
├── include/
│   ├── buffer.h        # Circular ring buffer data structures
│   ├── collector.h     # Interception, hooks & event emission
│   ├── correlator.h    # Cross-thread / syscall correlation engine
│   ├── engine.h        # Root cause analyzer and causal graph
│   ├── event.h         # Common event definitions, types, enums
│   ├── reporter.h      # Human-readable & JSON crash report generator
│   └── signals.h       # Signal interception and handler preservation
├── src/
│   ├── buffer/
│   │   └── ring_buffer.c
│   ├── collector/
│   │   ├── syscall_hook.c
│   │   ├── mem_hook.c
│   │   ├── thread_hook.c
│   │   └── signal_handler.c
│   ├── analyzer/
│   │   ├── correlator.c
│   │   ├── causal_graph.c
│   │   └── root_cause.c
│   ├── reporter/
│   │   └── report_generator.c
│   └── main.c
├── tests/
│   ├── unit/
│   │   └── test_ring_buffer.c
│   └── workloads/
│       ├── test_segfault.c
│       ├── test_uaf.c
│       ├── test_memleak.c
│       ├── test_race.c
│       ├── test_deadlock.c
│       ├── test_resource_exhaustion.c
│       └── test_ipc_failure.c
├── scripts/
│   ├── run_workloads.py
│   └── benchmark_overhead.py
└── reports/            # Output directory for generated crash reports
```

---

## 4. Key Components Explained

### 4.1 Circular Event Buffer (`include/buffer.h`, `src/buffer/`)
- Fixed-size lock-free / low-overhead ring buffer maintaining the last $N$ events (e.g., $10,000$ events).
- Zero disk-I/O overhead during normal execution.
- Overwrites oldest events sequentially; freezes immediately upon signal handler dispatch.

### 4.2 Event Collector (`src/collector/`)
- Monitors:
  - **Thread & Process:** `pthread_create`, `pthread_join`, thread ID switches, context switches.
  - **Memory:** `malloc`, `calloc`, `realloc`, `free`, `mmap`, `munmap`.
  - **Syscalls & I/O:** `read`, `write`, `open`, `close`, socket/pipe IPC.
  - **Signals:** Intercepts signals via POSIX signal handlers (`sigaction` with `SA_SIGINFO`).

### 4.3 Event Correlator & Causal Analysis (`src/analyzer/`)
- Reads the frozen circular buffer backwards in time from failure timestamp $T_0$.
- Filters events related to the faulting thread ID, memory addresses, or file descriptors.
- Identifies anomalies (e.g., address $A$ dereferenced at $T_0$, which was marked freed at $T_{-5}$).

### 4.4 Report Generator (`src/reporter/`)
- Produces a plain-text human-readable report as well as a JSON export.
- Specifies:
  - **Process / Thread metadata:** PID, TID, timestamp, exit signal.
  - **Identified Root Cause:** e.g., `USE_AFTER_FREE` or `CIRCULAR_DEADLOCK`.
  - **Confidence Score:** 0% – 100%.
  - **Chronological Causal Sequence:** Last critical events leading to failure.
