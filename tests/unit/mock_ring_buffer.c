#include "buffer.h"
#include <stdlib.h>
#include <string.h>

int ring_buffer_init(ring_buffer_t *rb, size_t capacity) {
    if (!rb) return -1;
    if (capacity == 0) capacity = DEFAULT_RING_BUFFER_CAPACITY;

    rb->events = (crash_event_t *)calloc(capacity, sizeof(crash_event_t));
    if (!rb->events) return -1;

    rb->capacity = capacity;
    rb->head = 0;
    rb->count = 0;
    rb->next_seq = 1;
    rb->frozen = false;
    return 0;
}

void ring_buffer_destroy(ring_buffer_t *rb) {
    if (!rb) return;
    if (rb->events) {
        free(rb->events);
        rb->events = NULL;
    }
    rb->capacity = 0;
    rb->head = 0;
    rb->count = 0;
    rb->next_seq = 0;
    rb->frozen = false;
}

void ring_buffer_record(ring_buffer_t *rb, const crash_event_t *ev) {
    if (!rb || !ev || !rb->events || rb->frozen) return;

    crash_event_t item = *ev;
    if (item.sequence_id == 0) {
        item.sequence_id = rb->next_seq;
    }
    rb->next_seq++;

    rb->events[rb->head] = item;
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }
}

void ring_buffer_freeze(ring_buffer_t *rb) {
    if (rb) {
        rb->frozen = true;
    }
}

bool ring_buffer_is_frozen(const ring_buffer_t *rb) {
    return rb ? rb->frozen : false;
}

size_t ring_buffer_get_count(const ring_buffer_t *rb) {
    return rb ? rb->count : 0;
}

int ring_buffer_get_at(const ring_buffer_t *rb, size_t index, crash_event_t *out_ev) {
    if (!rb || !out_ev || !rb->events || index >= rb->count) {
        return -1;
    }
    /* Index 0 represents the oldest preserved event in circular history */
    size_t oldest = (rb->head + rb->capacity - rb->count) % rb->capacity;
    size_t slot = (oldest + index) % rb->capacity;
    *out_ev = rb->events[slot];
    return 0;
}
