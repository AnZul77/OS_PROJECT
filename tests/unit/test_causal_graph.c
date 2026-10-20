#include "engine.h"
#include "buffer.h"
#include "event.h"
#include "analyzer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_graph_tests_run = 0;
static int g_graph_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[-] ASSERTION FAILED: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        return -1; \
    } \
} while(0)

#define RUN_GRAPH_TEST(fn) do { \
    g_graph_tests_run++; \
    printf("[*] Running %s...\n", #fn); \
    if (fn() == 0) { \
        printf("[+] %s PASSED\n", #fn); \
        g_graph_tests_passed++; \
    } else { \
        printf("[-] %s FAILED\n", #fn); \
    } \
} while(0)

/* Test 1: Empty event history */
static int test_empty_event_history(void) {
    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(NULL, 0, &res);
    TEST_ASSERT(rc == 0, "causal_graph_detect_deadlock should return 0 on NULL events");
    TEST_ASSERT(res.cycle_detected == false, "cycle_detected must be false for empty history");
    TEST_ASSERT(res.cycle_length == 0, "cycle_length must be 0");

    crash_event_t empty_ev[1];
    rc = causal_graph_detect_deadlock(empty_ev, 0, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock should return 0 on count == 0");
    TEST_ASSERT(res.cycle_detected == false, "cycle_detected must be false for count == 0");

    return 0;
}

/* Test 2: Two-thread AB-BA cycle */
static int test_two_thread_abba_cycle(void) {
    uintptr_t lock_a = 0xAAAA0000;
    uintptr_t lock_b = 0xBBBB0000;

    crash_event_t events[4] = {
        /* Seq 1: Thread 1 acquires Lock A */
        {
            .sequence_id = 1,
            .timestamp_ns = 1000,
            .pid = 10,
            .tid = 1,
            .category = EVENT_CAT_IPC,
            .type = EVENT_SYNC_MUTEX_LOCK,
            .addr = lock_a,
            .label = "lock_a"
        },
        /* Seq 2: Thread 2 acquires Lock B */
        {
            .sequence_id = 2,
            .timestamp_ns = 2000,
            .pid = 10,
            .tid = 2,
            .category = EVENT_CAT_IPC,
            .type = EVENT_SYNC_MUTEX_LOCK,
            .addr = lock_b,
            .label = "lock_b"
        },
        /* Seq 3: Thread 1 requests Lock B (blocked by Thread 2) */
        {
            .sequence_id = 3,
            .timestamp_ns = 3000,
            .pid = 10,
            .tid = 1,
            .category = EVENT_CAT_IPC,
            .type = EVENT_SYNC_MUTEX_LOCK,
            .addr = lock_b,
            .label = "lock_b"
        },
        /* Seq 4: Thread 2 requests Lock A (blocked by Thread 1) */
        {
            .sequence_id = 4,
            .timestamp_ns = 4000,
            .pid = 10,
            .tid = 2,
            .category = EVENT_CAT_IPC,
            .type = EVENT_SYNC_MUTEX_LOCK,
            .addr = lock_a,
            .label = "lock_a"
        }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 4, &res);
    TEST_ASSERT(rc == 0, "causal_graph_detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == true, "cycle must be detected for AB-BA sequence");
    TEST_ASSERT(res.cycle_length == 2, "cycle length should be 2 for 2-thread AB-BA deadlock");
    TEST_ASSERT(strlen(res.description) > 0, "description should contain cycle information");

    /* Verify cycle contains both Thread 1 and Thread 2 */
    bool has_t1 = (res.cycle_tids[0] == 1 || res.cycle_tids[1] == 1);
    bool has_t2 = (res.cycle_tids[0] == 2 || res.cycle_tids[1] == 2);
    TEST_ASSERT(has_t1 && has_t2, "cycle must contain both thread 1 and thread 2");

    return 0;
}

/* Test 3: Non-cyclic lock sequence (correct lock ordering or sequential acquisition) */
static int test_non_cyclic_lock_sequence(void) {
    uintptr_t lock_a = 0xAAAA0000;
    uintptr_t lock_b = 0xBBBB0000;

    /* Both threads acquire Lock A then Lock B in consistent hierarchy, then unlock */
    crash_event_t events[8] = {
        /* Thread 1: Lock A, Lock B, Unlock B, Unlock A */
        { .sequence_id = 1, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 2, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b },
        { .sequence_id = 3, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_b },
        { .sequence_id = 4, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a },

        /* Thread 2: Lock A, Lock B, Unlock B, Unlock A */
        { .sequence_id = 5, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 6, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b },
        { .sequence_id = 7, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_b },
        { .sequence_id = 8, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 8, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == false, "must not detect cycle in sequential non-cyclic lock sequence");
    TEST_ASSERT(res.cycle_length == 0, "cycle_length should be 0");

    return 0;
}

/* Test 4: Incomplete and truncated lock histories */
static int test_incomplete_lock_history(void) {
    uintptr_t lock_a = 0x11110000;
    uintptr_t lock_b = 0x22220000;

    /* Scenario A: Unlock without prior lock */
    crash_event_t ev_unlock_only = {
        .sequence_id = 1,
        .tid = 5,
        .category = EVENT_CAT_IPC,
        .type = EVENT_SYNC_MUTEX_UNLOCK,
        .addr = lock_a
    };
    causal_graph_result_t res;
    TEST_ASSERT(causal_graph_detect_deadlock(&ev_unlock_only, 1, &res) == 0, "call failed");
    TEST_ASSERT(res.cycle_detected == false, "unlock without lock must not detect cycle");

    /* Scenario B: Multiple threads acquiring disjoint locks without cross-dependency */
    crash_event_t ev_disjoint[2] = {
        { .sequence_id = 1, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 2, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b }
    };
    TEST_ASSERT(causal_graph_detect_deadlock(ev_disjoint, 2, &res) == 0, "call failed");
    TEST_ASSERT(res.cycle_detected == false, "disjoint locks must not detect cycle");

    /* Scenario C: Repeated lock on same thread (reentrant) */
    crash_event_t ev_reentrant[2] = {
        { .sequence_id = 1, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 2, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a }
    };
    TEST_ASSERT(causal_graph_detect_deadlock(ev_reentrant, 2, &res) == 0, "call failed");
    TEST_ASSERT(res.cycle_detected == false, "reentrant locks by same thread must not report circular deadlock");

    return 0;
}

/* Test 5: Multi-thread multi-lock cycle (3 threads: T1 -> T2 -> T3 -> T1) */
static int test_multi_thread_multi_lock_cycle(void) {
    uintptr_t lock_1 = 0x1000;
    uintptr_t lock_2 = 0x2000;
    uintptr_t lock_3 = 0x3000;

    crash_event_t events[6] = {
        /* Phase 1: Each thread holds one lock */
        { .sequence_id = 1, .tid = 101, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_1 },
        { .sequence_id = 2, .tid = 102, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_2 },
        { .sequence_id = 3, .tid = 103, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_3 },

        /* Phase 2: Each thread requests the next lock, closing a 3-way cycle */
        { .sequence_id = 4, .tid = 101, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_2 },
        { .sequence_id = 5, .tid = 102, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_3 },
        { .sequence_id = 6, .tid = 103, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_1 }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 6, &res);
    TEST_ASSERT(rc == 0, "causal_graph_detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == true, "must detect 3-thread cycle");
    TEST_ASSERT(res.cycle_length == 3, "cycle_length must be 3 for 3-thread deadlock");

    printf("    [Cycle Description]: %s\n", res.description);
    return 0;
}

/* Test 6: End-to-end analyze_crash() integration with causal graph */
static int test_end_to_end_analyzer_deadlock(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    uintptr_t lock_a = 0xCAFE0000;
    uintptr_t lock_b = 0xBEEF0000;

    crash_event_t ev1 = { .sequence_id = 1, .tid = 51, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a };
    crash_event_t ev2 = { .sequence_id = 2, .tid = 52, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b };
    crash_event_t ev3 = { .sequence_id = 3, .tid = 51, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b };
    crash_event_t ev4 = { .sequence_id = 4, .tid = 52, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a };

    ring_buffer_record(&rb, &ev1);
    ring_buffer_record(&rb, &ev2);
    ring_buffer_record(&rb, &ev3);
    ring_buffer_record(&rb, &ev4);
    ring_buffer_freeze(&rb);

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash failed on deadlock buffer");
    TEST_ASSERT(res.probable_cause == CAUSE_DEADLOCK, "probable_cause must be CAUSE_DEADLOCK");
    TEST_ASSERT(res.confidence_level >= 0.90f, "deadlock confidence must be >= 0.90");
    TEST_ASSERT(res.causal_chain_len == 4, "causal chain should retain all 4 lock events");
    TEST_ASSERT(strstr(res.explanation, "Wait-For-Graph") != NULL, "explanation must reference Wait-For-Graph");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Regression Test 7: Lock acquired and released before another thread attempts to acquire it */
static int test_lock_released_before_next_acquisition(void) {
    uintptr_t lock_a = 0x55550000;

    crash_event_t events[4] = {
        { .sequence_id = 1, .tid = 10, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 2, .tid = 10, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a },
        { .sequence_id = 3, .tid = 20, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        { .sequence_id = 4, .tid = 20, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 4, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == false, "sequential acquisition without overlap must not detect cycle");
    return 0;
}

/* Regression Test 8: A thread waiting on a lock that is subsequently unlocked */
static int test_thread_waiting_then_unlocked(void) {
    uintptr_t lock_a = 0x66660000;

    crash_event_t events[4] = {
        /* Thread 1 acquires lock A */
        { .sequence_id = 1, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        /* Thread 2 attempts to acquire lock A -> temporarily blocked */
        { .sequence_id = 2, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a },
        /* Thread 1 releases lock A -> resolves Thread 2's wait */
        { .sequence_id = 3, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a },
        /* Thread 2 finishes and releases */
        { .sequence_id = 4, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_a }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 4, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == false, "temporary wait that was resolved by unlock must not be reported as deadlock");
    return 0;
}

/* Regression Test 9: An unlock without a corresponding observed acquisition */
static int test_unlock_without_acquisition(void) {
    uintptr_t lock_x = 0x77770000;
    uintptr_t lock_y = 0x88880000;

    crash_event_t events[3] = {
        /* Truncated history: unlock appears first */
        { .sequence_id = 1, .tid = 3, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_x },
        /* Thread 4 normal lock/unlock */
        { .sequence_id = 2, .tid = 4, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_y },
        { .sequence_id = 3, .tid = 4, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_UNLOCK, .addr = lock_y }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 3, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == false, "unobserved acquisition before unlock must not report deadlock");
    return 0;
}

/* Regression Test 10: Cycle near configured maximum depth (16 threads in circular wait) */
static int test_cycle_near_max_depth(void) {
    #define DEPTH_TEST_N 16
    crash_event_t events[DEPTH_TEST_N * 2];

    uintptr_t base_lock = 0x10000;

    /* Phase 1: Thread i acquires Lock i */
    for (size_t i = 0; i < DEPTH_TEST_N; i++) {
        events[i].sequence_id = (uint64_t)(i + 1);
        events[i].timestamp_ns = (uint64_t)(1000 * (i + 1));
        events[i].pid = 1;
        events[i].tid = (uint32_t)(i + 1);
        events[i].category = EVENT_CAT_IPC;
        events[i].type = EVENT_SYNC_MUTEX_LOCK;
        events[i].addr = base_lock + (i * 0x100);
        events[i].label[0] = '\0';
    }

    /* Phase 2: Thread i requests Lock (i+1)%DEPTH_TEST_N (blocked on next thread) */
    for (size_t i = 0; i < DEPTH_TEST_N; i++) {
        size_t next_lock_idx = (i + 1) % DEPTH_TEST_N;
        size_t ev_idx = DEPTH_TEST_N + i;
        events[ev_idx].sequence_id = (uint64_t)(DEPTH_TEST_N + i + 1);
        events[ev_idx].timestamp_ns = (uint64_t)(1000 * (DEPTH_TEST_N + i + 1));
        events[ev_idx].pid = 1;
        events[ev_idx].tid = (uint32_t)(i + 1);
        events[ev_idx].category = EVENT_CAT_IPC;
        events[ev_idx].type = EVENT_SYNC_MUTEX_LOCK;
        events[ev_idx].addr = base_lock + (next_lock_idx * 0x100);
        events[ev_idx].label[0] = '\0';
    }

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, DEPTH_TEST_N * 2, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed on depth-16 cycle");
    TEST_ASSERT(res.cycle_detected == true, "must detect 16-thread cycle");
    TEST_ASSERT(res.cycle_length == DEPTH_TEST_N, "cycle length must equal 16");

    #undef DEPTH_TEST_N
    return 0;
}

/* Regression Test 11: More threads/locks than graph capacity (graceful degradation) */
static int test_capacity_overflow_safe(void) {
    #define OVERFLOW_COUNT 70
    crash_event_t events[OVERFLOW_COUNT];

    /* Inject 70 threads and 70 distinct non-conflicting locks (exceeding MAX_GRAPH_THREADS=64) */
    for (int i = 0; i < OVERFLOW_COUNT; i++) {
        events[i].sequence_id = (uint64_t)(i + 1);
        events[i].timestamp_ns = (uint64_t)(1000 * (i + 1));
        events[i].pid = 1;
        events[i].tid = (uint32_t)(i + 1);
        events[i].category = EVENT_CAT_IPC;
        events[i].type = EVENT_SYNC_MUTEX_LOCK;
        events[i].addr = (uintptr_t)(0x100000 + i * 0x1000);
        events[i].label[0] = '\0';
    }

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, OVERFLOW_COUNT, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock must not crash on capacity overflow");
    TEST_ASSERT(res.cycle_detected == false, "no cycle should be reported for independent disjoint locks");

    #undef OVERFLOW_COUNT
    return 0;
}

/* Regression Test 12: Truncated history where apparent cycle cannot be established confidently */
static int test_truncated_incomplete_cycle(void) {
    uintptr_t lock_a = 0xAA00;
    uintptr_t lock_b = 0xBB00;

    /*
     * Thread 1 requests Lock B, but Lock B's owner acquisition was truncated from history!
     * Since Lock B has no recorded owner, Thread 1 acquires Lock B.
     * Thread 2 requests Lock A (which was never acquired by anyone in the buffer).
     */
    crash_event_t events[2] = {
        { .sequence_id = 100, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b },
        { .sequence_id = 101, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a }
    };

    causal_graph_result_t res;
    int rc = causal_graph_detect_deadlock(events, 2, &res);
    TEST_ASSERT(rc == 0, "detect_deadlock failed");
    TEST_ASSERT(res.cycle_detected == false, "truncated history must not hallucinate a circular wait");
    return 0;
}

/* Regression Test 13: Simultaneous deadlock and competing root-cause evidence */
static int test_simultaneous_deadlock_and_competing_signal(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    uintptr_t lock_a = 0x1111;
    uintptr_t lock_b = 0x2222;

    /* 1. Deadlock pattern in background threads */
    crash_event_t ev1 = { .sequence_id = 1, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a };
    crash_event_t ev2 = { .sequence_id = 2, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b };
    crash_event_t ev3 = { .sequence_id = 3, .tid = 1, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_b };
    crash_event_t ev4 = { .sequence_id = 4, .tid = 2, .category = EVENT_CAT_IPC, .type = EVENT_SYNC_MUTEX_LOCK, .addr = lock_a };

    /* 2. BUT the actual terminal crash is a fatal null pointer dereference on Thread 3 */
    crash_event_t ev_crash = {
        .sequence_id = 5,
        .timestamp_ns = 50000,
        .pid = 99,
        .tid = 3,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = 0x0,
        .fd_or_status = 11, /* SIGSEGV */
        .label = "SIGSEGV"
    };

    ring_buffer_record(&rb, &ev1);
    ring_buffer_record(&rb, &ev2);
    ring_buffer_record(&rb, &ev3);
    ring_buffer_record(&rb, &ev4);
    ring_buffer_record(&rb, &ev_crash);
    ring_buffer_freeze(&rb);

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash failed");

    /* The active fatal crash signal MUST take diagnostic precedence over background deadlock */
    TEST_ASSERT(res.probable_cause == CAUSE_SEGFAULT, "competing fatal crash signal must take precedence over deadlock");
    TEST_ASSERT(res.confidence_level >= 0.90f, "confidence should be high for fatal null dereference");
    TEST_ASSERT(res.faulting_tid == 3, "faulting TID must be the crashing thread (3)");
    TEST_ASSERT(res.faulting_addr == 0x0, "faulting addr must be 0x0");

    ring_buffer_destroy(&rb);
    return 0;
}

int main(void) {
    printf("==================================================\n");
    printf("  CRASH ARCHAEOLOGIST 2.0 - CAUSAL GRAPH TESTS    \n");
    printf("==================================================\n");

    RUN_GRAPH_TEST(test_empty_event_history);
    RUN_GRAPH_TEST(test_two_thread_abba_cycle);
    RUN_GRAPH_TEST(test_non_cyclic_lock_sequence);
    RUN_GRAPH_TEST(test_incomplete_lock_history);
    RUN_GRAPH_TEST(test_multi_thread_multi_lock_cycle);
    RUN_GRAPH_TEST(test_end_to_end_analyzer_deadlock);

    /* Regression Tests */
    RUN_GRAPH_TEST(test_lock_released_before_next_acquisition);
    RUN_GRAPH_TEST(test_thread_waiting_then_unlocked);
    RUN_GRAPH_TEST(test_unlock_without_acquisition);
    RUN_GRAPH_TEST(test_cycle_near_max_depth);
    RUN_GRAPH_TEST(test_capacity_overflow_safe);
    RUN_GRAPH_TEST(test_truncated_incomplete_cycle);
    RUN_GRAPH_TEST(test_simultaneous_deadlock_and_competing_signal);

    printf("==================================================\n");
    printf("  SUMMARY: %d / %d graph tests passed\n", g_graph_tests_passed, g_graph_tests_run);
    printf("==================================================\n");

    return (g_graph_tests_passed == g_graph_tests_run) ? 0 : 1;
}
