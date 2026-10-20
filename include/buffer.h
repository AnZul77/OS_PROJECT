#ifndef CRASH_BUFFER_H
#define CRASH_BUFFER_H

#include "event.h"
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_RING_BUFFER_CAPACITY 8192

typedef struct {
    crash_event_t *events;
    size_t capacity;
    size_t head;                /* Next write index */
    size_t count;               /* Total valid items currently stored */
    uint64_t next_seq;          /* Next sequence id */
    volatile bool frozen;       /* When true, buffer preserves history and forbids writes */
    pthread_mutex_t lock;       /* Mutex guarding head/count during multithreaded writes */
} ring_buffer_t;

/* Ring buffer lifecycle API */
int ring_buffer_init(ring_buffer_t *rb, size_t capacity);
void ring_buffer_destroy(ring_buffer_t *rb);

/* Recording and state management */
void ring_buffer_record(ring_buffer_t *rb, const crash_event_t *ev);
void ring_buffer_freeze(ring_buffer_t *rb);
bool ring_buffer_is_frozen(const ring_buffer_t *rb);

/* Snapshot iteration (from oldest to newest or newest to oldest) */
size_t ring_buffer_get_count(const ring_buffer_t *rb);
int ring_buffer_get_at(const ring_buffer_t *rb, size_t index, crash_event_t *out_ev);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_BUFFER_H */
