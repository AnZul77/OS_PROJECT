
#define _GNU_SOURCE

#include "collector.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>

static __thread int inside_syscall_hook = 0;

static int (*real_open)(const char *, int, ...);
static int (*real_close)(int);
static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);

static void resolve_syscall_functions(void)
{
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");

    if (!real_close)
        real_close = dlsym(RTLD_NEXT, "close");

    if (!real_read)
        real_read = dlsym(RTLD_NEXT, "read");

    if (!real_write)
        real_write = dlsym(RTLD_NEXT, "write");
}

int open(const char *pathname, int flags, ...)
{
    resolve_syscall_functions();

    if (!real_open) {
        errno = ENOSYS;
        return -1;
    }

    int fd;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        fd = real_open(pathname, flags, mode);
    } else {
        fd = real_open(pathname, flags);
    }

    if (!inside_syscall_hook) {
        inside_syscall_hook = 1;
        collector_record_event(
            EVENT_CAT_SYSCALL, (event_type_t)300,
            0, 0, fd, "open");
        inside_syscall_hook = 0;
    }

    return fd;
}

int close(int fd)
{
    resolve_syscall_functions();

    if (!real_close) {
        errno = ENOSYS;
        return -1;
    }

    int result = real_close(fd);

    if (!inside_syscall_hook) {
        inside_syscall_hook = 1;
        collector_record_event(
            EVENT_CAT_SYSCALL, (event_type_t)301,
            0, 0, result == 0 ? fd : -errno, "close");
        inside_syscall_hook = 0;
    }

    return result;
}

ssize_t read(int fd, void *buffer, size_t count)
{
    resolve_syscall_functions();

    if (!real_read) {
        errno = ENOSYS;
        return -1;
    }

    ssize_t result = real_read(fd, buffer, count);

    if (!inside_syscall_hook) {
        inside_syscall_hook = 1;
        collector_record_event(
            EVENT_CAT_SYSCALL, (event_type_t)302,
            (uintptr_t)buffer,
            result > 0 ? (size_t)result : 0,
            result < 0 ? -errno : (int32_t)fd,
            "read");
        inside_syscall_hook = 0;
    }

    return result;
}

ssize_t write(int fd, const void *buffer, size_t count)
{
    resolve_syscall_functions();

    if (!real_write) {
        errno = ENOSYS;
        return -1;
    }

    ssize_t result = real_write(fd, buffer, count);

    if (!inside_syscall_hook) {
        inside_syscall_hook = 1;
        collector_record_event(
            EVENT_CAT_SYSCALL, (event_type_t)303,
            (uintptr_t)buffer,
            result > 0 ? (size_t)result : 0,
            result < 0 ? -errno : (int32_t)fd,
            "write");
        inside_syscall_hook = 0;
    }

    return result;
}
