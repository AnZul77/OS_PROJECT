/*
 * bench_analyzer.c - Standalone Analyzer Latency & Throughput Benchmark
 * Project: Crash Archaeologist 2.0
 *
 * IMPORTANT SCOPE NOTICE:
 * This benchmark measures ONLY post-crash analysis latency and throughput
 * of Person 3's causal reconstruction engine (correlator, causal_graph, root_cause).
 * It does NOT measure runtime flight recording overhead (<5% target), which
 * requires Member 1's ring buffer and Member 2's dynamic hooking layer.
 */

#include "engine.h"
#include "buffer.h"
#include "event.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define BENCH_CAPACITY 8192
#define BENCH_ITERATIONS 100

static double get_time_sec(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

int main(void) {
    printf("================================================================================\n");
    printf("        CRASH ARCHAEOLOGIST 2.0 - STANDALONE ANALYZER BENCHMARK\n");
    printf("================================================================================\n");
    printf("[NOTICE] This benchmark evaluates Person 3 post-crash analysis latency.\n");
    printf("[NOTICE] Steady-state recording overhead (<5%%) is pending Member 1/2 hooks.\n");
    printf("--------------------------------------------------------------------------------\n");

    ring_buffer_t rb;
    if (ring_buffer_init(&rb, BENCH_CAPACITY) != 0) {
        fprintf(stderr, "Failed to initialize ring buffer.\n");
        return 1;
    }

    /* Populate ring buffer with a synthetic workload history ending in UAF */
    uintptr_t target_chunk = 0x7FFF1000;
    for (size_t i = 1; i < BENCH_CAPACITY - 2; i++) {
        crash_event_t ev = {
            .sequence_id = i,
            .timestamp_ns = i * 100,
            .pid = 1000,
            .tid = (uint32_t)(1 + (i % 4)),
            .category = EVENT_CAT_SYSCALL,
            .type = EVENT_SYS_READ,
            .addr = 0x10000 + (i * 8),
            .size = 64,
            .fd_or_status = 3,
            .label = "sys_read"
        };
        ring_buffer_record(&rb, &ev);
    }

    /* Terminal UAF sequence at end of history */
    crash_event_t ev_alloc = {
        .sequence_id = BENCH_CAPACITY - 2,
        .timestamp_ns = (BENCH_CAPACITY - 2) * 100,
        .pid = 1000,
        .tid = 1,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = target_chunk,
        .size = 512,
        .label = "malloc"
    };
    crash_event_t ev_free = {
        .sequence_id = BENCH_CAPACITY - 1,
        .timestamp_ns = (BENCH_CAPACITY - 1) * 100,
        .pid = 1000,
        .tid = 1,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_FREE,
        .addr = target_chunk,
        .size = 512,
        .label = "free"
    };
    crash_event_t ev_crash = {
        .sequence_id = BENCH_CAPACITY,
        .timestamp_ns = BENCH_CAPACITY * 100,
        .pid = 1000,
        .tid = 2,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = target_chunk + 16,
        .fd_or_status = 11, /* SIGSEGV */
        .label = "SIGSEGV"
    };

    ring_buffer_record(&rb, &ev_alloc);
    ring_buffer_record(&rb, &ev_free);
    ring_buffer_record(&rb, &ev_crash);
    ring_buffer_freeze(&rb);

    printf("[BENCHMARK] Ring buffer populated: %zu events (capacity %d).\n",
           ring_buffer_get_count(&rb), BENCH_CAPACITY);
    printf("[BENCHMARK] Executing %d complete analysis iterations...\n", BENCH_ITERATIONS);

    crash_analysis_result_t result;
    double start_time = get_time_sec();

    for (int it = 0; it < BENCH_ITERATIONS; it++) {
        if (analyze_crash(&rb, &result) != 0) {
            fprintf(stderr, "analyze_crash failed at iteration %d\n", it);
            ring_buffer_destroy(&rb);
            return 1;
        }
    }

    double end_time = get_time_sec();
    double total_sec = end_time - start_time;
    double avg_ms_per_run = (total_sec / BENCH_ITERATIONS) * 1000.0;
    double runs_per_sec = (double)BENCH_ITERATIONS / total_sec;

    printf("--------------------------------------------------------------------------------\n");
    printf("Reconstructed Diagnosis : %d (UAF = %d)\n", result.probable_cause, CAUSE_USE_AFTER_FREE);
    printf("Confidence Level        : %.1f%%\n", result.confidence_level * 100.0);
    printf("Causal Chain Length     : %zu events\n", result.causal_chain_len);
    printf("Total Elapsed Time      : %.4f seconds (%d runs)\n", total_sec, BENCH_ITERATIONS);
    printf("Average Analysis Time   : %.3f ms (%.1f microseconds per 8K-event buffer)\n",
           avg_ms_per_run, avg_ms_per_run * 1000.0);
    printf("Throughput              : %.1f full post-crash reconstructions / sec\n", runs_per_sec);
    printf("================================================================================\n");

    ring_buffer_destroy(&rb);
    return 0;
}
