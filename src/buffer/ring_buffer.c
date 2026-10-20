#include "buffer.h"
#include "collector.h"
#include <stdlib.h>
#include <string.h>

int ring_buffer_init(ring_buffer_t *rb, size_t capacity) {
    if (!rb) {
        return -1;
    }
    
    if (capacity == 0) {
        capacity = DEFAULT_RING_BUFFER_CAPACITY;
    }
    
    rb->events = (crash_event_t *)calloc(capacity, sizeof(crash_event_t));
    if (!rb->events) {
        return -1;
    }
    
    rb->capacity = capacity;
    rb->head = 0;
    rb->count = 0;
    rb->next_seq = 1;
    rb->frozen = false;
    
    if (pthread_mutex_init(&rb->lock, NULL) != 0) {
        free(rb->events);
        rb->events = NULL;
        return -1;
    }
    
    return 0;
}

void ring_buffer_destroy(ring_buffer_t *rb) {
    if (!rb) {
        return;
    }
    
    pthread_mutex_lock(&rb->lock);
    rb->frozen = true;
    if (rb->events) {
        free(rb->events);
        rb->events = NULL;
    }
    rb->capacity = 0;
    rb->count = 0;
    rb->head = 0;
    pthread_mutex_unlock(&rb->lock);
    
    pthread_mutex_destroy(&rb->lock);
}

void ring_buffer_record(ring_buffer_t *rb, const crash_event_t *ev) {
    /* Fast lock-free exit if buffer is frozen or uninitialized */
    if (!rb || rb->frozen || !ev || !rb->events) {
        return;
    }
    
    pthread_mutex_lock(&rb->lock);
    
    /* Double check after lock acquisition */
    if (rb->frozen) {
        pthread_mutex_unlock(&rb->lock);
        return;
    }
    
    size_t slot = rb->head;
    rb->events[slot] = *ev;
    
    /* Assign sequential ordering ID */
    rb->events[slot].sequence_id = rb->next_seq++;
    
    /* Populate timestamp if caller did not supply one */
    if (rb->events[slot].timestamp_ns == 0) {
        rb->events[slot].timestamp_ns = crash_get_timestamp_ns();
    }
    
    /* Populate TID if caller did not supply one */
    if (rb->events[slot].tid == 0) {
        rb->events[slot].tid = crash_get_current_tid();
    }
    
    /* Advance head and wrap around */
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }
    
    pthread_mutex_unlock(&rb->lock);
}

void ring_buffer_freeze(ring_buffer_t *rb) {
    if (!rb) {
        return;
    }
    
    /* Memory barrier ensure writes prior to freeze are visible */
    __sync_synchronize();
    rb->frozen = true;
    __sync_synchronize();
}

bool ring_buffer_is_frozen(const ring_buffer_t *rb) {
    if (!rb) {
        return false;
    }
    return rb->frozen;
}

size_t ring_buffer_get_count(const ring_buffer_t *rb) {
    if (!rb) {
        return 0;
    }
    return rb->count;
}

int ring_buffer_get_at(const ring_buffer_t *rb, size_t index, crash_event_t *out_ev) {
    if (!rb || !out_ev || index >= rb->count || !rb->events) {
        return -1;
    }
    
    size_t slot;
    if (rb->count < rb->capacity) {
        /* Buffer has not wrapped yet; index 0 is at slot 0 */
        slot = index;
    } else {
        /* Buffer wrapped; oldest element is at rb->head */
        slot = (rb->head + index) % rb->capacity;
    }
    
    *out_ev = rb->events[slot];
    return 0;
}
