/*
 * test_resource_exhaustion.c - Synthetic File Descriptor Exhaustion Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Loops calling open() without close() until the operating system process
 * descriptor table limit (EMFILE / ENFILE) is encountered, producing a failed
 * syscall event.
 *
 * Safe Execution Strategy:
 * 1. Strict cap at MAX_FD_TEST (1024 descriptors) to prevent system-wide FD strain.
 * 2. Every successfully opened descriptor is stored in a tracking array and
 *    explicitly closed during cleanup before program termination, ensuring
 *    zero resource leakage on the developer's workstation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define MAX_FD_TEST 1024

int main(void) {
    printf("[test_resource_exhaustion] Starting File Descriptor Exhaustion Workload...\n");

    int fds[MAX_FD_TEST];
    int opened_count = 0;
    int last_errno = 0;

#ifdef _WIN32
    const char *target_file = "NUL";
#else
    const char *target_file = "/dev/null";
#endif

    for (int i = 0; i < MAX_FD_TEST; i++) {
        int fd = open(target_file, O_RDONLY);
        if (fd < 0) {
            last_errno = errno;
            printf("[test_resource_exhaustion] open() failed at iteration %d with errno %d (%s)\n",
                   i, last_errno,
                   (last_errno == EMFILE) ? "EMFILE: Too many open files" : "Descriptor limit reached");
            break;
        }
        fds[opened_count++] = fd;
    }

    printf("[test_resource_exhaustion] Successfully opened %d file descriptors before limit.\n",
           opened_count);

    /* Safe cleanup: close all open file descriptors */
    printf("[test_resource_exhaustion] Cleaning up all %d open descriptors...\n", opened_count);
    for (int i = 0; i < opened_count; i++) {
        close(fds[i]);
    }

    printf("[test_resource_exhaustion] Cleanup complete. Exiting cleanly.\n");
    return 0;
}
