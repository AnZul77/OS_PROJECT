
#define _GNU_SOURCE

#include "collector.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>

static __thread int inside_memory_hook = 0;

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static int (*real_munmap)(void *, size_t);

static void resolve_memory_functions(void)
{
    if (!real_malloc)
        real_malloc = dlsym(RTLD_NEXT, "malloc");

    if (!real_calloc)
        real_calloc = dlsym(RTLD_NEXT, "calloc");

    if (!real_realloc)
        real_realloc = dlsym(RTLD_NEXT, "realloc");

    if (!real_free)
        real_free = dlsym(RTLD_NEXT, "free");

    if (!real_mmap)
        real_mmap = dlsym(RTLD_NEXT, "mmap");

    if (!real_munmap)
        real_munmap = dlsym(RTLD_NEXT, "munmap");
}

void *malloc(size_t size)
{
    resolve_memory_functions();

    if (!real_malloc) {
        errno = ENOMEM;
        return NULL;
    }

    if (inside_memory_hook)
        return real_malloc(size);

    inside_memory_hook = 1;
    void *ptr = real_malloc(size);

    if (ptr != NULL) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)200,
            (uintptr_t)ptr, size, 0, "malloc");
    }

    inside_memory_hook = 0;
    return ptr;
}

void *calloc(size_t count, size_t size)
{
    resolve_memory_functions();

    if (!real_calloc) {
        errno = ENOMEM;
        return NULL;
    }

    if (inside_memory_hook)
        return real_calloc(count, size);

    inside_memory_hook = 1;
    void *ptr = real_calloc(count, size);

    if (ptr != NULL) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)200,
            (uintptr_t)ptr, count * size, 0, "calloc");
    }

    inside_memory_hook = 0;
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    resolve_memory_functions();

    if (!real_realloc) {
        errno = ENOMEM;
        return NULL;
    }

    if (inside_memory_hook)
        return real_realloc(ptr, size);

    inside_memory_hook = 1;
    void *new_ptr = real_realloc(ptr, size);

    if (new_ptr != NULL || size == 0) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)202,
            (uintptr_t)new_ptr, size, 0, "realloc");
    }

    inside_memory_hook = 0;
    return new_ptr;
}

void free(void *ptr)
{
    resolve_memory_functions();

    if (!real_free)
        return;

    if (inside_memory_hook) {
        real_free(ptr);
        return;
    }

    inside_memory_hook = 1;

    if (ptr != NULL) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)201,
            (uintptr_t)ptr, 0, 0, "free");
    }

    real_free(ptr);
    inside_memory_hook = 0;
}

void *mmap(void *addr, size_t length, int prot,
           int flags, int fd, off_t offset)
{
    resolve_memory_functions();

    if (!real_mmap) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    if (inside_memory_hook)
        return real_mmap(addr, length, prot, flags, fd, offset);

    inside_memory_hook = 1;
    void *result = real_mmap(addr, length, prot, flags, fd, offset);

    if (result != MAP_FAILED) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)203,
            (uintptr_t)result, length, fd, "mmap");
    }

    inside_memory_hook = 0;
    return result;
}

int munmap(void *addr, size_t length)
{
    resolve_memory_functions();

    if (!real_munmap) {
        errno = ENOSYS;
        return -1;
    }

    if (inside_memory_hook)
        return real_munmap(addr, length);

    inside_memory_hook = 1;
    int result = real_munmap(addr, length);

    if (result == 0) {
        collector_record_event(
            EVENT_CAT_MEMORY, (event_type_t)204,
            (uintptr_t)addr, length, result, "munmap");
    }

    inside_memory_hook = 0;
    return result;
}
