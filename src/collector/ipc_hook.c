
#define _GNU_SOURCE

#include "collector.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

static __thread int inside_ipc_hook = 0;

static int (*real_pipe)(int [2]);
static ssize_t (*real_send)(int, const void *, size_t, int);
static ssize_t (*real_recv)(int, void *, size_t, int);
static int (*real_socket)(int, int, int);

static void resolve_ipc_functions(void)
{
    if (!real_pipe)
        real_pipe = dlsym(RTLD_NEXT, "pipe");

    if (!real_send)
        real_send = dlsym(RTLD_NEXT, "send");

    if (!real_recv)
        real_recv = dlsym(RTLD_NEXT, "recv");

    if (!real_socket)
        real_socket = dlsym(RTLD_NEXT, "socket");
}

int pipe(int pipefd[2])
{
    static int (*real_pipe)(int pipefd[2]) = NULL;

    if (real_pipe == NULL) {
        real_pipe = dlsym(RTLD_NEXT, "pipe");
    }

    return real_pipe(pipefd);
}

int socket(int domain, int type, int protocol)
{
    resolve_ipc_functions();

    if (!real_socket) {
        errno = ENOSYS;
        return -1;
    }

    int fd = real_socket(domain, type, protocol);



    return fd;
}

ssize_t send(int sockfd, const void *buffer,
             size_t length, int flags)
{
    resolve_ipc_functions();

    if (!real_send) {
        errno = ENOSYS;
        return -1;
    }

    ssize_t result = real_send(sockfd, buffer, length, flags);

    if (!inside_ipc_hook) {
        inside_ipc_hook = 1;
        collector_record_event(
            EVENT_CAT_IPC, (event_type_t)402,
            (uintptr_t)buffer,
            result > 0 ? (size_t)result : 0,
            result < 0 ? -errno : sockfd,
            "socket_send");
        inside_ipc_hook = 0;
    }

    return result;
}

ssize_t recv(int sockfd, void *buffer,
             size_t length, int flags)
{
    resolve_ipc_functions();

    if (!real_recv) {
        errno = ENOSYS;
        return -1;
    }

    ssize_t result = real_recv(sockfd, buffer, length, flags);

    if (!inside_ipc_hook) {
        inside_ipc_hook = 1;
        collector_record_event(
            EVENT_CAT_IPC, (event_type_t)403,
            (uintptr_t)buffer,
            result > 0 ? (size_t)result : 0,
            result < 0 ? -errno : sockfd,
            "socket_recv");
        inside_ipc_hook = 0;
    }

    return result;
}
