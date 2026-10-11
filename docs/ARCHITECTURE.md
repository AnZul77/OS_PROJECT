# Crash Archaeologist: System Architecture & Design Specification

## 1. System Pipeline Overview

```
+-------------------------------------------------------------+
|                      Target Programs                        |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                       Event Collector                       |
|   - Thread/Process Lifecycle (create, exit, switch)         |
|   - Memory Allocator Interception (malloc, free, mmap)      |
|   - Syscall Tracing (read, write, open, close, futex)       |
|   - Signal Interception (SIGSEGV, SIGABRT, SIGBUS, etc.)    |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                    Circular Event Buffer                    |
|   - Fixed-size in-memory ring buffer (e.g. 10,000 entries)  |
|   - Atomic / thread-safe sequence increment                 |
|   - High-throughput, zero disk I/O during execution         |
+-------------------------------------------------------------+
                               |
                        [Failure Signal]
                               v
+-------------------------------------------------------------+
|                      Failure Detection                      |
|   - Intercepts fatal signal via sigaction / SA_SIGINFO      |
|   - Freezes the ring buffer immediately                     |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                       State Collector                       |
|   - Captures process memory map (/proc/self/maps)           |
|   - Captures register state & fault address from siginfo_t  |
|   - Captures thread list & open file descriptors            |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                       Event Correlator                      |
|   - Chronological indexing of buffered events               |
|   - Filter by faulting thread, memory address, or FD        |
|   - Cross-thread causation linking                          |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                       Causal Analysis                       |
|   - Pattern matching against failure heuristics:            |
|       * Segfault / Invalid dereference                      |
|       * Use-After-Free (access after free event)            |
|       * Memory Leak (unbounded monotonic alloc delta)       |
|       * Deadlock (lock acquisition dependency cycle)        |
|       * Resource Exhaustion (EMFILE, ENOMEM, etc.)          |
|       * IPC Failure (broken pipes, closed socket writes)    |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                         Crash Report                        |
|   - Plain text human-readable summary                       |
|   - Machine-parseable JSON summary                          |
|   - Causal sequence of preceding events                     |
|   - Probable root cause & confidence rating                 |
+-------------------------------------------------------------+
```

## 2. Low-Overhead Engineering Considerations
1. **Memory Bound:** The ring buffer uses a fixed pre-allocated memory chunk. No dynamic memory allocations occur inside the tracing critical path.
2. **Signal-Safe Freezing:** When a signal strikes, the collector flips an atomic freeze flag to avoid race conditions during post-crash inspection.
3. **No External Kernel Dependencies Required for Lab Workloads:** Can be implemented and verified in user-space via library preloading (`LD_PRELOAD`) or wrapper instrumentation, keeping it portable for OS Lab evaluations.
