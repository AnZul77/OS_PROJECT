#include "buffer.h"
#include "collector.h"
#include "signals.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION_STRING "2.0.0-integrated"

static void print_banner(void) {
    printf("===================================================================\n");
    printf("    CRASH ARCHAEOLOGIST 2.0 — OS-Level Flight Recorder\n");
    printf("    BCSE303P: Operating Systems Lab | Fall 2026-2027\n");
    printf("    Team: Anshul Gupta (149), Adharshini Rajesh (150), Rohan Nayak (132)\n");
    printf("===================================================================\n");
}

static void print_usage(const char *prog_name) {
    print_banner();
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --buffer-size <N>     Configure ring buffer slot capacity (default: 8192)\n");
    printf("  --demo                Run full end-to-end flight recording & causal analysis demo\n");
    printf("  --test-all            Run all unit tests across all team subsystems\n");
    printf("  --version             Display version information\n");
    printf("  --help                Display this help message\n");
    printf("\n");
}

static void run_flight_recorder_demo(size_t capacity) {
    print_banner();
    printf("[DEMO] Initializing circular flight recorder (capacity = %zu slots)...\n", capacity);
    
    ring_buffer_t rb;
    if (ring_buffer_init(&rb, capacity) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize ring buffer.\n");
        return;
    }
    
    /* Install signal handler */
    if (signal_handler_install(&rb) != 0) {
        fprintf(stderr, "[ERROR] Failed to install signal handler.\n");
        ring_buffer_destroy(&rb);
        return;
    }
    printf("[DEMO] Signal preservation subsystem armed (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL).\n");
    
    /* Simulate multi-thread OS events */
    printf("[DEMO] Simulating continuous multithreaded OS events...\n");
    
    /* 1. Thread creation */
    crash_event_t ev1 = {
        .sequence_id = 0,
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)1001,
        .tid = 101,
        .category = EVENT_CAT_PROCESS_THREAD,
        .type = EVENT_THREAD_CREATE,
        .label = "worker_thread_init"
    };
    ring_buffer_record(&rb, &ev1);
    
    /* 2. Memory allocations */
    uintptr_t target_chunk = 0x0040A100;
    crash_event_t ev2 = {
        .sequence_id = 0,
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)1001,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = target_chunk,
        .size = 2048,
        .label = "malloc(2048)"
    };
    ring_buffer_record(&rb, &ev2);
    
    /* 3. Syscall I/O */
    crash_event_t ev3 = {
        .sequence_id = 0,
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)1001,
        .tid = 101,
        .category = EVENT_CAT_SYSCALL,
        .type = EVENT_SYS_OPEN,
        .fd_or_status = 3,
        .label = "open(\"/etc/config\")"
    };
    ring_buffer_record(&rb, &ev3);
    
    /* 4. Memory deallocation (creating Use-After-Free condition) */
    crash_event_t ev4 = {
        .sequence_id = 0,
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)1001,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_FREE,
        .addr = target_chunk,
        .size = 2048,
        .label = "free(0x0040A100)"
    };
    ring_buffer_record(&rb, &ev4);
    
    printf("[DEMO] Recorded 4 active events in circular flight buffer.\n");
    printf("[DEMO] Simulating fatal crash event: SIGSEGV accessing freed address 0x%08llx...\n",
           (unsigned long long)(target_chunk + 16));
    
    /* Record terminal crash event */
    crash_event_t crash_ev = {
        .sequence_id = 0,
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)1001,
        .tid = 101,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = target_chunk + 16,
        .fd_or_status = SIGSEGV,
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &crash_ev);

    /* Simulate crash preservation */
    ring_buffer_freeze(&rb);
    
    printf("[DEMO] Flight recorder is now FROZEN (invariance active = %s).\n",
           ring_buffer_is_frozen(&rb) ? "TRUE" : "FALSE");
    
    /* Step 5: Causal Analysis Engine Execution */
    printf("\n[ANALYZE] Invoking Person 3 Causal Reconstruction Engine...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        printf("[ANALYZE] Causal correlation complete! Generating diagnostic reports...\n\n");
        generate_crash_report(&result, NULL); /* Output to console */
        generate_crash_report(&result, "reports/demo_crash_report.json");
        generate_crash_report(&result, "reports/demo_crash_report.txt");
        printf("[SUCCESS] Written reports to reports/demo_crash_report.json and .txt\n");
    } else {
        fprintf(stderr, "[ERROR] Causal analysis failed.\n");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

int main(int argc, char *argv[]) {
    size_t buffer_size = DEFAULT_RING_BUFFER_CAPACITY;
    
    if (argc < 2) {
        run_flight_recorder_demo(buffer_size);
        return 0;
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("Crash Archaeologist version %s\n", VERSION_STRING);
            return 0;
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            buffer_size = (size_t)strtoul(argv[++i], NULL, 10);
            if (buffer_size == 0) buffer_size = DEFAULT_RING_BUFFER_CAPACITY;
        } else if (strcmp(argv[i], "--demo") == 0) {
            run_flight_recorder_demo(buffer_size);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    return 0;
}
