/*
 * test_deadlock.c - Synthetic Circular Deadlock Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Implements a classic AB-BA circular lock dependency between two threads:
 *   - Thread 1 acquires Mutex A, sleeps briefly, and requests Mutex B.
 *   - Thread 2 acquires Mutex B, sleeps briefly, and requests Mutex A.
 * Neither thread can proceed, resulting in an unresolvable circular deadlock.
 *
 * Safe Execution Strategy:
 * Spawns an independent detached Watchdog Thread with a strict 2-second timeout.
 * When the deadlock occurs, the watchdog catches the hang, logs the verified
 * deadlock state, and terminates the process via _exit(124).
 *
 * Exit Code Distinction:
 * - Exit code 124 (standard timeout status) indicates intentional watchdog termination
 *   due to confirmed deadlock reproduction.
 * - Exit code 0 would indicate the threads completed normally without deadlocking (unexpected).
 * This explicitly distinguishes an intentional timeout termination from an ordinary pass.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t g_mutex_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mutex_b = PTHREAD_MUTEX_INITIALIZER;

#define WATCHDOG_TIMEOUT_SECONDS 2
#define EXIT_CODE_WATCHDOG_DEADLOCK 124

/* Watchdog thread ensuring bounded execution */
static void *watchdog_timer(void *arg) {
    (void)arg;
    sleep(WATCHDOG_TIMEOUT_SECONDS);
    printf("\n[test_deadlock:WATCHDOG_TIMEOUT_TRIGGERED] 2-second timeout reached.\n");
    printf("[test_deadlock:WATCHDOG_TIMEOUT_TRIGGERED] Circular deadlock successfully provoked (AB-BA hang verified).\n");
    printf("[test_deadlock:WATCHDOG_TIMEOUT_TRIGGERED] Terminating process with exit code %d (intentional deadlock timeout).\n",
           EXIT_CODE_WATCHDOG_DEADLOCK);
    fflush(stdout);
    _exit(EXIT_CODE_WATCHDOG_DEADLOCK);
    return NULL;
}

static void *thread_one(void *arg) {
    (void)arg;
    printf("[test_deadlock] Thread 1: Acquiring Mutex A...\n");
    pthread_mutex_lock(&g_mutex_a);
    printf("[test_deadlock] Thread 1: Acquired Mutex A. Sleeping 20ms...\n");
    usleep(20000);

    printf("[test_deadlock] Thread 1: Requesting Mutex B (will block)...\n");
    pthread_mutex_lock(&g_mutex_b);
    printf("[test_deadlock] Thread 1: Acquired Mutex B (unreachable in deadlock).\n");

    pthread_mutex_unlock(&g_mutex_b);
    pthread_mutex_unlock(&g_mutex_a);
    return NULL;
}

static void *thread_two(void *arg) {
    (void)arg;
    printf("[test_deadlock] Thread 2: Acquiring Mutex B...\n");
    pthread_mutex_lock(&g_mutex_b);
    printf("[test_deadlock] Thread 2: Acquired Mutex B. Sleeping 20ms...\n");
    usleep(20000);

    printf("[test_deadlock] Thread 2: Requesting Mutex A (will block)...\n");
    pthread_mutex_lock(&g_mutex_a);
    printf("[test_deadlock] Thread 2: Acquired Mutex A (unreachable in deadlock).\n");

    pthread_mutex_unlock(&g_mutex_a);
    pthread_mutex_unlock(&g_mutex_b);
    return NULL;
}

int main(void) {
    printf("[test_deadlock] Starting Circular Deadlock Workload (Watchdog limit: %d sec)...\n",
           WATCHDOG_TIMEOUT_SECONDS);
    fflush(stdout);

    /* Launch watchdog thread first */
    pthread_t watchdog;
    if (pthread_create(&watchdog, NULL, watchdog_timer, NULL) != 0) {
        perror("Failed to create watchdog thread");
        return 1;
    }
    pthread_detach(watchdog);

    /* Launch deadlock threads */
    pthread_t t1, t2;
    if (pthread_create(&t1, NULL, thread_one, NULL) != 0 ||
        pthread_create(&t2, NULL, thread_two, NULL) != 0) {
        perror("Failed to create worker threads");
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[test_deadlock] Completed without deadlock (unexpected: lock ordering did not deadlock).\n");
    return 0;
}
