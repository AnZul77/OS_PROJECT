
#define _GNU_SOURCE

#include "collector.h"
#include "signals.h"
#include "buffer.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

static ring_buffer_t event_buffer;
static pthread_mutex_t collector_mutex = PTHREAD_MUTEX_INITIALIZER;
static int collector_ready = 0;
static __thread int inside_collector = 0;

static uint64_t current_time_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000000000ULL
         + (uint64_t)ts.tv_nsec;
}

int collector_init(void)
{
    if (collector_ready) {
        return 0;
    }

    if (ring_buffer_init(&event_buffer,
                         DEFAULT_RING_BUFFER_CAPACITY) != 0) {
        return -1;
    }

    signal_handler_install(&event_buffer);
    collector_ready = 1;
    return 0;
}

void collector_record_event(
    event_category_t category,
    event_type_t type,
    uintptr_t addr,
    size_t size,
    int32_t status,
    const char *label)
{
    if (inside_collector) {
        return;
    }

    inside_collector = 1;

    if (!collector_ready && collector_init() != 0) {
        inside_collector = 0;
        return;
    }

    crash_event_t event;
    memset(&event, 0, sizeof(event));

    event.timestamp_ns = current_time_ns();
    event.pid = (uint32_t)getpid();
    event.tid = (uint32_t)syscall(SYS_gettid);
    event.category = category;
    event.type = type;
    event.addr = addr;
    event.size = size;
    event.fd_or_status = status;

    if (label != NULL) {
        snprintf(event.label, sizeof(event.label), "%s", label);
    }

    pthread_mutex_lock(&collector_mutex);
    if (collector_ready) {
        ring_buffer_record(&event_buffer, &event);
    }
    pthread_mutex_unlock(&collector_mutex);

    inside_collector = 0;
}

void collector_shutdown(void)
{
    if (!collector_ready) {
        return;
    }

    inside_collector = 1;

    pthread_mutex_lock(&collector_mutex);

    size_t count = ring_buffer_get_count(&event_buffer);

    fprintf(stderr, "\n=== Crash Archaeologist: Captured Events ===\n");
    fprintf(stderr, "Total events: %zu\n", count);

    for (size_t i = 0; i < count; i++) {
        crash_event_t event;

        if (ring_buffer_get_at(&event_buffer, i, &event) == 0) {
            fprintf(stderr,
                    "PID=%u TID=%u TIME=%llu TYPE=%d "
                    "ADDR=%p SIZE=%zu STATUS=%d LABEL=%s\n",
                    event.pid,
                    event.tid,
                    (unsigned long long)event.timestamp_ns,
                    (int)event.type,
                    (void *)event.addr,
                    event.size,
                    event.fd_or_status,
                    event.label);
        }
    }

    ring_buffer_destroy(&event_buffer);
    collector_ready = 0;

    pthread_mutex_unlock(&collector_mutex);
    inside_collector = 0;
}

__attribute__((constructor))
static void collector_library_start(void)
{
    collector_init();
}

__attribute__((destructor))
static void collector_library_stop(void)
{
    collector_shutdown();
}
