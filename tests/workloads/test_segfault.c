/*
 * test_segfault.c - Synthetic Segmentation Fault Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Spawns a worker thread that intentionally attempts to dereference
 * a NULL pointer (address 0x0), generating an unhandled SIGSEGV.
 *
 * Safe Execution:
 * Targeted specifically at address 0x0. When executed standalone,
 * the OS delivers SIGSEGV terminating the process immediately.
 * When attached to Crash Archaeologist, the signal handler intercepts
 * the fault, freezes the flight recorder, and triggers post-mortem analysis.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static void *worker_thread(void *arg) {
    (void)arg;
    printf("[test_segfault] Worker thread running. Triggering NULL dereference...\n");
    fflush(stdout);

    /* Explicit NULL pointer dereference */
    volatile int *null_ptr = NULL;
    *null_ptr = 0xDEADBEEF;

    return NULL;
}

int main(void) {
    printf("[test_segfault] Starting Segmentation Fault Workload...\n");
    fflush(stdout);

    pthread_t thread;
    if (pthread_create(&thread, NULL, worker_thread, NULL) != 0) {
        perror("pthread_create failed");
        return 1;
    }

    pthread_join(thread, NULL);
    printf("[test_segfault] Completed (unexpected if crash occurred).\n");
    return 0;
}
