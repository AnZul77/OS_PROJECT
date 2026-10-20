#include "analyzer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GRAPH_THREADS 64
#define MAX_GRAPH_LOCKS   128
#define MAX_CYCLE_DEPTH   16

typedef struct {
    uintptr_t addr;
    uint32_t owner_tid;         /* 0 if free/unowned */
    uint64_t acquired_seq;
} lock_state_t;

typedef struct {
    uint32_t tid;
    uintptr_t waiting_for_lock; /* 0 if not waiting */
    uint32_t waiting_for_tid;  /* TID of thread holding the lock */
} thread_node_t;

static int find_or_add_thread(thread_node_t *threads, size_t *thread_count, uint32_t tid) {
    if (tid == 0) return -1;
    for (size_t i = 0; i < *thread_count; i++) {
        if (threads[i].tid == tid) {
            return (int)i;
        }
    }
    if (*thread_count >= MAX_GRAPH_THREADS) {
        return -1;
    }
    int idx = (int)(*thread_count);
    threads[idx].tid = tid;
    threads[idx].waiting_for_lock = 0;
    threads[idx].waiting_for_tid = 0;
    (*thread_count)++;
    return idx;
}

static int find_or_add_lock(lock_state_t *locks, size_t *lock_count, uintptr_t addr) {
    if (addr == 0) return -1;
    for (size_t i = 0; i < *lock_count; i++) {
        if (locks[i].addr == addr) {
            return (int)i;
        }
    }
    if (*lock_count >= MAX_GRAPH_LOCKS) {
        return -1;
    }
    int idx = (int)(*lock_count);
    locks[idx].addr = addr;
    locks[idx].owner_tid = 0;
    locks[idx].acquired_seq = 0;
    (*lock_count)++;
    return idx;
}

int causal_graph_detect_deadlock(const crash_event_t *events,
                                 size_t count,
                                 causal_graph_result_t *out_result) {
    if (!out_result) {
        return -1;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->cycle_detected = false;
    out_result->cycle_length = 0;
    snprintf(out_result->description, sizeof(out_result->description), "No deadlock cycle detected.");

    if (!events || count == 0) {
        return 0;
    }

    thread_node_t threads[MAX_GRAPH_THREADS];
    size_t thread_count = 0;

    lock_state_t locks[MAX_GRAPH_LOCKS];
    size_t lock_count = 0;

    /*
     * Step 1: Sequential Timeline Simulation of Mutex Primitives
     * Conservative handling:
     * - If a thread attempts to acquire a lock already owned by another thread,
     *   it registers a Wait-For dependency (T_wait -> T_owner on Lock).
     * - When an unlock occurs, lock ownership is cleared and resolved waits are lifted.
     */
    for (size_t i = 0; i < count; i++) {
        const crash_event_t *ev = &events[i];
        if (ev->type != EVENT_SYNC_MUTEX_LOCK &&
            ev->type != EVENT_SYNC_MUTEX_UNLOCK) {
            continue;
        }

        uint32_t tid = ev->tid;
        uintptr_t lock_addr = ev->addr;
        if (tid == 0 || lock_addr == 0) {
            continue;
        }

        int t_idx = find_or_add_thread(threads, &thread_count, tid);
        int l_idx = find_or_add_lock(locks, &lock_count, lock_addr);
        if (t_idx < 0 || l_idx < 0) {
            continue;
        }

        if (ev->type == EVENT_SYNC_MUTEX_LOCK) {
            uint32_t current_owner = locks[l_idx].owner_tid;
            if (current_owner == 0 || current_owner == tid) {
                /* Lock is acquired by this thread */
                locks[l_idx].owner_tid = tid;
                locks[l_idx].acquired_seq = ev->sequence_id;
                threads[t_idx].waiting_for_lock = 0;
                threads[t_idx].waiting_for_tid = 0;
            } else {
                /*
                 * Lock is already held by another thread.
                 * This thread is now blocked waiting for current_owner.
                 */
                threads[t_idx].waiting_for_lock = lock_addr;
                threads[t_idx].waiting_for_tid = current_owner;
            }
        } else if (ev->type == EVENT_SYNC_MUTEX_UNLOCK) {
            /* If this thread was the recorded owner (or owner was unknown), release it */
            if (locks[l_idx].owner_tid == tid || locks[l_idx].owner_tid == 0) {
                locks[l_idx].owner_tid = 0;
                locks[l_idx].acquired_seq = 0;
            }
            /* The lock is now unlocked; any wait-for edge on this lock is resolved */
            for (size_t t = 0; t < thread_count; t++) {
                if (threads[t].waiting_for_lock == lock_addr) {
                    threads[t].waiting_for_lock = 0;
                    threads[t].waiting_for_tid = 0;
                }
            }
        }
    }

    if (thread_count < 2) {
        return 0;
    }

    /*
     * Step 2: Directed Cycle Detection on the Wait-For-Graph (WFG)
     * Every thread has out-degree <= 1 (waiting_for_tid).
     * We use path-based DFS traversal with cycle reconstruction.
     */
    for (size_t start = 0; start < thread_count; start++) {
        if (threads[start].waiting_for_tid == 0) {
            continue;
        }

        int path[MAX_GRAPH_THREADS];
        uintptr_t path_locks[MAX_GRAPH_THREADS];
        size_t path_len = 0;

        int curr = (int)start;
        bool in_path = false;
        size_t cycle_start_index = 0;

        while (curr >= 0 && threads[curr].waiting_for_tid != 0 && path_len < MAX_GRAPH_THREADS) {
            /* Check if curr is already in path */
            for (size_t p = 0; p < path_len; p++) {
                if (path[p] == curr) {
                    in_path = true;
                    cycle_start_index = p;
                    break;
                }
            }
            if (in_path) {
                break;
            }

            path[path_len] = curr;
            path_locks[path_len] = threads[curr].waiting_for_lock;
            path_len++;

            uint32_t target_tid = threads[curr].waiting_for_tid;
            int next = -1;
            for (size_t j = 0; j < thread_count; j++) {
                if (threads[j].tid == target_tid) {
                    next = (int)j;
                    break;
                }
            }
            curr = next;
        }

        if (in_path) {
            /* Cycle detected! Reconstruct the cycle */
            size_t cycle_len = path_len - cycle_start_index;
            if (cycle_len >= 2) {
                out_result->cycle_detected = true;
                out_result->cycle_length = cycle_len;

                size_t store_len = (cycle_len > MAX_CYCLE_DEPTH) ? MAX_CYCLE_DEPTH : cycle_len;

                char desc_buf[256];
                int offset = snprintf(desc_buf, sizeof(desc_buf), "Deadlock cycle: ");

                for (size_t c = 0; c < store_len; c++) {
                    int node_idx = path[cycle_start_index + c];
                    out_result->cycle_tids[c] = threads[node_idx].tid;
                    out_result->cycle_locks[c] = path_locks[cycle_start_index + c];

                    offset += snprintf(desc_buf + offset,
                                       (sizeof(desc_buf) > (size_t)offset) ? sizeof(desc_buf) - offset : 0,
                                       "T%u -[0x%llx]-> ",
                                       (unsigned int)threads[node_idx].tid,
                                       (unsigned long long)path_locks[cycle_start_index + c]);
                }

                if (cycle_len > MAX_CYCLE_DEPTH) {
                    offset += snprintf(desc_buf + offset,
                                       (sizeof(desc_buf) > (size_t)offset) ? sizeof(desc_buf) - offset : 0,
                                       "... (%zu threads) -> ", cycle_len);
                }

                /* Close loop in description */
                int first_idx = path[cycle_start_index];
                snprintf(desc_buf + offset,
                         (sizeof(desc_buf) > (size_t)offset) ? sizeof(desc_buf) - offset : 0,
                         "T%u", (unsigned int)threads[first_idx].tid);

                strncpy(out_result->description, desc_buf, sizeof(out_result->description) - 1);
                out_result->description[sizeof(out_result->description) - 1] = '\0';
                return 0;
            }
        }
    }

    return 0;
}
