#ifndef CRASH_ANALYZER_INTERNAL_H
#define CRASH_ANALYZER_INTERNAL_H

#include "engine.h"
#include "event.h"
#include "buffer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* String conversions for reporting and debugging */
const char *root_cause_to_string(root_cause_type_t cause);
const char *event_category_to_string(event_category_t cat);
const char *event_type_to_string(event_type_t type);

/*
 * Correlator APIs:
 * Extracts all valid events from the ring buffer into a newly allocated,
 * chronologically sorted array. Caller must release with correlator_free_timeline.
 */
int correlator_extract_timeline(const ring_buffer_t *rb,
                                crash_event_t **out_events,
                                size_t *out_count);

void correlator_free_timeline(crash_event_t *events);

/*
 * Scans chronological timeline for the latest fatal signal event (EVENT_SIG_RECEIVED).
 * Returns NULL if no signal was logged.
 */
const crash_event_t *correlator_find_terminal_signal(const crash_event_t *events, size_t count);

/*
 * Selects up to max_chain causally linked events relevant to the diagnosed cause,
 * ordered chronologically. Returns count of events written to out_chain.
 */
size_t correlator_build_causal_chain(const crash_event_t *events,
                                     size_t count,
                                     uint32_t faulting_tid,
                                     uintptr_t faulting_addr,
                                     root_cause_type_t cause,
                                     crash_event_t *out_chain,
                                     size_t max_chain);

/*
 * Causal Graph / Wait-For-Graph (WFG) APIs:
 * Analyzes mutex events, tracks lock ownership and wait states,
 * and detects directed dependency cycles (deadlocks).
 */
typedef struct {
    bool cycle_detected;
    size_t cycle_length;
    uint32_t cycle_tids[16];
    uintptr_t cycle_locks[16];
    char description[256];
} causal_graph_result_t;

int causal_graph_detect_deadlock(const crash_event_t *events,
                                 size_t count,
                                 causal_graph_result_t *out_result);

#ifdef __cplusplus

}
#endif

#endif /* CRASH_ANALYZER_INTERNAL_H */
