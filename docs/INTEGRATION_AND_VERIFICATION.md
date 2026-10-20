# Crash Archaeologist 2.0: Full Team Integration & Verification Report

**Course:** BCSE303P — Operating Systems Lab  
**Academic Term:** Fall Semester 2026–2027  
**Faculty Guide:** Dr. Porkodi S (Assistant Professor Senior Grade – 1)  
**School:** School of Computer Science Engineering and Information Systems (SCOPE)  

### Contributors & Merged Branches
1. **Anshul Gupta (24BYB0149)** — Branch: `Anshul_dev`  
   *Role:* Systems Lead, In-Memory Flight Recorder, Signal Preservation, State Collector, CLI Orchestrator  
2. **Adharshini Rajesh (24BYB0150)** — Branch: `feature/adharshini-event-hooks`  
   *Role:* OS Interception Hooks (`mem_hook`, `thread_hook`, `syscall_hook`, `ipc_hook`, `collector.c`)  
3. **Rohan Gopal Nayak (24BYB0132)** — Branch: `person-3-causal-analysis`  
   *Role:* Causal Correlator, Wait-For-Graph & Deadlock Detection, Root-Cause Inference Engine, Report Generator, Synthetic Workload Suite  

---

## 1. Executive Summary & Integration Architecture

All three independent developer branches have been pulled, reconciled, merged, and fully integrated into `main` and `Anshul_dev`.

```
                                  CRASH ARCHAEOLOGIST 2.0 PIPELINE
                                  
    [Target Program / Workload]
                 │
                 ▼
    [Member 2: Interception Hooks]
    - mem_hook.c (malloc, free, realloc, mmap, munmap)
    - thread_hook.c (pthread_create, pthread_join, pthread_mutex_lock/unlock)
    - syscall_hook.c (open, close, read, write)
    - ipc_hook.c (pipe read/write, socket send/recv)
                 │ (Emits zero-allocation crash_event_t)
                 ▼
    [Member 1: Circular Flight Recorder]
    - ring_buffer.c (Thread-safe, N=8192 capacity, O(1) overwrite)
                 │
           [FATAL SIGNAL]
                 ▼
    [Member 1: Signal Trapping & Preservation]
    - signal_handler.c (Traps SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGPIPE)
    - state_collector.c (Parses /proc/self/maps, captures fault address & register context)
    - Freezes ring buffer immediately via memory barrier
                 │
                 ▼
    [Member 3: Post-Crash Causal Engine]
    - correlator.c (Chronological timeline extraction & fault address filtering)
    - causal_graph.c (Wait-For-Graph cycle detection for AB-BA deadlocks)
    - root_cause.c (Heuristic inference: UAF, Segfault, Deadlock, Leak, Race, Exhaustion, IPC)
                 │
                 ▼
    [Member 3: Report Generator]
    - report_generator.c (Generates human-readable console/text reports & machine-parseable JSON)
```

---

## 2. Integrated Source Tree

```
os_project/
├── Makefile                            # Master build system
├── README.md                           # Project overview
├── bin/                                # Compiled binaries & test runners
│   ├── crash_archaeologist             # Integrated flight recorder CLI
│   ├── test_ring_buffer                # Member 1 unit tests
│   ├── test_signal_handler             # Member 1 signal tests
│   ├── test_analyzer                   # Member 3 analyzer tests
│   ├── test_causal_graph               # Member 3 graph & deadlock tests
│   ├── bench_analyzer                  # Analyzer throughput benchmark
│   ├── test_segfault                   # Workload 1: Segfault
│   ├── test_uaf                        # Workload 2: Use-After-Free
│   ├── test_memleak                    # Workload 3: Memory Leak
│   ├── test_race                       # Workload 4: Data Race
│   ├── test_deadlock                   # Workload 5: AB-BA Deadlock
│   ├── test_resource_exhaustion        # Workload 6: EMFILE Exhaustion
│   └── test_ipc_failure                # Workload 7: Broken Pipe IPC
├── docs/
│   ├── ARCHITECTURE.md                 # System architecture & dataflow
│   ├── DEV1_IMPLEMENTATION_PLAN.md     # Member 1 implementation & verification
│   ├── INTEGRATION_AND_VERIFICATION.md # Full team integration report
│   ├── PERSON_3_HANDOFF.md             # Member 3 handoff guide
│   ├── PRD.md                          # Product requirements document
│   ├── SPECIFICATION.md                # Data structures & heuristics
│   ├── TESTING_AND_EVALUATION.md       # Review defense test protocols
│   └── WORK_SPLIT.md                   # RACI responsibility matrix
├── include/
│   ├── buffer.h                        # Circular buffer API & ring_buffer_t
│   ├── collector.h                     # Unified hook & state collector APIs
│   ├── engine.h                        # Causal analysis & reporter headers
│   ├── event.h                         # Uniform crash_event_t struct & enums
│   └── signals.h                       # Signal trapping headers
├── src/
│   ├── main.c                          # Integrated CLI driver & demo
│   ├── analyzer/
│   │   ├── analyzer_internal.h
│   │   ├── causal_graph.c              # Deadlock graph & cycle detection
│   │   ├── correlator.c                # Backwards timeline correlation
│   │   └── root_cause.c                # Multi-class failure classification
│   ├── buffer/
│   │   └── ring_buffer.c               # In-memory circular buffer
│   ├── collector/
│   │   ├── collector.c                 # Hook dispatcher & LD_PRELOAD controller
│   │   ├── ipc_hook.c                  # Pipe/socket interception
│   │   ├── mem_hook.c                  # Memory allocator interception
│   │   ├── signal_handler.c            # Fatal signal interception & freeze
│   │   ├── state_collector.c           # /proc/self/maps parser
│   │   ├── syscall_hook.c              # I/O syscall interception
│   │   └── thread_hook.c               # Thread lifecycle & mutex tracking
│   └── reporter/
│       └── report_generator.c          # RFC-8259 JSON & ASCII report generator
├── tests/
│   ├── unit/
│   │   ├── bench_analyzer.c            # Post-crash analysis benchmark
│   │   ├── mock_ring_buffer.c          # Mock buffer for standalone analyzer tests
│   │   ├── test_analyzer.c             # Root cause inference unit tests
│   │   ├── test_causal_graph.c         # Deadlock cycle detection unit tests
│   │   ├── test_ring_buffer.c          # Flight recorder unit tests
│   │   └── test_signal_handler.c       # Signal trapping unit tests
│   └── workloads/                      # 7 reproducible crash scenarios
│       ├── hook_test.c                 # Live interposition validation
│       ├── test_deadlock.c
│       ├── test_ipc_failure.c
│       ├── test_memleak.c
│       ├── test_race.c
│       ├── test_resource_exhaustion.c
│       ├── test_segfault.c
│       └── test_uaf.c
├── scripts/
│   └── benchmark_overhead.py           # Evaluation runner
└── reports/
    ├── demo_crash_report.json          # Machine-readable report sample
    └── demo_crash_report.txt           # Human-readable report sample
```

---

## 3. Comprehensive Verification & Test Results

### 3.1 Unit Test Pass Rates: 100% (27/27 Passing)

| Test Suite | Component Under Test | Tests Executed | Passed | Status |
| :--- | :--- | :---: | :---: | :---: |
| `test_ring_buffer` | Circular buffer, wrap-around, thread contention, freeze | 5 | 5 | **PASS (100%)** |
| `test_signal_handler` | Fatal signal interception, state snapshot, mappings | 3 | 3 | **PASS (100%)** |
| `test_analyzer` | Post-crash causal correlation & failure heuristics | 6 | 6 | **PASS (100%)** |
| `test_causal_graph` | Lock dependency cycle detection & Wait-For-Graph | 13 | 13 | **PASS (100%)** |
| **Total** | **All System Components** | **27** | **27** | **PASS (100%)** |

### 3.2 Performance & Throughput Benchmarks
- **Analyzer Throughput:** **2,323.2 full post-crash reconstructions per second** on an 8,192-event buffer.
- **Average Analysis Latency:** **0.430 ms** (430.4 microseconds) per crash analysis.
- **Flight Recorder Overhead Target:** Steady-state recording overhead < 5% via zero-allocation ring buffer.

### 3.3 Sample Reconstructed Crash Report (Use-After-Free)
```
================================================================================
                     CRASH ARCHAEOLOGIST 2.0 REPORT
================================================================================
Probable Cause   : CAUSE_USE_AFTER_FREE
Confidence Level : 95.0% (Heuristic Estimate)
Faulting Thread  : 101
Faulting Address : 0x40a110
Explanation      : TID 101 accessed address 0x40a110, which was previously freed by TID 101 at seq 4. Reconstructed as Use-After-Free.
--------------------------------------------------------------------------------
Causal Chain (5 events):
  [00] Seq: 1     | TID: 101  | EVENT_CAT_PROCESS_THREAD | EVENT_THREAD_CREATE    | Addr: 0x0          | Size: 0     | FD/Stat: 0    | Label: worker_thread_init
  [01] Seq: 2     | TID: 101  | EVENT_CAT_MEMORY     | EVENT_MEM_ALLOC        | Addr: 0x40a100     | Size: 2048  | FD/Stat: 0    | Label: malloc(2048)
  [02] Seq: 3     | TID: 101  | EVENT_CAT_SYSCALL    | EVENT_SYS_OPEN         | Addr: 0x0          | Size: 0     | FD/Stat: 3    | Label: open("/etc/config")
  [03] Seq: 4     | TID: 101  | EVENT_CAT_MEMORY     | EVENT_MEM_FREE         | Addr: 0x40a100     | Size: 2048  | FD/Stat: 0    | Label: free(0x0040A100)
  [04] Seq: 5     | TID: 101  | EVENT_CAT_SIGNAL     | EVENT_SIG_RECEIVED     | Addr: 0x40a110     | Size: 0     | FD/Stat: 11   | Label: SIGSEGV
================================================================================
```

---

## 4. How to Build and Run

```bash
# 1. Build the complete project and all workloads:
make all

# 2. Run all unit tests:
make test

# 3. Run the performance benchmark:
make benchmark

# 4. Run the end-to-end flight recorder demo:
./bin/crash_archaeologist --demo
```
