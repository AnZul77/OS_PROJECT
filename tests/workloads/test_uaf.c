/*
 * test_uaf.c - Synthetic Use-After-Free Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Thread 1 allocates a bounded memory buffer (512 bytes), writes to it,
 * and calls free(). Shortly thereafter, Thread 2 attempts to write to
 * the previously freed buffer.
 *
 * Safe Execution & Platform Notes:
 * In modern user-space heap allocators (glibc ptmalloc / Windows UCRT), writing
 * to recently freed memory within an active virtual memory page is undefined behavior (UB)
 * and does NOT immediately cause an MMU page fault (SIGSEGV) because the virtual memory
 * page remains mapped in the process address space.
 *
 * Therefore, in a standalone execution:
 *   - The workload writes to freed memory (demonstrating the UAF condition).
 *   - It completes without crashing unless page-level guard allocations, ASan, or
 *     strict allocator poisoning are enabled.
 *
 * Role in Crash Archaeologist 2.0 Pipeline:
 *   1. Member 2's mem_hook.c intercepts malloc() and free(), recording sequence
 *      IDs, pointers, and allocation sizes into the flight recorder.
 *   2. When an eventual crash (e.g. heap header corruption or SIGSEGV) occurs,
 *      Person 3's analyzer correlates the fault address against the recorded free event
 *      to reconstruct CAUSE_USE_AFTER_FREE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define CHUNK_SIZE 512

static char * volatile g_shared_ptr = NULL;

static void *thread_alloc_and_free(void *arg) {
    (void)arg;
    printf("[test_uaf] Thread 1: Allocating %d bytes...\n", CHUNK_SIZE);
    char *ptr = (char *)malloc(CHUNK_SIZE);
    if (!ptr) {
        perror("malloc failed");
        return NULL;
    }
    memset(ptr, 0xAA, CHUNK_SIZE);
    g_shared_ptr = ptr;

    /* Simulate brief work then free the block */
    usleep(10000); /* 10ms */
    printf("[test_uaf] Thread 1: Freeing memory block at %p...\n", (void *)ptr);
    free(ptr);

    return NULL;
}

static void *thread_access_after_free(void *arg) {
    (void)arg;
    /* Wait for Thread 1 to allocate and free */
    while (g_shared_ptr == NULL) {
        usleep(5000);
    }
    usleep(25000); /* 25ms to ensure Thread 1 has called free() */

    printf("[test_uaf] Thread 2: Deliberately writing to freed pointer %p...\n", (void *)g_shared_ptr);
    /* Attempting write to freed memory block (Use-After-Free) */
    g_shared_ptr[16] = 0x55;

    printf("[test_uaf] Thread 2: Wrote to freed memory (value: 0x%02X).\n", (unsigned char)g_shared_ptr[16]);
    printf("[test_uaf] Note: Modern allocators keep pages mapped, so standalone run does not crash by default.\n");
    return NULL;
}

int main(void) {
    printf("[test_uaf] Starting Use-After-Free Workload...\n");
    fflush(stdout);

    pthread_t t1, t2;
    if (pthread_create(&t1, NULL, thread_alloc_and_free, NULL) != 0 ||
        pthread_create(&t2, NULL, thread_access_after_free, NULL) != 0) {
        perror("pthread_create failed");
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[test_uaf] Completed execution cleanly.\n");
    return 0;
}
