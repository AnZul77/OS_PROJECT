#include "buffer.h"
#include "collector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#define TEST_PASS(name) printf("  [PASS] %s\n", name)
#define TEST_FAIL(name, msg) do { printf("  [FAIL] %s: %s\n", name, msg); exit(1); } while(0)

/* Test 1: Basic initialization and empty state */
static void test_init_and_empty(void) {
    ring_buffer_t rb;
    int res = ring_buffer_init(&rb, 16);
    assert(res == 0);
    assert(ring_buffer_get_count(&rb) == 0);
    assert(!ring_buffer_is_frozen(&rb));
    
    crash_event_t ev;
    assert(ring_buffer_get_at(&rb, 0, &ev) == -1);
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_init_and_empty");
}

/* Test 2: Sequential recording before capacity limit */
static void test_sequential_record(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 8);
    
    for (int i = 0; i < 5; i++) {
        crash_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.category = EVENT_CAT_MEMORY;
        ev.type = EVENT_MEM_ALLOC;
        ev.addr = (uintptr_t)(0x1000 + i * 0x10);
        ev.size = 64;
        snprintf(ev.label, sizeof(ev.label), "ALLOC_%d", i);
        
        ring_buffer_record(&rb, &ev);
    }
    
    assert(ring_buffer_get_count(&rb) == 5);
    
    for (int i = 0; i < 5; i++) {
        crash_event_t out;
        int ret = ring_buffer_get_at(&rb, (size_t)i, &out);
        assert(ret == 0);
        assert(out.sequence_id == (uint64_t)(i + 1));
        assert(out.addr == (uintptr_t)(0x1000 + i * 0x10));
    }
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_sequential_record");
}

/* Test 3: Wrap-around overwrite mechanics */
static void test_wrap_around_overwrite(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 4);
    
    /* Record 6 events into a capacity-4 buffer */
    for (int i = 1; i <= 6; i++) {
        crash_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.category = EVENT_CAT_SYSCALL;
        ev.type = EVENT_SYS_OPEN;
        ev.sequence_id = 0; /* Auto-assigned */
        snprintf(ev.label, sizeof(ev.label), "EVENT_%d", i);
        
        ring_buffer_record(&rb, &ev);
    }
    
    /* Count must be capped at capacity (4) */
    assert(ring_buffer_get_count(&rb) == 4);
    
    /* Surviving events must be sequence 3, 4, 5, 6 */
    for (size_t i = 0; i < 4; i++) {
        crash_event_t out;
        int ret = ring_buffer_get_at(&rb, i, &out);
        assert(ret == 0);
        assert(out.sequence_id == (uint64_t)(i + 3));
        
        char expected_label[32];
        snprintf(expected_label, sizeof(expected_label), "EVENT_%d", (int)(i + 3));
        assert(strcmp(out.label, expected_label) == 0);
    }
    
    /* Out of bounds access must return -1 */
    crash_event_t out;
    assert(ring_buffer_get_at(&rb, 4, &out) == -1);
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_wrap_around_overwrite");
}

/* Test 4: Freeze semantics (Invariance guarantee) */
static void test_freeze_invariance(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 4);
    
    for (int i = 1; i <= 3; i++) {
        crash_event_t ev;
        memset(&ev, 0, sizeof(ev));
        snprintf(ev.label, sizeof(ev.label), "BEFORE_%d", i);
        ring_buffer_record(&rb, &ev);
    }
    assert(ring_buffer_get_count(&rb) == 3);
    
    /* Freeze buffer */
    ring_buffer_freeze(&rb);
    assert(ring_buffer_is_frozen(&rb));
    
    /* Attempt to record while frozen */
    crash_event_t forbidden;
    memset(&forbidden, 0, sizeof(forbidden));
    snprintf(forbidden.label, sizeof(forbidden.label), "AFTER_FREEZE");
    ring_buffer_record(&rb, &forbidden);
    
    /* Must not change count or contents */
    assert(ring_buffer_get_count(&rb) == 3);
    
    crash_event_t last;
    ring_buffer_get_at(&rb, 2, &last);
    assert(strcmp(last.label, "BEFORE_3") == 0);
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_freeze_invariance");
}

/* Test 5: Multithreaded stress recording */
#define NUM_THREADS 4
#define EVENTS_PER_THREAD 2500

typedef struct {
    ring_buffer_t *rb;
    int thread_id;
} worker_arg_t;

static void* stress_worker(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    for (int i = 0; i < EVENTS_PER_THREAD; i++) {
        crash_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.category = EVENT_CAT_PROCESS_THREAD;
        ev.type = EVENT_THREAD_SWITCH;
        ev.tid = (uint32_t)w->thread_id;
        snprintf(ev.label, sizeof(ev.label), "T%d_EV%d", w->thread_id, i);
        ring_buffer_record(w->rb, &ev);
    }
    return NULL;
}

static void test_multithreaded_stress(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 1024);
    
    pthread_t threads[NUM_THREADS];
    worker_arg_t args[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].rb = &rb;
        args[i].thread_id = i + 1;
        pthread_create(&threads[i], NULL, stress_worker, &args[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    assert(ring_buffer_get_count(&rb) == 1024);
    
    /* Verify sequential order is monotonically increasing */
    uint64_t prev_seq = 0;
    for (size_t i = 0; i < 1024; i++) {
        crash_event_t ev;
        int res = ring_buffer_get_at(&rb, i, &ev);
        assert(res == 0);
        assert(ev.sequence_id > prev_seq);
        prev_seq = ev.sequence_id;
    }
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_multithreaded_stress");
}

int main(void) {
    printf("====================================================\n");
    printf("   CRASH ARCHAEOLOGIST: Dev 1 Ring Buffer Unit Tests\n");
    printf("====================================================\n");
    
    test_init_and_empty();
    test_sequential_record();
    test_wrap_around_overwrite();
    test_freeze_invariance();
    test_multithreaded_stress();
    
    printf("====================================================\n");
    printf("   ALL DEV 1 RING BUFFER UNIT TESTS PASSED (5/5)\n");
    printf("====================================================\n");
    return 0;
}
