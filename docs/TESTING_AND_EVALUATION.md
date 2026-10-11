# Project Evaluation, Testing & Review Defense Plan
## Project: CRASH ARCHAEOLOGIST 2.0
### BCSE303P — Operating Systems Lab

---

## 1. Overview & Evaluation Goals
The system must be quantitatively and qualitatively verified before the faculty review panel (Dr. Porkodi S). This document specifies:
1. The synthetic test suites covering all 7 mandatory failure classes.
2. Overhead measurement protocol (<5% target).
3. Demonstration script and expected crash reports for viva/review defense.

---

## 2. Synthetic Workload Test Suite (`tests/workloads/`)

Each test failure scenario is codified as an independent, reproducible test program:

### 2.1 Segmentation Fault (`tests/workloads/test_segfault.c`)
- **Action:** Spawns a worker thread that intentionally dereferences a null pointer or an unmapped memory address (`0x0` or `(int*)0xDEADBEEF`).
- **Expected Archaeologist Output:**
  - Signal: `SIGSEGV`
  - Cause: `CAUSE_SEGFAULT` (Confidence: 98%)
  - Explanation: "Illegal dereference of unmapped address 0xDEADBEEF by Thread 2."

### 2.2 Use-After-Free (`tests/workloads/test_uaf.c`)
- **Action:** Thread 1 allocates a 1KB block (`ptr = malloc(1024)`), performs valid writes, frees it (`free(ptr)`), and 50 milliseconds later Thread 2 attempts to write to `ptr[10]`.
- **Expected Archaeologist Output:**
  - Signal: `SIGSEGV`
  - Cause: `CAUSE_USE_AFTER_FREE` (Confidence: 95%)
  - Causal Chain: Explicitly points to sequence ID of the earlier `free()` call executed by Thread 1.

### 2.3 Memory Leak (`tests/workloads/test_memleak.c`)
- **Action:** A background thread allocates blocks in a tight loop without freeing them until physical memory threshold triggers.
- **Expected Archaeologist Output:**
  - Cause: `CAUSE_MEMORY_LEAK` (Confidence: 85%)
  - Metric: Shows monotonically increasing net heap delta over recent history frames.

### 2.4 Data Race (`tests/workloads/test_race.c`)
- **Action:** Two threads concurrently increment a shared pointer address without acquiring the mutex.
- **Expected Archaeologist Output:**
  - Cause: `CAUSE_DATA_RACE` (Confidence: 90%)
  - Evidence: Shows interleaved read/write memory operations without lock ownership.

### 2.5 Circular Deadlock (`tests/workloads/test_deadlock.c`)
- **Action:** Classic Dining Philosophers / AB-BA locking scenario: Thread 1 locks Mutex A and requests Mutex B; Thread 2 locks Mutex B and requests Mutex A.
- **Expected Archaeologist Output:**
  - Cause: `CAUSE_DEADLOCK` (Confidence: 95%)
  - Graph: Emits cycle detection log: `Thread 1 -> Mutex B -> Thread 2 -> Mutex A -> Thread 1`.

### 2.6 Resource Exhaustion (`tests/workloads/test_resource_exhaustion.c`)
- **Action:** Process repeatedly calls `open()` in a loop without `close()` until `EMFILE` (Too many open files) is returned, causing an unhandled crash or abort.
- **Expected Archaeologist Output:**
  - Cause: `CAUSE_RESOURCE_EXHAUSTION` (Confidence: 95%)
  - Explanation: "Exhaustion of file descriptors. 1024 open descriptors detected prior to failure."

### 2.7 IPC Broken Pipe Failure (`tests/workloads/test_ipc_failure.c`)
- **Action:** A parent process spawns a child connected via a pipe. Child abruptly closes its read end and exits. Parent attempts to write to the closed pipe endpoint without handling `SIGPIPE`.
- **Expected Archaeologist Output:**
  - Signal: `SIGPIPE`
  - Cause: `CAUSE_IPC_FAILURE` (Confidence: 92%)
  - Causal Chain: Child close event correlated directly with parent's fatal pipe write.

---

## 3. Overhead Benchmarking Protocol

Run via `scripts/benchmark_overhead.py`:
- **Workload:** Standard compute + I/O intensive multithreaded benchmark (matrix multiplication + file logging).
- **Runs:**
  1. **Baseline:** Uninstrumented execution time ($T_{base}$).
  2. **Archaeologist Enabled:** Execution with continuous flight recording active ($T_{record}$).
- **Calculated Metric:**
  $$\text{Overhead \%} = \frac{T_{record} - T_{base}}{T_{base}} \times 100$$
- **Target:** Must strictly achieve $\text{Overhead \%} < 5.0\%$.

---

## 4. Viva / Review 2 & Final Review Defense Q&A

**Q1: Why not just use Core Dumps and GDB?**  
*Answer:* Core dumps only save the state *at the crash point*. If a pointer was corrupted or freed 10 seconds earlier by another thread, the core dump cannot tell you who freed it or when. Crash Archaeologist keeps a continuous, pre-crash rolling history to reconstruct the timeline.

**Q2: Does your flight recorder slow down the OS or the process?**  
*Answer:* No. The recorder uses a fixed-size in-memory ring buffer with $O(1)$ overwrite mechanics and zero disk I/O during steady execution. Disk writes only happen *after* a crash is detected to save the report.

**Q3: How do you handle signal safety?**  
*Answer:* The signal handler merely flips an atomic freeze flag (`frozen = true`) and triggers the post-mortem dumper. No unsafe blocking mutexes are acquired inside the signal handler context.
