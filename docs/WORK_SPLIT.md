# Team Work Split & Responsibility Matrix
## Project: CRASH ARCHAEOLOGIST 2.0
### BCSE303P — Operating Systems Lab | Fall 2026–2027


---

## 1. Overview of Division of Labor

The project is logically divided across the 5 primary pipeline phases defined in Review 1:
$$\text{Capture} \longrightarrow \text{Preserve} \longrightarrow \text{Correlate} \longrightarrow \text{Reconstruct} \longrightarrow \text{Explain}$$

To ensure equal, clear, and non-overlapping ownership, the workload is distributed as follows:

```
+-------------------------------------------------------------------------------------------------+
|                                     MODULE OWNERSHIP SUMMARY                                    |
+------------------------------------+------------------------------------+-----------------------+
| Member 1:                          | Member 2:                          | Member 3:              |
+------------------------------------+------------------------------------+-----------------------+
| - Circular Ring Buffer Architecture| - Memory & Allocator Tracing       | - Event Correlator    |
| - Signal Interception & Freeze     | - Syscall & I/O Interceptors       | - Causal Engine       |
| - State Collector & /proc inspect  | - Thread & IPC Interceptors        | - Synthetic Workloads |
| - Core Data Structures & CLI Main  | - Synchronization / Mutex Tracing  | - Benchmark & Reports |
+------------------------------------+------------------------------------+-----------------------+
```

---

## 2. Detailed Member Responsibilities & Deliverables

### Member 1:
**Role:** Systems Architecture, In-Memory Flight Recorder & Preservation Lead

- **Primary Components Owned:**
  1. `include/event.h` & `include/buffer.h`: Master event structure design and category enums.
  2. `src/buffer/ring_buffer.c`: High-performance circular buffer with atomic indexing, overwrite logic, and instant signal freezing.
  3. `src/collector/signal_handler.c`: Interception of fatal POSIX signals (`SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`) via `sigaction` with `SA_SIGINFO`.
  4. `src/collector/state_collector.c`: Process state snapshotting upon crash (reading `/proc/self/maps`, thread register metadata, faulting address `si_addr`).
  5. `src/main.c`: Driver application, CLI parameter parsing, and pipeline orchestration.
- **Milestone Deliverables:**
  - Robust, thread-safe circular event buffer with zero heap reallocation during runtime.
  - Safe crash-time preservation preventing memory corruption during fault inspection.

---

### Member 2: 
**Role:** OS Instrumentation, Hooking & Event Collection Specialist

- **Primary Components Owned:**
  1. `src/collector/mem_hook.c`: Allocation & deallocation instrumentation (wrapping `malloc`, `calloc`, `realloc`, `free`, `mmap`, `munmap`) to feed memory events into the flight recorder.
  2. `src/collector/thread_hook.c`: Thread lifecycle and synchronization tracking (wrapping `pthread_create`, `pthread_join`, `pthread_mutex_lock`, `pthread_mutex_unlock`).
  3. `src/collector/syscall_hook.c`: Syscall & I/O tracking (monitoring `open`, `close`, `read`, `write`, tracking file descriptors and return codes/`errno`).
  4. `src/collector/ipc_hook.c`: IPC event observation (monitoring pipe creation/traffic, socket send/recv, and FIFO transactions).
- **Milestone Deliverables:**
  - Seamless interception layer recording OS events with low CPU overhead.
  - Metadata population (timestamps, thread IDs, affected memory pointers, descriptor tags).

---

### Member 3: 
**Role:** Causal Graph Analysis, Diagnostic Reporting & Workload Evaluation Lead

- **Primary Components Owned:**
  1. `src/analyzer/correlator.c`: Backwards timeline scanner matching faulting TID, address, and timestamps.
  2. `src/analyzer/causal_graph.c`: Dependency linking and cycle detection (for deadlocks and race conditions).
  3. `src/analyzer/root_cause.c`: Heuristic inference engine attributing probable causes (Segfault, UAF, Leak, Deadlock, Resource Exhaustion, IPC) and calculating confidence scores (0%–100%).
  4. `src/reporter/report_generator.c`: Automated plain-text and JSON crash report generator (`reports/`).
  5. `tests/workloads/*`: The 7 synthetic test failure programs:
     - `test_segfault.c`, `test_uaf.c`, `test_memleak.c`, `test_race.c`, `test_deadlock.c`, `test_resource_exhaustion.c`, `test_ipc_failure.c`.
  6. `scripts/benchmark_overhead.py`: Empirical performance benchmarking measuring CPU runtime overhead (<5% target) and memory footprint.
- **Milestone Deliverables:**
  - Fully automated root-cause detection engine with confidence scoring.
  - Comprehensive test suite validating every failure mode outlined in Review 1.

---

## 3. Work Breakdown Structure (WBS) & Review Roadmap

| Phase / Review | Focus | Milestones | Primary Contributors |
| :--- | :--- | :--- | :--- |
| **Review 1 (Completed)** | Concept & Architecture | Problem statement, literature review, abstract, objectives, pipeline design. | All Members |
| **Sprint 1 (Weeks 1–3)** | Data Structures & Flight Recorder | Circular ring buffer, signal trapping, initial wrapper templates, build system. | Anshul (Lead), Adharshini |
| **Sprint 2 (Weeks 4–6)** | Event Collection & Hooking | Memory, thread, syscall, IPC tracing hooks operational. Basic unit tests. | Adharshini (Lead), Rohan |
| **Sprint 3 (Weeks 7–9)** | Causal Analysis & Correlator | Root-cause heuristics, backwards timeline correlation, report generation. | Rohan (Lead), Anshul |
| **Sprint 4 (Weeks 10–12)**| Workloads, Testing & Benchmarking | All 7 failure workloads verified, overhead benchmarked (<5%), Review 2 presentation. | All Members |

---

## 4. Responsibility Assignment Matrix (RACI)

- **R** = Responsible (Does the work)
- **A** = Accountable (Final approval/correctness)
- **C** = Consulted (Provides input)
- **I** = Informed (Kept updated)

| Project Component / Activity | Anshul Gupta | Adharshini Rajesh | Rohan Gopal Nayak |
| :--- | :---: | :---: | :---: |
| **Circular Ring Buffer (`buffer.h`, `ring_buffer.c`)** | **A / R** | C | I |
| **Signal Handling & Freeze (`signal_handler.c`)** | **A / R** | C | I |
| **Memory & Allocator Hooking (`mem_hook.c`)** | C | **A / R** | C |
| **Thread & Sync Tracking (`thread_hook.c`)** | C | **A / R** | C |
| **Syscall & IPC Tracing (`syscall_hook.c`, `ipc_hook.c`)** | I | **A / R** | C |
| **Event Correlator & Graph (`correlator.c`)** | C | C | **A / R** |
| **Root Cause Engine & Confidence (`root_cause.c`)** | C | C | **A / R** |
| **Report Generator (`report_generator.c`)** | I | I | **A / R** |
| **Test Workloads (Segfault, UAF, Deadlock, etc.)** | C | C | **A / R** |
| **Benchmarking & Overhead Analysis** | C | C | **A / R** |
| **Documentation & Review Presentations** | **A / R** | **A / R** | **A / R** |
