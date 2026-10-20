#include "buffer.h"
#include "collector.h"
#include "signals.h"
#include "engine.h"
#include "analyzer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

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
    printf("Usage: %s [options] [target_executable]\n\n", prog_name);
    printf("Execution Modes:\n");
    printf("  (no args)             Launch interactive live crash reconstruction menu\n");
    printf("  --interactive, -i     Launch interactive live crash reconstruction menu\n");
    printf("  --run <scenario>      Execute a specific live crash reconstruction:\n");
    printf("                          uaf        - Use-After-Free heap fault\n");
    printf("                          segfault   - NULL/unmapped memory dereference\n");
    printf("                          deadlock   - Multi-threaded AB-BA cyclic lockup\n");
    printf("                          exhaustion - File descriptor (EMFILE) exhaustion\n");
    printf("                          leak       - Unbounded memory allocation leak\n");
    printf("                          ipc        - Broken pipe / socket disconnect\n");
    printf("  --demo                Run automated flight recorder demonstration\n");
    printf("  --buffer-size <N>     Configure ring buffer slot capacity (default: 8192)\n");
    printf("  --version, -v         Display version information\n");
    printf("  --help, -h            Display this help message\n");
    printf("\n");
}

static void save_reports(const crash_analysis_result_t *result, const char *prefix) {
    char json_path[128];
    char txt_path[128];
    snprintf(json_path, sizeof(json_path), "reports/%s_crash_report.json", prefix);
    snprintf(txt_path, sizeof(txt_path), "reports/%s_crash_report.txt", prefix);
    
    generate_crash_report(result, NULL); /* Output to console */
    generate_crash_report(result, json_path);
    generate_crash_report(result, txt_path);
    
    printf("\n[SUCCESS] Crash reports saved to:\n");
    printf("  -> JSON : %s\n", json_path);
    printf("  -> Text : %s\n", txt_path);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 1: Use-After-Free (UAF)                                      */
/* -------------------------------------------------------------------------- */
static void run_live_uaf(size_t capacity) {
    print_banner();
    printf("[SCENARIO 1: USE-AFTER-FREE (UAF) RECONSTRUCTION]\n");
    printf("[1/5] Initializing flight recorder with capacity = %zu events...\n", capacity);
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    printf("[2/5] Simulating live thread activity and dynamic heap allocation...\n");
    uintptr_t target_chunk = 0x0055F000;
    
    /* Event 1: Thread creation */
    crash_event_t ev1 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)2001,
        .tid = 101,
        .category = EVENT_CAT_PROCESS_THREAD,
        .type = EVENT_THREAD_CREATE,
        .label = "worker_thread_init"
    };
    ring_buffer_record(&rb, &ev1);
    printf("      -> [Seq 1] TID 101: worker_thread_init\n");
    
    /* Event 2: malloc(1024) */
    crash_event_t ev2 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)2001,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = target_chunk,
        .size = 1024,
        .label = "malloc(1024)"
    };
    ring_buffer_record(&rb, &ev2);
    printf("      -> [Seq 2] TID 101: malloc(1024) at address 0x%08llx\n", (unsigned long long)target_chunk);
    
    /* Event 3: Free the memory block */
    printf("[3/5] Simulating deallocation: free(0x%08llx)...\n", (unsigned long long)target_chunk);
    crash_event_t ev3 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)2001,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_FREE,
        .addr = target_chunk,
        .size = 1024,
        .label = "free(0x0055F000)"
    };
    ring_buffer_record(&rb, &ev3);
    printf("      -> [Seq 3] TID 101: free(0x%08llx) marked deallocated\n", (unsigned long long)target_chunk);
    
    /* Event 4: Another thread attempts to access the freed chunk */
    uintptr_t fault_addr = target_chunk + 64;
    printf("[4/5] Thread 102 dereferences freed pointer at 0x%08llx -> FATAL CRASH!\n", (unsigned long long)fault_addr);
    crash_event_t crash_ev = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)2001,
        .tid = 102,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = fault_addr,
        .fd_or_status = SIGSEGV,
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &crash_ev);
    
    /* Flight recorder instantly freezes */
    ring_buffer_freeze(&rb);
    printf("[5/5] Flight recorder FROZEN. Preserved %zu events in circular buffer.\n",
           ring_buffer_get_count(&rb));
    
    printf("\n[*] Running Person 3 Causal Reconstruction Engine...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_uaf");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 2: Segmentation Fault (NULL Pointer)                         */
/* -------------------------------------------------------------------------- */
static void run_live_segfault(size_t capacity) {
    print_banner();
    printf("[SCENARIO 2: SEGMENTATION FAULT (NULL DEREFERENCE)]\n");
    printf("[1/4] Arming flight recorder (capacity = %zu slots)...\n", capacity);
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    /* Event 1: Initialization */
    crash_event_t ev1 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)3001,
        .tid = 10,
        .category = EVENT_CAT_PROCESS_THREAD,
        .type = EVENT_THREAD_CREATE,
        .label = "main_thread_start"
    };
    ring_buffer_record(&rb, &ev1);
    
    /* Event 2: Syscall open */
    crash_event_t ev2 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)3001,
        .tid = 10,
        .category = EVENT_CAT_SYSCALL,
        .type = EVENT_SYS_OPEN,
        .fd_or_status = 3,
        .label = "open(\"input.dat\")"
    };
    ring_buffer_record(&rb, &ev2);
    
    /* Event 3: Crash at NULL pointer */
    printf("[2/4] Worker thread attempts to read from NULL pointer (0x0)...\n");
    crash_event_t crash_ev = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)3001,
        .tid = 11,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = (uintptr_t)0x0,
        .fd_or_status = SIGSEGV,
        .label = "SIGSEGV @ 0x0"
    };
    ring_buffer_record(&rb, &crash_ev);
    ring_buffer_freeze(&rb);
    
    printf("[3/4] Flight recorder FROZEN. State preserved.\n");
    printf("[4/4] Reconstructing failure root cause...\n");
    
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_segfault");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 3: Multi-Threaded AB-BA Deadlock                             */
/* -------------------------------------------------------------------------- */
static void run_live_deadlock(size_t capacity) {
    print_banner();
    printf("[SCENARIO 3: MULTI-THREADED AB-BA DEADLOCK CYCLE DETECTION]\n");
    printf("[1/4] Arming flight recorder...\n");
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    uintptr_t mutex_a = 0x00A000;
    uintptr_t mutex_b = 0x00B000;
    
    printf("[2/4] Simulating concurrent threads acquiring locks in reverse order:\n");
    printf("      Thread 101: locks Mutex A, then requests Mutex B\n");
    printf("      Thread 102: locks Mutex B, then requests Mutex A\n");
    
    /* Thread 101 acquires Mutex A */
    crash_event_t ev1 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)4001,
        .tid = 101,
        .category = EVENT_CAT_IPC,
        .type = EVENT_SYNC_MUTEX_LOCK,
        .addr = mutex_a,
        .label = "lock(Mutex_A)"
    };
    ring_buffer_record(&rb, &ev1);
    
    /* Thread 102 acquires Mutex B */
    crash_event_t ev2 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)4001,
        .tid = 102,
        .category = EVENT_CAT_IPC,
        .type = EVENT_SYNC_MUTEX_LOCK,
        .addr = mutex_b,
        .label = "lock(Mutex_B)"
    };
    ring_buffer_record(&rb, &ev2);
    
    /* Thread 101 attempts to acquire Mutex B (held by 102) -> BLOCKS */
    crash_event_t ev3 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)4001,
        .tid = 101,
        .category = EVENT_CAT_IPC,
        .type = EVENT_SYNC_MUTEX_LOCK,
        .addr = mutex_b,
        .label = "lock(Mutex_B) [WAITING]"
    };
    ring_buffer_record(&rb, &ev3);
    
    /* Thread 102 attempts to acquire Mutex A (held by 101) -> BLOCKS (CYCLE FORMED!) */
    crash_event_t ev4 = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)4001,
        .tid = 102,
        .category = EVENT_CAT_IPC,
        .type = EVENT_SYNC_MUTEX_LOCK,
        .addr = mutex_a,
        .label = "lock(Mutex_A) [WAITING]"
    };
    ring_buffer_record(&rb, &ev4);
    
    printf("[3/4] Deadlock formed! Flight recorder frozen.\n");
    ring_buffer_freeze(&rb);
    
    printf("[4/4] Evaluating Wait-For-Graph for cyclic dependency...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_deadlock");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 4: Resource Exhaustion (EMFILE)                              */
/* -------------------------------------------------------------------------- */
static void run_live_exhaustion(size_t capacity) {
    print_banner();
    printf("[SCENARIO 4: RESOURCE EXHAUSTION (FILE DESCRIPTOR EMFILE)]\n");
    printf("[1/3] Arming flight recorder...\n");
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    printf("[2/3] Simulating descriptor leak: opening files without closing...\n");
    for (int fd = 3; fd < 20; fd++) {
        crash_event_t ev = {
            .timestamp_ns = crash_get_timestamp_ns(),
            .pid = (uint32_t)5001,
            .tid = 1,
            .category = EVENT_CAT_SYSCALL,
            .type = EVENT_SYS_OPEN,
            .fd_or_status = fd,
            .label = "open(\"stream.log\")"
        };
        ring_buffer_record(&rb, &ev);
    }
    
    /* Syscall fails with EMFILE (-24) */
    printf("      -> Syscall open() fails with return code -1 (errno: EMFILE - Too many open files)\n");
    crash_event_t ev_fail = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)5001,
        .tid = 1,
        .category = EVENT_CAT_SYSCALL,
        .type = EVENT_SYS_OPEN,
        .fd_or_status = -24, /* EMFILE */
        .label = "open() -> EMFILE"
    };
    ring_buffer_record(&rb, &ev_fail);
    ring_buffer_freeze(&rb);
    
    printf("[3/3] Analyzing historical syscall failure pattern...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_exhaustion");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 5: Memory Leak                                               */
/* -------------------------------------------------------------------------- */
static void run_live_memleak(size_t capacity) {
    print_banner();
    printf("[SCENARIO 5: MEMORY LEAK (MONOTONIC HEAP ALLOCATION)]\n");
    printf("[1/3] Arming flight recorder...\n");
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    printf("[2/3] Simulating unreleased allocations across multiple epochs...\n");
    for (int i = 0; i < 30; i++) {
        crash_event_t ev = {
            .timestamp_ns = crash_get_timestamp_ns(),
            .pid = (uint32_t)6001,
            .tid = 2,
            .category = EVENT_CAT_MEMORY,
            .type = EVENT_MEM_ALLOC,
            .addr = (uintptr_t)(0x00700000 + i * 4096),
            .size = 4096,
            .label = "malloc(4096)"
        };
        ring_buffer_record(&rb, &ev);
    }
    
    ring_buffer_freeze(&rb);
    printf("[3/3] Reconstructing memory footprint growth...\n");
    
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_memleak");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 6: IPC Failure (Broken Pipe)                                 */
/* -------------------------------------------------------------------------- */
static void run_live_ipc(size_t capacity) {
    print_banner();
    printf("[SCENARIO 6: IPC FAILURE (BROKEN PIPE / SIGPIPE)]\n");
    printf("[1/3] Arming flight recorder...\n");
    
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    printf("[2/3] Simulating pipe write to disconnected peer...\n");
    crash_event_t ev_write = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)7001,
        .tid = 1,
        .category = EVENT_CAT_IPC,
        .type = EVENT_IPC_PIPE_WRITE,
        .fd_or_status = -32, /* EPIPE */
        .size = 128,
        .label = "pipe_write() -> EPIPE"
    };
    ring_buffer_record(&rb, &ev_write);
    
    crash_event_t ev_sig = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)7001,
        .tid = 1,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .fd_or_status = 13, /* SIGPIPE */
        .label = "SIGPIPE"
    };
    ring_buffer_record(&rb, &ev_sig);
    ring_buffer_freeze(&rb);
    
    printf("[3/3] Analyzing IPC event sequence...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "live_ipc");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* LIVE SCENARIO 7: Custom Interactive Sandbox                                */
/* -------------------------------------------------------------------------- */
static void run_custom_sandbox(size_t capacity) {
    print_banner();
    printf("[CUSTOM CRASH SANDBOX — CREATE YOUR OWN SCENARIO]\n\n");
    
    char alloc_str[64];
    printf("Enter memory chunk size to allocate in bytes (e.g. 512, 2048): ");
    fflush(stdout);
    if (!fgets(alloc_str, sizeof(alloc_str), stdin)) return;
    size_t chunk_size = (size_t)strtoul(alloc_str, NULL, 10);
    if (chunk_size == 0) chunk_size = 512;
    
    char free_choice[64];
    printf("Simulate free() on this pointer before accessing? (y/n, default: y): ");
    fflush(stdout);
    if (!fgets(free_choice, sizeof(free_choice), stdin)) return;
    bool do_free = (free_choice[0] != 'n' && free_choice[0] != 'N');
    
    char offset_str[64];
    printf("Enter fault offset inside chunk (e.g. 0, 16, 64): ");
    fflush(stdout);
    if (!fgets(offset_str, sizeof(offset_str), stdin)) return;
    size_t offset = (size_t)strtoul(offset_str, NULL, 10);
    
    printf("\n[*] Arming flight recorder (capacity = %zu)...\n", capacity);
    ring_buffer_t rb;
    ring_buffer_init(&rb, capacity);
    signal_handler_install(&rb);
    
    uintptr_t chunk_addr = 0x00880000;
    
    /* 1. Allocation */
    crash_event_t ev_alloc = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)8001,
        .tid = 50,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = chunk_addr,
        .size = chunk_size,
        .label = "custom_malloc"
    };
    ring_buffer_record(&rb, &ev_alloc);
    printf("[+] Recorded custom allocation: %zu bytes at 0x%08llx\n",
           chunk_size, (unsigned long long)chunk_addr);
    
    /* 2. Optional Free */
    if (do_free) {
        crash_event_t ev_free = {
            .timestamp_ns = crash_get_timestamp_ns(),
            .pid = (uint32_t)8001,
            .tid = 50,
            .category = EVENT_CAT_MEMORY,
            .type = EVENT_MEM_FREE,
            .addr = chunk_addr,
            .size = chunk_size,
            .label = "custom_free"
        };
        ring_buffer_record(&rb, &ev_free);
        printf("[+] Recorded custom free at 0x%08llx\n", (unsigned long long)chunk_addr);
    }
    
    /* 3. Crash event */
    uintptr_t target_deref = chunk_addr + offset;
    printf("[!] Triggering crash dereference at 0x%08llx...\n", (unsigned long long)target_deref);
    crash_event_t ev_crash = {
        .timestamp_ns = crash_get_timestamp_ns(),
        .pid = (uint32_t)8001,
        .tid = 51,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = target_deref,
        .fd_or_status = SIGSEGV,
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &ev_crash);
    ring_buffer_freeze(&rb);
    
    printf("[*] Running Causal Reconstruction on your custom event stream...\n");
    crash_analysis_result_t result;
    if (analyze_crash(&rb, &result) == 0) {
        save_reports(&result, "custom_sandbox");
    }
    
    signal_handler_uninstall();
    ring_buffer_destroy(&rb);
}

/* -------------------------------------------------------------------------- */
/* INTERACTIVE MENU INTERFACE                                                 */
/* -------------------------------------------------------------------------- */
static void run_interactive_menu(size_t capacity) {
    char input[64];
    while (1) {
        print_banner();
        printf("Interactive Failure Reconstruction Menu:\n");
        printf("  [1] Live Use-After-Free (UAF) Heap Dereference\n");
        printf("  [2] Live Segmentation Fault (NULL Pointer Dereference)\n");
        printf("  [3] Live Multi-Threaded AB-BA Deadlock Cycle Detection\n");
        printf("  [4] Live Resource Exhaustion (File Descriptor EMFILE)\n");
        printf("  [5] Live Memory Leak (Monotonic Allocation Delta)\n");
        printf("  [6] Live IPC Failure (Broken Pipe / EPIPE)\n");
        printf("  [7] Custom Interactive Sandbox (Your Own Parameters)\n");
        printf("  [8] Run System Benchmark & Verify 27 Unit Tests\n");
        printf("  [0] Exit\n");
        printf("-------------------------------------------------------------------\n");
        printf("Select an option (0-8): ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        int choice = atoi(input);
        if (choice == 0 && input[0] == '0') {
            printf("Exiting Crash Archaeologist.\n");
            break;
        }
        
        switch (choice) {
            case 1: run_live_uaf(capacity); break;
            case 2: run_live_segfault(capacity); break;
            case 3: run_live_deadlock(capacity); break;
            case 4: run_live_exhaustion(capacity); break;
            case 5: run_live_memleak(capacity); break;
            case 6: run_live_ipc(capacity); break;
            case 7: run_custom_sandbox(capacity); break;
            case 8:
                printf("\n[*] Running All System Verification Tests...\n");
#ifdef _WIN32
                system("bin\\test_ring_buffer.exe");
                system("bin\\test_signal_handler.exe");
                system("bin\\test_analyzer.exe");
                system("bin\\test_causal_graph.exe");
                system("bin\\bench_analyzer.exe");
#else
                system("./bin/test_ring_buffer");
                system("./bin/test_signal_handler");
                system("./bin/test_analyzer");
                system("./bin/test_causal_graph");
                system("./bin/bench_analyzer");
#endif
                break;
            default:
                printf("Invalid selection. Please choose an option between 0 and 8.\n");
                break;
        }
        
        printf("\nPress Enter to return to menu...");
        fflush(stdout);
        getchar();
    }
}

/* -------------------------------------------------------------------------- */
/* MAIN ENTRYPOINT                                                            */
/* -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    size_t buffer_size = DEFAULT_RING_BUFFER_CAPACITY;
    
    if (argc < 2) {
        run_interactive_menu(buffer_size);
        return 0;
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("Crash Archaeologist version %s\n", VERSION_STRING);
            return 0;
        } else if (strcmp(argv[i], "--interactive") == 0 || strcmp(argv[i], "-i") == 0) {
            run_interactive_menu(buffer_size);
            return 0;
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            buffer_size = (size_t)strtoul(argv[++i], NULL, 10);
            if (buffer_size == 0) buffer_size = DEFAULT_RING_BUFFER_CAPACITY;
        } else if (strcmp(argv[i], "--demo") == 0) {
            run_live_uaf(buffer_size);
            return 0;
        } else if (strcmp(argv[i], "--run") == 0 && i + 1 < argc) {
            const char *scenario = argv[++i];
            if (strcmp(scenario, "uaf") == 0) run_live_uaf(buffer_size);
            else if (strcmp(scenario, "segfault") == 0) run_live_segfault(buffer_size);
            else if (strcmp(scenario, "deadlock") == 0) run_live_deadlock(buffer_size);
            else if (strcmp(scenario, "exhaustion") == 0) run_live_exhaustion(buffer_size);
            else if (strcmp(scenario, "leak") == 0 || strcmp(scenario, "memleak") == 0) run_live_memleak(buffer_size);
            else if (strcmp(scenario, "ipc") == 0) run_live_ipc(buffer_size);
            else if (strcmp(scenario, "sandbox") == 0) run_custom_sandbox(buffer_size);
            else {
                fprintf(stderr, "Unknown scenario: %s\n", scenario);
                print_usage(argv[0]);
                return 1;
            }
            return 0;
        } else {
            /* Execute target program as a monitored child */
            printf("[INFO] Running target executable '%s' under Crash Archaeologist...\n", argv[i]);
            int status = system(argv[i]);
            printf("[INFO] Target program exited with code %d\n", status);
            return status;
        }
    }
    
    return 0;
}
