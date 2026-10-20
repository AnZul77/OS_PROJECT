#include "engine.h"
#include "buffer.h"
#include "event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[-] ASSERTION FAILED: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        return -1; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("[*] Running %s...\n", #fn); \
    if (fn() == 0) { \
        printf("[+] %s PASSED\n", #fn); \
        g_tests_passed++; \
    } else { \
        printf("[-] %s FAILED\n", #fn); \
    } \
} while(0)

/* Test 1: Empty history in ring buffer */
static int test_empty_history(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash should return 0 on empty buffer");
    TEST_ASSERT(res.probable_cause == CAUSE_UNKNOWN, "cause should be CAUSE_UNKNOWN for empty buffer");
    TEST_ASSERT(res.confidence_level == 0.0f, "confidence should be 0.0 for empty buffer");
    TEST_ASSERT(res.causal_chain_len == 0, "causal chain len should be 0");
    TEST_ASSERT(strlen(res.explanation) > 0, "explanation should not be empty");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Test 2: Low-address / null pointer segmentation fault */
static int test_low_address_segfault(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    crash_event_t ev1 = {
        .sequence_id = 1,
        .timestamp_ns = 1000,
        .pid = 100,
        .tid = 1,
        .category = EVENT_CAT_PROCESS_THREAD,
        .type = EVENT_THREAD_CREATE,
        .addr = 0,
        .size = 0,
        .fd_or_status = 0,
        .label = "worker_thread"
    };
    ring_buffer_record(&rb, &ev1);

    /* Signal event: null pointer dereference at address 0x0 */
    crash_event_t ev_sig = {
        .sequence_id = 2,
        .timestamp_ns = 2000,
        .pid = 100,
        .tid = 1,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = 0x0,
        .size = 0,
        .fd_or_status = 11, /* SIGSEGV */
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &ev_sig);
    ring_buffer_freeze(&rb);

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash failed");
    TEST_ASSERT(res.probable_cause == CAUSE_SEGFAULT, "probable_cause should be CAUSE_SEGFAULT");
    TEST_ASSERT(res.confidence_level >= 0.90f, "confidence should be high (>= 0.90) for null dereference");
    TEST_ASSERT(res.faulting_tid == 1, "faulting TID should be 1");
    TEST_ASSERT(res.faulting_addr == 0x0, "faulting addr should be 0x0");
    TEST_ASSERT(res.causal_chain_len >= 1, "causal chain should have at least the signal event");
    TEST_ASSERT(strstr(res.explanation, "null pointer") != NULL, "explanation should mention null pointer");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Test 3: Memory access after matching free (Use-After-Free) */
static int test_use_after_free(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    uintptr_t target_chunk = 0x55556000;
    size_t chunk_size = 512;

    /* Event 1: Thread 1 allocates 512 bytes */
    crash_event_t ev_alloc = {
        .sequence_id = 10,
        .timestamp_ns = 100000,
        .pid = 200,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = target_chunk,
        .size = chunk_size,
        .fd_or_status = 0,
        .label = "malloc"
    };
    ring_buffer_record(&rb, &ev_alloc);

    /* Event 2: Thread 1 frees the block */
    crash_event_t ev_free = {
        .sequence_id = 11,
        .timestamp_ns = 150000,
        .pid = 200,
        .tid = 101,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_FREE,
        .addr = target_chunk,
        .size = chunk_size,
        .fd_or_status = 0,
        .label = "free"
    };
    ring_buffer_record(&rb, &ev_free);

    /* Event 3: Thread 2 crashes while writing into target_chunk + 16 */
    crash_event_t ev_crash = {
        .sequence_id = 12,
        .timestamp_ns = 200000,
        .pid = 200,
        .tid = 102,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = target_chunk + 16,
        .size = 0,
        .fd_or_status = 11, /* SIGSEGV */
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &ev_crash);
    ring_buffer_freeze(&rb);

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash failed");
    TEST_ASSERT(res.probable_cause == CAUSE_USE_AFTER_FREE, "probable_cause should be CAUSE_USE_AFTER_FREE");
    TEST_ASSERT(res.confidence_level >= 0.90f, "confidence should be >= 0.90 for matched free");
    TEST_ASSERT(res.faulting_tid == 102, "faulting TID should be 102");
    TEST_ASSERT(res.faulting_addr == (target_chunk + 16), "faulting address should match crash address");
    TEST_ASSERT(res.causal_chain_len >= 2, "causal chain should link alloc/free and crash events");
    TEST_ASSERT(strstr(res.explanation, "Use-After-Free") != NULL, "explanation should mention Use-After-Free");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Test 4: Inconclusive event history (no fatal signal, normal operations) */
static int test_inconclusive_history(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    /* Normal benign syscalls */
    crash_event_t ev1 = {
        .sequence_id = 1,
        .timestamp_ns = 1000,
        .pid = 300,
        .tid = 1,
        .category = EVENT_CAT_SYSCALL,
        .type = EVENT_SYS_READ,
        .addr = 0x7ffffff0,
        .size = 64,
        .fd_or_status = 0,
        .label = "read"
    };
    crash_event_t ev2 = {
        .sequence_id = 2,
        .timestamp_ns = 2000,
        .pid = 300,
        .tid = 1,
        .category = EVENT_CAT_SYSCALL,
        .type = EVENT_SYS_WRITE,
        .addr = 0x7ffffff0,
        .size = 64,
        .fd_or_status = 0,
        .label = "write"
    };
    ring_buffer_record(&rb, &ev1);
    ring_buffer_record(&rb, &ev2);

    crash_analysis_result_t res;
    int rc = analyze_crash(&rb, &res);
    TEST_ASSERT(rc == 0, "analyze_crash should succeed");
    TEST_ASSERT(res.probable_cause == CAUSE_UNKNOWN, "should diagnose CAUSE_UNKNOWN when inconclusive");
    TEST_ASSERT(res.confidence_level < 0.20f, "confidence should be very low (< 0.20)");
    TEST_ASSERT(strlen(res.explanation) > 0, "explanation should explain inconclusive evidence");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Test 5: Report generation and JSON output format */
static int test_report_generation(void) {
    ring_buffer_t rb;
    TEST_ASSERT(ring_buffer_init(&rb, 64) == 0, "ring_buffer_init failed");

    crash_event_t ev1 = {
        .sequence_id = 1,
        .timestamp_ns = 10000,
        .pid = 500,
        .tid = 2,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_ALLOC,
        .addr = 0x100000,
        .size = 256,
        .fd_or_status = 0,
        .label = "alloc_chunk"
    };
    crash_event_t ev2 = {
        .sequence_id = 2,
        .timestamp_ns = 20000,
        .pid = 500,
        .tid = 2,
        .category = EVENT_CAT_MEMORY,
        .type = EVENT_MEM_FREE,
        .addr = 0x100000,
        .size = 256,
        .fd_or_status = 0,
        .label = "free_chunk"
    };
    crash_event_t ev3 = {
        .sequence_id = 3,
        .timestamp_ns = 30000,
        .pid = 500,
        .tid = 2,
        .category = EVENT_CAT_SIGNAL,
        .type = EVENT_SIG_RECEIVED,
        .addr = 0x100010,
        .size = 0,
        .fd_or_status = 11,
        .label = "SIGSEGV"
    };
    ring_buffer_record(&rb, &ev1);
    ring_buffer_record(&rb, &ev2);
    ring_buffer_record(&rb, &ev3);

    crash_analysis_result_t res;
    TEST_ASSERT(analyze_crash(&rb, &res) == 0, "analyze_crash failed");

    /* Test plain-text report generation */
    const char *txt_path = "reports/test_crash_report.txt";
    int rc_txt = generate_crash_report(&res, txt_path);
    TEST_ASSERT(rc_txt == 0, "text report generation failed");

    /* Test JSON report generation */
    const char *json_path = "reports/test_crash_report.json";
    int rc_json = generate_crash_report(&res, json_path);
    TEST_ASSERT(rc_json == 0, "json report generation failed");

    /* Test stdout path (output_file == NULL) */
    printf("\n--- BEGIN TEST STDOUT REPORT ---\n");
    int rc_stdout = generate_crash_report(&res, NULL);
    printf("--- END TEST STDOUT REPORT ---\n\n");
    TEST_ASSERT(rc_stdout == 0, "stdout report generation failed");

    /* Verify files exist and are non-empty */
    FILE *f_txt = fopen(txt_path, "r");
    TEST_ASSERT(f_txt != NULL, "text report file was not created");
    fseek(f_txt, 0, SEEK_END);
    long txt_sz = ftell(f_txt);
    fclose(f_txt);
    TEST_ASSERT(txt_sz > 50, "text report file is unexpectedly small");

    FILE *f_json = fopen(json_path, "r");
    TEST_ASSERT(f_json != NULL, "json report file was not created");
    fseek(f_json, 0, SEEK_END);
    long json_sz = ftell(f_json);
    fclose(f_json);
    TEST_ASSERT(json_sz > 50, "json report file is unexpectedly small");

    ring_buffer_destroy(&rb);
    return 0;
}

/* Test 6: Null pointer safety */
static int test_null_safety(void) {
    crash_analysis_result_t res;
    TEST_ASSERT(analyze_crash(NULL, &res) == 0, "analyze_crash with null rb should handle gracefully");
    TEST_ASSERT(res.probable_cause == CAUSE_UNKNOWN, "cause should be unknown for null rb");

    TEST_ASSERT(analyze_crash(NULL, NULL) == -1, "analyze_crash with null result should return -1");
    TEST_ASSERT(generate_crash_report(NULL, "dummy.txt") == -1, "generate_crash_report with null result should return -1");

    return 0;
}

int main(void) {
    printf("==================================================\n");
    printf("  CRASH ARCHAEOLOGIST 2.0 - ANALYZER UNIT TESTS   \n");
    printf("==================================================\n");

    RUN_TEST(test_empty_history);
    RUN_TEST(test_low_address_segfault);
    RUN_TEST(test_use_after_free);
    RUN_TEST(test_inconclusive_history);
    RUN_TEST(test_report_generation);
    RUN_TEST(test_null_safety);

    printf("==================================================\n");
    printf("  SUMMARY: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("==================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
