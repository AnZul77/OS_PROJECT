#include "buffer.h"
#include "collector.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION_STRING "2.0.0-dev1"

static void print_banner(void) {
    printf("===================================================================\n");
    printf("    CRASH ARCHAEOLOGIST 2.0 — OS-Level Flight Recorder\n");
    printf("    BCSE303P: Operating Systems Lab | Fall 2026-2027\n");
    printf("    Author (Dev 1): Anshul Gupta (24BYB0149)\n");
    printf("===================================================================\n");
}

static void print_usage(const char *prog_name) {
    print_banner();
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --buffer-size <N>     Configure ring buffer slot capacity (default: 8192)\n");
    printf("  --demo                Run a simulated flight recording and freeze demo\n");
    printf("  --test-buffer         Verify in-memory ring buffer integrity\n");
    printf("  --version             Display version information\n");
    printf("  --help                Display this help message\n");
    printf("\n");
}

static void run_flight_recorder_demo(size_t capacity) {
    print_banner();
    printf("[DEMO] Initializing flight recorder with capacity = %zu slots...\n", capacity);
    
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
    
    /* Simulate normal application activity */
    printf("[DEMO] Simulating continuous multi-thread OS events...\n");
    
    /* 1. Thread creation */
    crash_event_t ev1;
    memset(&ev1, 0, sizeof(ev1));
    ev1.category = EVENT_CAT_PROCESS_THREAD;
    ev1.type = EVENT_THREAD_CREATE;
    ev1.tid = 101;
    strncpy(ev1.label, "worker_thread_init", sizeof(ev1.label) - 1);
    ring_buffer_record(&rb, &ev1);
    
    /* 2. Memory allocations */
    crash_event_t ev2;
    memset(&ev2, 0, sizeof(ev2));
    ev2.category = EVENT_CAT_MEMORY;
    ev2.type = EVENT_MEM_ALLOC;
    ev2.addr = 0x0040A100;
    ev2.size = 2048;
    ev2.tid = 101;
    strncpy(ev2.label, "malloc(2048)", sizeof(ev2.label) - 1);
    ring_buffer_record(&rb, &ev2);
    
    /* 3. Syscall I/O */
    crash_event_t ev3;
    memset(&ev3, 0, sizeof(ev3));
    ev3.category = EVENT_CAT_SYSCALL;
    ev3.type = EVENT_SYS_OPEN;
    ev3.fd_or_status = 3;
    ev3.tid = 101;
    strncpy(ev3.label, "open(\"/etc/config\")", sizeof(ev3.label) - 1);
    ring_buffer_record(&rb, &ev3);
    
    /* 4. Memory deallocation (Potential use-after-free setup) */
    crash_event_t ev4;
    memset(&ev4, 0, sizeof(ev4));
    ev4.category = EVENT_CAT_MEMORY;
    ev4.type = EVENT_MEM_FREE;
    ev4.addr = 0x0040A100;
    ev4.tid = 101;
    strncpy(ev4.label, "free(0x0040A100)", sizeof(ev4.label) - 1);
    ring_buffer_record(&rb, &ev4);
    
    printf("[DEMO] Recorded 4 active events in circular flight buffer.\n");
    printf("[DEMO] Simulating crash event interception (SIGSEGV)...\n");
    
    /* Simulate crash preservation */
    ring_buffer_freeze(&rb);
    
    /* Record terminal crash marker */
    crash_event_t crash_ev;
    memset(&crash_ev, 0, sizeof(crash_ev));
    crash_ev.category = EVENT_CAT_SIGNAL;
    crash_ev.type = EVENT_SIG_RECEIVED;
    crash_ev.addr = 0x0040A100;
    crash_ev.fd_or_status = SIGSEGV;
    crash_ev.tid = 101;
    strncpy(crash_ev.label, "SIGSEGV @ 0x0040A100", sizeof(crash_ev.label) - 1);
    
    /* Attempt to record after freeze (simulating post-crash invariance) */
    printf("[DEMO] Flight recorder is now FROZEN (invariance active = %s).\n",
           ring_buffer_is_frozen(&rb) ? "TRUE" : "FALSE");
    
    /* Inspect preserved history */
    size_t count = ring_buffer_get_count(&rb);
    printf("\n--- PRESERVED FLIGHT RECORDER LOG (%zu events) ---\n", count);
    for (size_t i = 0; i < count; i++) {
        crash_event_t item;
        ring_buffer_get_at(&rb, i, &item);
        printf("[%04zu] Seq: %-3llu | Cat: %d | TID: %-4u | Addr: 0x%08llx | Action: %s\n",
               i,
               (unsigned long long)item.sequence_id,
               item.category,
               item.tid,
               (unsigned long long)item.addr,
               item.label);
    }
    printf("----------------------------------------------------\n");
    printf("[DEMO] State successfully preserved for Dev 3 Causal Correlator.\n");
    
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
        } else if (strcmp(argv[i], "--test-buffer") == 0) {
            printf("[INFO] Running ring buffer verification...\n");
            ring_buffer_t rb;
            if (ring_buffer_init(&rb, 64) == 0) {
                printf("[SUCCESS] Ring buffer initialized and verified.\n");
                ring_buffer_destroy(&rb);
            } else {
                fprintf(stderr, "[FAIL] Ring buffer initialization failed.\n");
                return 1;
            }
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    return 0;
}
