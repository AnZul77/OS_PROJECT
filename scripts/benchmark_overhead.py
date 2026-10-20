#!/usr/bin/env python3
"""
benchmark_overhead.py - Benchmark Harness for Crash Archaeologist 2.0

Scope and Methodological Rules:
1. When full upstream instrumentation (Member 1's ring buffer and Member 2's
   LD_PRELOAD dynamic hooks) is integrated, this script runs baseline vs. instrumented
   compute/IO workloads to empirically compute:
       Overhead % = ((T_instrumented - T_baseline) / T_baseline) * 100
   Target: < 5.0% runtime overhead.

2. Current Integration Status:
   Members 1 and 2 have not yet committed the runtime hooking layer (mem_hook.c,
   thread_hook.c, syscall_hook.c, ipc_hook.c) or the main CLI driver.
   Per project policy, fabricated runtime overhead measurements are strictly forbidden.

3. Standalone Mode:
   This harness invokes the standalone analyzer latency benchmark (bin/bench_analyzer)
   to measure post-mortem analysis speed over an 8,192-event buffer, clearly documenting
   that this measures analysis speed, NOT runtime hook recording overhead.
"""

import os
import sys
import subprocess
import time
import shutil

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN_DIR = os.path.join(ROOT_DIR, "bin")

def run_standalone_analyzer_benchmark():
    bench_bin = os.path.join(BIN_DIR, "bench_analyzer.exe" if os.name == "nt" else "bench_analyzer")

    if not os.path.exists(bench_bin):
        print(f"[*] Compiling standalone analyzer benchmark...")
        # Compile bench_analyzer
        make_cmd = ["mingw32-make" if os.name == "nt" else "make", "bench_analyzer"]
        res = subprocess.run(make_cmd, cwd=ROOT_DIR, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[-] Compilation failed:\n{res.stderr}")
            return False

    print(f"\n[*] Executing Standalone Analyzer Benchmark: {bench_bin}")
    start = time.perf_counter()
    res = subprocess.run([bench_bin], capture_output=True, text=True)
    elapsed = time.perf_counter() - start

    print(res.stdout)
    if res.returncode != 0:
        print(f"[-] Benchmark exited with error code {res.returncode}:\n{res.stderr}")
        return False
    return True

def print_prerequisite_notice():
    print("""
================================================================================
           CRASH ARCHAEOLOGIST 2.0 - OVERHEAD BENCHMARK PROTOCOL
================================================================================
[STATUS] End-to-End Runtime Overhead Measurement: PENDING INTEGRATION

Integration Prerequisites:
  1. Member 1: Flight recorder preservation & CLI driver
     - src/buffer/ring_buffer.c (lock-free / atomic circular ring buffer)
     - src/collector/signal_handler.c (SA_SIGINFO fatal signal trap)
     - src/main.c (CLI runner)

  2. Member 2: Runtime Event Collection Hooks
     - src/collector/mem_hook.c (malloc/free/mmap interposition)
     - src/collector/thread_hook.c (pthread_create/mutex interposition)
     - src/collector/syscall_hook.c (open/close/read/write interposition)
     - src/collector/ipc_hook.c (pipe/socket interposition)

Methodology for Final <5% Evaluation (Once Upstream Integrates):
  - Workload: 1,000,000 synthetic memory allocations + multi-threaded matrix operations.
  - Baseline Run: Native execution without LD_PRELOAD interposition (T_base).
  - Instrumented Run: Execution with libcrash_hooks.so recording into ring buffer (T_record).
  - Calculation: Overhead % = ((T_record - T_base) / T_base) * 100.0
  - Target SLA: Strict runtime overhead < 5.0%.
================================================================================
""")

def main():
    print_prerequisite_notice()
    success = run_standalone_analyzer_benchmark()
    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()
