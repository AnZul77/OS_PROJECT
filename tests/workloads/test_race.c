/*
 * test_race.c - Synthetic Data Race Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Two concurrent threads repeatedly increment a shared non-atomic counter
 * without mutex synchronization.
 *
 * Determinism & Undefined Behavior (UB) Caution:
 * Per C99/C11 §5.1.2.4, concurrent unsynchronized reads and writes to a non-atomic
 * variable constitute undefined behavior (UB).
 * Consequently:
 *   - The exact count of lost updates cannot be asserted deterministically.
 *   - Compilers may legally optimize, hoist, or reorder non-atomic accesses.
 *   - On single-core VMs or under certain optimization levels (-O3), the threads
 *     may interleave differently or show zero lost updates.
 * This workload illustrates the hazard on multi-core systems, but is NOT a
 * deterministic unit test with an exact expected lost-update count.
 *
 * Diagnostic Reality Check:
 * Standard user-space function hooking (LD_PRELOAD on libc) only observes
 * library calls (malloc, pthread_mutex_lock, syscalls), NOT raw CPU read/write
 * instructions. To detect data races in production, an engine requires hardware
 * watchpoints or compiler-assisted instrumentation (like ThreadSanitizer).
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define ITERATIONS_PER_THREAD 500000

static volatile long g_shared_counter = 0;

static void *race_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        /* Unsynchronized read-modify-write operation (data race) */
        g_shared_counter++;
    }
    return NULL;
}

int main(void) {
    printf("[test_race] Starting Data Race Workload (%d threads x %d increments)...\n",
           2, ITERATIONS_PER_THREAD);

    pthread_t t1, t2;
    if (pthread_create(&t1, NULL, race_worker, NULL) != 0 ||
        pthread_create(&t2, NULL, race_worker, NULL) != 0) {
        perror("pthread_create failed");
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    long expected = 2 * ITERATIONS_PER_THREAD;
    long actual = g_shared_counter;
    long lost = expected - actual;

    printf("[test_race] Expected counter : %ld\n", expected);
    printf("[test_race] Actual counter   : %ld\n", actual);
    printf("[test_race] Lost updates     : %ld\n", lost);
    printf("[test_race] Note: Lost update count is non-deterministic (UB per C11 §5.1.2.4).\n");

    return 0;
}
