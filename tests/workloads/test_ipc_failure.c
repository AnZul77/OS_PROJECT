/*
 * test_ipc_failure.c - Synthetic IPC Broken Pipe Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Creates an anonymous unidirectional pipe, explicitly closes the read endpoint,
 * and immediately attempts to write payload bytes to the write endpoint.
 *
 * Platform Differences (Windows CRT vs POSIX):
 * - On Linux / POSIX:
 *   Writing to a pipe whose read end is closed causes the kernel to deliver
 *   SIGPIPE (Signal 13) to the calling process, terminating it unless handled.
 *   If SIGPIPE is ignored, write() returns -1 with errno = EPIPE (Broken pipe).
 *   Member 1's signal handler intercepts SIGPIPE as a fatal IPC failure.
 * - On Windows / MinGW CRT:
 *   Windows does not implement POSIX SIGPIPE signals for anonymous pipes created via _pipe().
 *   When the read handle is closed, the Windows CRT invalidates the pipe state,
 *   causing write() to return -1 with errno = EINVAL (Invalid argument, errno 22)
 *   or EBADF, rather than delivering a signal.
 *   Both behaviors verify broken pipe detection within their respective OS contexts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
static inline int portable_pipe(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}
#else
#include <unistd.h>
#include <signal.h>
static inline int portable_pipe(int fds[2]) {
    return pipe(fds);
}
#endif

int main(void) {
    printf("[test_ipc_failure] Starting IPC Broken Pipe Workload...\n");

    int fds[2];
    if (portable_pipe(fds) < 0) {
        perror("pipe creation failed");
        return 1;
    }

    printf("[test_ipc_failure] Pipe created: Read FD = %d, Write FD = %d\n", fds[0], fds[1]);

    /* Abruptly close the read endpoint */
    printf("[test_ipc_failure] Closing read endpoint (FD %d)...\n", fds[0]);
    close(fds[0]);

    /* Attempt to write to the orphan write endpoint */
    const char *payload = "crash_archaeologist_test_payload";
    printf("[test_ipc_failure] Attempting write to orphan write endpoint (FD %d)...\n", fds[1]);
    fflush(stdout);

    int bytes_written = write(fds[1], payload, strlen(payload));
    if (bytes_written < 0) {
        printf("[test_ipc_failure] write() failed as expected with errno = %d (%s)\n",
               errno, strerror(errno));
#ifdef _WIN32
        printf("[test_ipc_failure] Note: Windows CRT returns EINVAL (22) for closed pipe writes instead of POSIX EPIPE/SIGPIPE.\n");
#else
        printf("[test_ipc_failure] POSIX broken pipe confirmed (errno %d = %s).\n", errno, strerror(errno));
#endif
    } else {
        printf("[test_ipc_failure] Wrote %d bytes (unexpected if read end is closed).\n",
               bytes_written);
    }

    close(fds[1]);
    printf("[test_ipc_failure] Workload finished.\n");
    return 0;
}
