
#define _GNU_SOURCE

#include "collector.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>

static __thread int inside_thread_hook = 0;

static int (*real_pthread_create)(
    pthread_t *, const pthread_attr_t *,
    void *(*)(void *), void *);

static int (*real_pthread_join)(pthread_t, void **);

static int (*real_pthread_mutex_lock)(pthread_mutex_t *);
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *);

static void resolve_thread_functions(void)
{
    if (!real_pthread_create)
        real_pthread_create =
            dlsym(RTLD_NEXT, "pthread_create");

    if (!real_pthread_join)
        real_pthread_join =
            dlsym(RTLD_NEXT, "pthread_join");

    if (!real_pthread_mutex_lock)
        real_pthread_mutex_lock =
            dlsym(RTLD_NEXT, "pthread_mutex_lock");

    if (!real_pthread_mutex_unlock)
        real_pthread_mutex_unlock =
            dlsym(RTLD_NEXT, "pthread_mutex_unlock");
}

int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg)
{
    resolve_thread_functions();

    if (!real_pthread_create)
        return -1;

    int result = real_pthread_create(
        thread, attr, start_routine, arg);

    if (result == 0 && !inside_thread_hook) {
        inside_thread_hook = 1;
        collector_record_event(
            EVENT_CAT_PROCESS_THREAD, (event_type_t)100,
            (uintptr_t)*thread, 0, result, "pthread_create");
        inside_thread_hook = 0;
    }

    return result;
}

int pthread_join(pthread_t thread, void **retval)
{
    resolve_thread_functions();

    if (!real_pthread_join)
        return -1;

    int result = real_pthread_join(thread, retval);

    if (!inside_thread_hook) {
        inside_thread_hook = 1;
        collector_record_event(
            EVENT_CAT_PROCESS_THREAD, (event_type_t)101,
            (uintptr_t)thread, 0, result, "pthread_join");
        inside_thread_hook = 0;
    }

    return result;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    resolve_thread_functions();

    if (!real_pthread_mutex_lock)
        return -1;

    int result = real_pthread_mutex_lock(mutex);

    if (result == 0 && !inside_thread_hook) {
        inside_thread_hook = 1;
        collector_record_event(
            EVENT_CAT_IPC, (event_type_t)404,
            (uintptr_t)mutex, 0, result, "mutex_lock");
        inside_thread_hook = 0;
    }

    return result;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    resolve_thread_functions();

    if (!real_pthread_mutex_unlock)
        return -1;

    int result = real_pthread_mutex_unlock(mutex);

    if (result == 0 && !inside_thread_hook) {
        inside_thread_hook = 1;
        collector_record_event(
            EVENT_CAT_IPC, (event_type_t)405,
            (uintptr_t)mutex, 0, result, "mutex_unlock");
        inside_thread_hook = 0;
    }

    return result;
}
