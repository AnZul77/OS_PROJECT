#include "analyzer_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHAIN_LEN 16

/* Helper checking if address falls within allocated/freed block */
static bool address_in_block(uintptr_t target_addr, uintptr_t block_base, size_t block_size) {
    size_t effective_size = (block_size > 0) ? block_size : 1;
    return (target_addr >= block_base) && (target_addr < (block_base + effective_size));
}

/*
 * Heuristic 1: Use-After-Free Detector
 * Confirms whether the crash address matches a memory block freed prior to the fault,
 * with no intervening reallocation of that address.
 */
static bool detect_use_after_free(const crash_event_t *events,
                                  size_t count,
                                  const crash_event_t *sig_ev,
                                  crash_analysis_result_t *out) {
    if (!sig_ev || count == 0) {
        return false;
    }

    uintptr_t fault_addr = sig_ev->addr;

    /* Scan backwards from the signal event */
    int free_idx = -1;
    for (int i = (int)count - 1; i >= 0; i--) {
        const crash_event_t *ev = &events[i];
        if (ev->sequence_id >= sig_ev->sequence_id) {
            continue;
        }

        if (ev->category == EVENT_CAT_MEMORY) {
            if (ev->type == EVENT_MEM_FREE || ev->type == EVENT_MEM_MUNMAP) {
                if (address_in_block(fault_addr, ev->addr, ev->size)) {
                    free_idx = i;
                    break;
                }
            }
        }
    }

    if (free_idx < 0) {
        return false;
    }

    /* Check for intervening reallocation */
    const crash_event_t *free_ev = &events[free_idx];
    for (size_t i = free_idx + 1; i < count; i++) {
        const crash_event_t *ev = &events[i];
        if (ev->sequence_id >= sig_ev->sequence_id) {
            break;
        }
        if (ev->category == EVENT_CAT_MEMORY &&
            (ev->type == EVENT_MEM_ALLOC || ev->type == EVENT_MEM_REALLOC || ev->type == EVENT_MEM_MMAP)) {
            if (address_in_block(fault_addr, ev->addr, ev->size)) {
                /* Address was legitimately reallocated before the crash; not a direct UAF */
                return false;
            }
        }
    }

    out->probable_cause = CAUSE_USE_AFTER_FREE;
    out->confidence_level = 0.95f; /* Estimated heuristic confidence */
    out->faulting_tid = sig_ev->tid;
    out->faulting_addr = fault_addr;

    snprintf(out->explanation, sizeof(out->explanation),
             "TID %u accessed address 0x%llx, which was previously freed by TID %u at seq %llu. Reconstructed as Use-After-Free.",
             (unsigned int)sig_ev->tid,
             (unsigned long long)fault_addr,
             (unsigned int)free_ev->tid,
             (unsigned long long)free_ev->sequence_id);

    out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                          out->faulting_tid,
                                                          out->faulting_addr,
                                                          CAUSE_USE_AFTER_FREE,
                                                          out->causal_chain,
                                                          MAX_CHAIN_LEN);
    return true;
}

/*
 * Heuristic 2: Low-Address / Null Pointer Dereference Detector
 */
static bool detect_low_address_segfault(const crash_event_t *events,
                                        size_t count,
                                        const crash_event_t *sig_ev,
                                        crash_analysis_result_t *out) {
    if (!sig_ev) {
        return false;
    }

    /* Standard low page boundary: < 4096 (0x1000) */
    if (sig_ev->addr < 0x1000) {
        out->probable_cause = CAUSE_SEGFAULT;
        out->confidence_level = 0.98f; /* Estimated heuristic confidence */
        out->faulting_tid = sig_ev->tid;
        out->faulting_addr = sig_ev->addr;

        if (sig_ev->addr == 0) {
            snprintf(out->explanation, sizeof(out->explanation),
                     "TID %u attempted null pointer dereference at address 0x0.",
                     (unsigned int)sig_ev->tid);
        } else {
            snprintf(out->explanation, sizeof(out->explanation),
                     "TID %u attempted invalid memory dereference in protected zero-page at offset 0x%llx.",
                     (unsigned int)sig_ev->tid,
                     (unsigned long long)sig_ev->addr);
        }

        out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                              out->faulting_tid,
                                                              out->faulting_addr,
                                                              CAUSE_SEGFAULT,
                                                              out->causal_chain,
                                                              MAX_CHAIN_LEN);
        return true;
    }
    return false;
}

/*
 * Heuristic 3: IPC Broken Pipe Failure Detector
 */
static bool detect_ipc_failure(const crash_event_t *events,
                               size_t count,
                               const crash_event_t *sig_ev,
                               crash_analysis_result_t *out) {
    bool has_sigpipe = (sig_ev && (sig_ev->fd_or_status == 13 /* SIGPIPE */ ||
                                   strstr(sig_ev->label, "SIGPIPE") != NULL));

    int last_ipc_fd = -1;
    bool write_to_closed_ipc = false;

    for (size_t i = 0; i < count; i++) {
        const crash_event_t *ev = &events[i];
        if (ev->category == EVENT_CAT_IPC) {
            last_ipc_fd = ev->fd_or_status;
            if (ev->type == EVENT_IPC_PIPE_WRITE || ev->type == EVENT_IPC_SOCKET_SEND) {
                if (ev->fd_or_status < 0) {
                    write_to_closed_ipc = true;
                }
            }
        }
    }

    if (has_sigpipe || write_to_closed_ipc) {
        out->probable_cause = CAUSE_IPC_FAILURE;
        out->confidence_level = 0.90f; /* Estimated heuristic confidence */
        out->faulting_tid = sig_ev ? sig_ev->tid : (events ? events[count - 1].tid : 0);
        out->faulting_addr = sig_ev ? sig_ev->addr : 0;

        snprintf(out->explanation, sizeof(out->explanation),
                 "Fatal IPC communication failure (broken pipe / closed socket write) detected on FD %d.",
                 last_ipc_fd);

        out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                              out->faulting_tid,
                                                              out->faulting_addr,
                                                              CAUSE_IPC_FAILURE,
                                                              out->causal_chain,
                                                              MAX_CHAIN_LEN);
        return true;
    }
    return false;
}

/*
 * Heuristic 4: Resource Exhaustion (Too many open files or memory bounds)
 */
static bool detect_resource_exhaustion(const crash_event_t *events,
                                       size_t count,
                                       const crash_event_t *sig_ev,
                                       crash_analysis_result_t *out) {
    size_t open_count = 0;
    size_t close_count = 0;
    bool emfile_seen = false;

    for (size_t i = 0; i < count; i++) {
        const crash_event_t *ev = &events[i];
        if (ev->type == EVENT_SYS_OPEN) {
            if (ev->fd_or_status < 0) {
                /* Error return code from open (EMFILE = 24 on Linux) */
                emfile_seen = true;
            } else {
                open_count++;
            }
        } else if (ev->type == EVENT_SYS_CLOSE) {
            close_count++;
        }
    }

    if (emfile_seen || (open_count >= 100 && close_count == 0)) {
        out->probable_cause = CAUSE_RESOURCE_EXHAUSTION;
        out->confidence_level = 0.92f; /* Estimated heuristic confidence */
        out->faulting_tid = sig_ev ? sig_ev->tid : events[count - 1].tid;
        out->faulting_addr = sig_ev ? sig_ev->addr : 0;

        snprintf(out->explanation, sizeof(out->explanation),
                 "Resource exhaustion: %zu open descriptors with insufficient closures prior to failure.",
                 open_count);

        out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                              out->faulting_tid,
                                                              out->faulting_addr,
                                                              CAUSE_RESOURCE_EXHAUSTION,
                                                              out->causal_chain,
                                                              MAX_CHAIN_LEN);
        return true;
    }
    return false;
}

/*
 * Heuristic 5: Circular Deadlock Detector
 * Uses Wait-For-Graph (WFG) analysis from causal_graph.c
 */
static bool detect_deadlock(const crash_event_t *events,
                            size_t count,
                            crash_analysis_result_t *out) {
    causal_graph_result_t graph_res;
    if (causal_graph_detect_deadlock(events, count, &graph_res) != 0) {
        return false;
    }

    if (graph_res.cycle_detected && graph_res.cycle_length >= 2) {
        out->probable_cause = CAUSE_DEADLOCK;
        out->confidence_level = 0.95f; /* Estimated heuristic confidence */
        out->faulting_tid = graph_res.cycle_tids[0];
        out->faulting_addr = graph_res.cycle_locks[0];

        snprintf(out->explanation, sizeof(out->explanation),
                 "Wait-For-Graph deadlock: %.200s",
                 graph_res.description);

        out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                              out->faulting_tid,
                                                              out->faulting_addr,
                                                              CAUSE_DEADLOCK,
                                                              out->causal_chain,
                                                              MAX_CHAIN_LEN);
        return true;
    }
    return false;
}

/*
 * Heuristic 6: Memory Leak Detector
 */
static bool detect_memory_leak(const crash_event_t *events,
                               size_t count,
                               crash_analysis_result_t *out) {
    size_t alloc_count = 0;
    size_t free_count = 0;
    size_t total_alloc_bytes = 0;

    for (size_t i = 0; i < count; i++) {
        const crash_event_t *ev = &events[i];
        if (ev->type == EVENT_MEM_ALLOC || ev->type == EVENT_MEM_MMAP) {
            alloc_count++;
            total_alloc_bytes += ev->size;
        } else if (ev->type == EVENT_MEM_FREE || ev->type == EVENT_MEM_MUNMAP) {
            free_count++;
        }
    }

    if (alloc_count >= 10 && free_count == 0) {
        out->probable_cause = CAUSE_MEMORY_LEAK;
        out->confidence_level = 0.85f; /* Estimated heuristic confidence */
        out->faulting_tid = events[count - 1].tid;
        out->faulting_addr = events[count - 1].addr;

        snprintf(out->explanation, sizeof(out->explanation),
                 "Monotonic heap growth: %zu allocations (%zu bytes) with 0 deallocations recorded.",
                 alloc_count, total_alloc_bytes);

        out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                              out->faulting_tid,
                                                              out->faulting_addr,
                                                              CAUSE_MEMORY_LEAK,
                                                              out->causal_chain,
                                                              MAX_CHAIN_LEN);
        return true;
    }
    return false;
}

/*
 * Heuristic 7: Generic High-Address Segmentation Fault (no matched free)
 */
static bool detect_high_address_segfault(const crash_event_t *events,
                                         size_t count,
                                         const crash_event_t *sig_ev,
                                         crash_analysis_result_t *out) {
    if (!sig_ev) {
        return false;
    }

    out->probable_cause = CAUSE_SEGFAULT;
    out->confidence_level = 0.70f; /* Lower confidence: unmapped address with no prior allocator record */
    out->faulting_tid = sig_ev->tid;
    out->faulting_addr = sig_ev->addr;

    snprintf(out->explanation, sizeof(out->explanation),
             "TID %u crashed with fatal signal at address 0x%llx. No preceding allocator event matched this address in history.",
             (unsigned int)sig_ev->tid,
             (unsigned long long)sig_ev->addr);

    out->causal_chain_len = correlator_build_causal_chain(events, count,
                                                          out->faulting_tid,
                                                          out->faulting_addr,
                                                          CAUSE_SEGFAULT,
                                                          out->causal_chain,
                                                          MAX_CHAIN_LEN);
    return true;
}

/* Public analyze_crash implementation declared in engine.h */
int analyze_crash(const ring_buffer_t *rb, crash_analysis_result_t *out_result) {
    if (!out_result) {
        return -1;
    }

    /* Initialize all fields to safe defaults */
    memset(out_result, 0, sizeof(*out_result));
    out_result->probable_cause = CAUSE_UNKNOWN;
    out_result->confidence_level = 0.0f;
    out_result->faulting_tid = 0;
    out_result->faulting_addr = 0;
    out_result->causal_chain_len = 0;
    out_result->explanation[0] = '\0';

    if (!rb) {
        snprintf(out_result->explanation, sizeof(out_result->explanation),
                 "Null ring buffer pointer provided.");
        return 0;
    }

    size_t raw_count = ring_buffer_get_count(rb);
    if (raw_count == 0) {
        snprintf(out_result->explanation, sizeof(out_result->explanation),
                 "Empty event history in ring buffer. Cannot reconstruct crash cause.");
        return 0;
    }

    crash_event_t *events = NULL;
    size_t count = 0;
    if (correlator_extract_timeline(rb, &events, &count) != 0 || count == 0 || !events) {
        snprintf(out_result->explanation, sizeof(out_result->explanation),
                 "Failed to extract timeline from ring buffer.");
        return 0;
    }

    const crash_event_t *sig_ev = correlator_find_terminal_signal(events, count);

    /* Run heuristics in order of specificity and diagnostic certainty */
    bool diagnosed = false;

    if (sig_ev) {
        /* If a fatal signal was captured, test signal-dependent failure patterns */
        if (detect_use_after_free(events, count, sig_ev, out_result)) {
            diagnosed = true;
        } else if (detect_ipc_failure(events, count, sig_ev, out_result)) {
            diagnosed = true;
        } else if (detect_low_address_segfault(events, count, sig_ev, out_result)) {
            diagnosed = true;
        } else if (detect_resource_exhaustion(events, count, sig_ev, out_result)) {
            diagnosed = true;
        } else if (detect_high_address_segfault(events, count, sig_ev, out_result)) {
            diagnosed = true;
        }
    } else {
        /* Non-signal patterns: deadlocks, leaks, resource exhaustion without SIGSEGV */
        if (detect_deadlock(events, count, out_result)) {
            diagnosed = true;
        } else if (detect_resource_exhaustion(events, count, NULL, out_result)) {
            diagnosed = true;
        } else if (detect_memory_leak(events, count, out_result)) {
            diagnosed = true;
        }
    }

    /* Fallback if no heuristic matched or evidence is inconclusive */
    if (!diagnosed) {
        out_result->probable_cause = CAUSE_UNKNOWN;
        out_result->confidence_level = 0.05f; /* Minimal confidence reflecting inconclusive data */
        out_result->faulting_tid = events[count - 1].tid;
        out_result->faulting_addr = events[count - 1].addr;

        snprintf(out_result->explanation, sizeof(out_result->explanation),
                 "Event history does not exhibit a recognized failure pattern or fatal crash signal.");

        out_result->causal_chain_len = correlator_build_causal_chain(events, count,
                                                                     out_result->faulting_tid,
                                                                     out_result->faulting_addr,
                                                                     CAUSE_UNKNOWN,
                                                                     out_result->causal_chain,
                                                                     MAX_CHAIN_LEN);
    }

    correlator_free_timeline(events);
    return 0;
}
