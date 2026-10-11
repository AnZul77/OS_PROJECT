# Technical Specification & Internal Data Structures
## Project: CRASH ARCHAEOLOGIST 2.0
### BCSE303P — Operating Systems Lab

---

## 1. High-Performance Circular Ring Buffer

### 1.1 In-Memory Ring Buffer Design
The ring buffer is allocated in a contiguous virtual memory page block at process startup. It is sized to accommodate a fixed number of events (e.g., $N = 8192$):

```
       +-------------------------------------------------------+
       | [0] | [1] | [2] | ... | [head-1] | [head] | ... | [N-1]|
       +-------------------------------------------------------+
                                              ^
                                         Write Pointer
```

- **Zero-Allocation Critical Path:** Once initialized via `ring_buffer_init()`, no `malloc()` or kernel memory resizing takes place during event emission.
- **Atomic Indexing:** `head` advances monotonically using atomic increment operations (`__sync_fetch_and_add` or C11 `stdatomic.h`) modulo capacity $N$.
- **Instant Freeze:** Upon fatal signal trap, the `frozen` flag is flipped to `true`. All subsequent recording attempts by other threads return immediately, preserving the crash history without race condition clobbering.

---

## 2. Event Representation (`include/event.h`)

Each event is encoded within a fixed-size, 64-byte aligned structure:

```c
typedef struct {
    uint64_t sequence_id;       /* Global monotonic order counter */
    uint64_t timestamp_ns;      /* CLOCK_MONOTONIC / gettimeofday timestamp */
    uint32_t pid;               /* Process Identifier */
    uint32_t tid;               /* POSIX Thread Identifier */
    
    event_category_t category;  /* THREAD, SYSCALL, MEMORY, IO, IPC, SIGNAL */
    event_type_t type;          /* Specific sub-event identifier */
    
    uintptr_t addr;             /* Memory address (pointer, fault addr, mmap base) */
    size_t size;                /* Allocation size or I/O byte count */
    int32_t fd_or_status;       /* File descriptor, signal number, or return code */
    
    char label[32];             /* Descriptive label or function identifier */
} crash_event_t;
```

---

## 3. Signal Trapping & Preservation Strategy

### 3.1 Intercepted Signals
The recorder registers handlers for all standard POSIX termination and crash signals:
- `SIGSEGV` (Invalid memory reference / segmentation fault)
- `SIGBUS` (Bus error / misaligned or unmapped physical address)
- `SIGABRT` (Process abort / assertion failure / glibc heap corruption)
- `SIGFPE` (Erroneous arithmetic operation / divide by zero)
- `SIGILL` (Illegal instruction)
- `SIGPIPE` (Broken pipe during IPC)

### 3.2 Handler Implementation (`SA_SIGINFO`)
The handler is configured using `sigaction()` with the `SA_SIGINFO` flag:
```c
struct sigaction sa;
memset(&sa, 0, sizeof(sa));
sa.sa_sigaction = crash_signal_handler;
sa.sa_flags = SA_SIGINFO | SA_NODEFER;
sigaction(SIGSEGV, &sa, NULL);
```

Within the handler:
1. `ring_buffer_freeze(&g_ring_buffer)` is invoked immediately.
2. The faulting address is extracted from `info->si_addr`.
3. Signal code `info->si_code` is logged (e.g. `SEGV_MAPERR` vs `SEGV_ACCERR`).
4. Control passes to the post-mortem analysis engine.

---

## 4. Root-Cause Causal Analysis Heuristics

The analysis engine scans backward through the preserved ring buffer from $T_0$ (crash instant) to identify the causal anomaly:

```
[Crash Event @ T0: SIGSEGV @ addr 0x55a3012]
         |
         | (Backtracking search for addr 0x55a3012)
         v
[T - 4ms: free(0x55a3012) by TID 102]
         |
         v
==> MATCH: USE_AFTER_FREE detected with 95% Confidence!
```

### Heuristic Decision Matrix:
1. **Use-After-Free (UAF):**
   - Condition: Crash is `SIGSEGV` or `SIGBUS`.
   - Heuristic: The crash address $A$ matches the range $[A_{base}, A_{base} + size]$ of an `EVENT_MEM_FREE` event logged within the last $K$ events.
   - Result: `CAUSE_USE_AFTER_FREE` (Confidence: 95%).

2. **Null / Unmapped Segmentation Fault:**
   - Condition: Crash is `SIGSEGV` with `si_addr == NULL` or address $< 0x1000$ (low page null offset).
   - Heuristic: Faulting instruction executed without prior valid allocation, or `si_code == SEGV_MAPERR`.
   - Result: `CAUSE_SEGFAULT` (Confidence: 98%).

3. **Memory Leak:**
   - Condition: Application encounters `ENOMEM` or excessive RSS.
   - Heuristic: Net allocation count $(\sum \text{alloc} - \sum \text{free})$ continuously grows monotonically without equilibrium.
   - Result: `CAUSE_MEMORY_LEAK` (Confidence: 85%).

4. **Circular Deadlock:**
   - Condition: Execution hangs or watchdog timer triggers.
   - Heuristic: Resource allocation graph constructed from `EVENT_SYNC_MUTEX_LOCK` records contains a directed cycle (Thread A holds Lock 1, waiting on Lock 2; Thread B holds Lock 2, waiting on Lock 1).
   - Result: `CAUSE_DEADLOCK` (Confidence: 95%).

5. **Resource Exhaustion:**
   - Condition: Syscall returns negative with `errno == EMFILE` (too many open files) or `ENFILE`.
   - Heuristic: Recent history contains rapid sequence of `EVENT_SYS_OPEN` without intervening `EVENT_SYS_CLOSE`.
   - Result: `CAUSE_RESOURCE_EXHAUSTION` (Confidence: 95%).

6. **IPC Failure:**
   - Condition: Crash via `SIGPIPE` or write failure `EPIPE`.
   - Heuristic: Target socket/pipe experienced unexpected remote disconnect or closed descriptor write.
   - Result: `CAUSE_IPC_FAILURE` (Confidence: 90%).
