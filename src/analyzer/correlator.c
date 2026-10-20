#include "analyzer_internal.h"
#include <stdlib.h>
#include <string.h>

/* Convert root_cause_type_t enum to string */
const char *root_cause_to_string(root_cause_type_t cause) {
    switch (cause) {
        case CAUSE_SEGFAULT:            return "CAUSE_SEGFAULT";
        case CAUSE_USE_AFTER_FREE:      return "CAUSE_USE_AFTER_FREE";
        case CAUSE_MEMORY_LEAK:         return "CAUSE_MEMORY_LEAK";
        case CAUSE_DATA_RACE:           return "CAUSE_DATA_RACE";
        case CAUSE_DEADLOCK:            return "CAUSE_DEADLOCK";
        case CAUSE_RESOURCE_EXHAUSTION: return "CAUSE_RESOURCE_EXHAUSTION";
        case CAUSE_IPC_FAILURE:         return "CAUSE_IPC_FAILURE";
        case CAUSE_UNKNOWN:
        default:                        return "CAUSE_UNKNOWN";
    }
}

/* Convert event_category_t enum to string */
const char *event_category_to_string(event_category_t cat) {
    switch (cat) {
        case EVENT_CAT_PROCESS_THREAD: return "EVENT_CAT_PROCESS_THREAD";
        case EVENT_CAT_SYSCALL:        return "EVENT_CAT_SYSCALL";
        case EVENT_CAT_MEMORY:         return "EVENT_CAT_MEMORY";
        case EVENT_CAT_IO:             return "EVENT_CAT_IO";
        case EVENT_CAT_IPC:            return "EVENT_CAT_IPC";
        case EVENT_CAT_SIGNAL:         return "EVENT_CAT_SIGNAL";
        default:                       return "EVENT_CAT_UNKNOWN";
    }
}

/* Convert event_type_t enum to string */
const char *event_type_to_string(event_type_t type) {
    switch (type) {
        case EVENT_THREAD_CREATE:     return "EVENT_THREAD_CREATE";
        case EVENT_THREAD_EXIT:       return "EVENT_THREAD_EXIT";
        case EVENT_THREAD_SWITCH:     return "EVENT_THREAD_SWITCH";
        case EVENT_MEM_ALLOC:         return "EVENT_MEM_ALLOC";
        case EVENT_MEM_FREE:          return "EVENT_MEM_FREE";
        case EVENT_MEM_REALLOC:       return "EVENT_MEM_REALLOC";
        case EVENT_MEM_MMAP:          return "EVENT_MEM_MMAP";
        case EVENT_MEM_MUNMAP:        return "EVENT_MEM_MUNMAP";
        case EVENT_SYS_OPEN:          return "EVENT_SYS_OPEN";
        case EVENT_SYS_CLOSE:         return "EVENT_SYS_CLOSE";
        case EVENT_SYS_READ:          return "EVENT_SYS_READ";
        case EVENT_SYS_WRITE:         return "EVENT_SYS_WRITE";
        case EVENT_IPC_PIPE_WRITE:    return "EVENT_IPC_PIPE_WRITE";
        case EVENT_IPC_PIPE_READ:     return "EVENT_IPC_PIPE_READ";
        case EVENT_IPC_SOCKET_SEND:   return "EVENT_IPC_SOCKET_SEND";
        case EVENT_IPC_SOCKET_RECV:   return "EVENT_IPC_SOCKET_RECV";
        case EVENT_SYNC_MUTEX_LOCK:   return "EVENT_SYNC_MUTEX_LOCK";
        case EVENT_SYNC_MUTEX_UNLOCK: return "EVENT_SYNC_MUTEX_UNLOCK";
        case EVENT_SIG_RECEIVED:      return "EVENT_SIG_RECEIVED";
        default:                      return "EVENT_TYPE_UNKNOWN";
    }
}

/* Comparison function for qsort ensuring strictly monotonic chronological order */
static int compare_events_chronological(const void *a, const void *b) {
    const crash_event_t *ea = (const crash_event_t *)a;
    const crash_event_t *eb = (const crash_event_t *)b;

    if (ea->sequence_id != eb->sequence_id) {
        return (ea->sequence_id < eb->sequence_id) ? -1 : 1;
    }
    if (ea->timestamp_ns != eb->timestamp_ns) {
        return (ea->timestamp_ns < eb->timestamp_ns) ? -1 : 1;
    }
    return 0;
}

int correlator_extract_timeline(const ring_buffer_t *rb,
                                crash_event_t **out_events,
                                size_t *out_count) {
    if (!rb || !out_events || !out_count) {
        return -1;
    }

    *out_events = NULL;
    *out_count = 0;

    size_t count = ring_buffer_get_count(rb);
    if (count == 0) {
        return 0;
    }

    crash_event_t *buf = (crash_event_t *)malloc(count * sizeof(crash_event_t));
    if (!buf) {
        return -1;
    }

    size_t valid = 0;
    for (size_t i = 0; i < count; i++) {
        crash_event_t ev;
        if (ring_buffer_get_at(rb, i, &ev) == 0) {
            buf[valid++] = ev;
        }
    }

    if (valid == 0) {
        free(buf);
        return 0;
    }

    /*
     * Ring buffer snapshot ordering may be newest-to-oldest or oldest-to-newest.
     * We explicitly sort by sequence_id and timestamp_ns to guarantee a true
     * chronological timeline.
     */
    qsort(buf, valid, sizeof(crash_event_t), compare_events_chronological);

    *out_events = buf;
    *out_count = valid;
    return 0;
}

void correlator_free_timeline(crash_event_t *events) {
    if (events) {
        free(events);
    }
}

const crash_event_t *correlator_find_terminal_signal(const crash_event_t *events, size_t count) {
    if (!events || count == 0) {
        return NULL;
    }

    /* Scan backwards from the newest events for a fatal signal */
    for (size_t i = count; i > 0; i--) {
        const crash_event_t *ev = &events[i - 1];
        if (ev->category == EVENT_CAT_SIGNAL || ev->type == EVENT_SIG_RECEIVED) {
            return ev;
        }
    }
    return NULL;
}

size_t correlator_build_causal_chain(const crash_event_t *events,
                                     size_t count,
                                     uint32_t faulting_tid,
                                     uintptr_t faulting_addr,
                                     root_cause_type_t cause,
                                     crash_event_t *out_chain,
                                     size_t max_chain) {
    if (!events || count == 0 || !out_chain || max_chain == 0) {
        return 0;
    }

    /* Temporary buffer to accumulate candidates */
    crash_event_t *candidates = (crash_event_t *)malloc(count * sizeof(crash_event_t));
    if (!candidates) {
        return 0;
    }

    size_t cand_count = 0;

    for (size_t i = 0; i < count; i++) {
        const crash_event_t *ev = &events[i];
        bool relevant = false;

        switch (cause) {
            case CAUSE_USE_AFTER_FREE: {
                /* Include memory operations that overlap or match the faulting address */
                if (ev->category == EVENT_CAT_MEMORY) {
                    uintptr_t block_start = ev->addr;
                    uintptr_t block_end = ev->addr + (ev->size > 0 ? ev->size : 1);
                    if (faulting_addr >= block_start && faulting_addr < block_end) {
                        relevant = true;
                    }
                }
                /* Include thread/signal events on faulting tid */
                if (ev->tid == faulting_tid || ev->type == EVENT_SIG_RECEIVED) {
                    relevant = true;
                }
                break;
            }
            case CAUSE_SEGFAULT: {
                /* Include memory mappings or events on the faulting thread */
                if (ev->tid == faulting_tid) {
                    relevant = true;
                }
                if (ev->type == EVENT_MEM_MMAP || ev->type == EVENT_MEM_MUNMAP || ev->type == EVENT_SIG_RECEIVED) {
                    relevant = true;
                }
                break;
            }
            case CAUSE_DEADLOCK: {
                if (ev->type == EVENT_SYNC_MUTEX_LOCK || ev->type == EVENT_SYNC_MUTEX_UNLOCK) {
                    relevant = true;
                }
                break;
            }
            case CAUSE_RESOURCE_EXHAUSTION: {
                if (ev->category == EVENT_CAT_SYSCALL || ev->category == EVENT_CAT_IO) {
                    relevant = true;
                }
                break;
            }
            case CAUSE_IPC_FAILURE: {
                if (ev->category == EVENT_CAT_IPC || ev->type == EVENT_SIG_RECEIVED) {
                    relevant = true;
                }
                break;
            }
            case CAUSE_MEMORY_LEAK: {
                if (ev->category == EVENT_CAT_MEMORY) {
                    relevant = true;
                }
                break;
            }
            default: {
                /* Fallback: recent events on the faulting thread or signals */
                if (ev->tid == faulting_tid || ev->type == EVENT_SIG_RECEIVED) {
                    relevant = true;
                }
                break;
            }
        }

        if (relevant) {
            candidates[cand_count++] = *ev;
        }
    }

    /*
     * If no filtered events match, take up to max_chain most recent events.
     */
    if (cand_count == 0) {
        size_t start = (count > max_chain) ? (count - max_chain) : 0;
        size_t written = 0;
        for (size_t i = start; i < count && written < max_chain; i++) {
            out_chain[written++] = events[i];
        }
        free(candidates);
        return written;
    }

    /*
     * Select up to max_chain most recent relevant events in chronological order.
     */
    size_t start = (cand_count > max_chain) ? (cand_count - max_chain) : 0;
    size_t written = 0;
    for (size_t i = start; i < cand_count && written < max_chain; i++) {
        out_chain[written++] = candidates[i];
    }

    free(candidates);
    return written;
}
