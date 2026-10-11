#ifndef CRASH_ENGINE_H
#define CRASH_ENGINE_H

#include "event.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    root_cause_type_t probable_cause;
    float confidence_level;         /* 0.0 to 1.0 (e.g. 0.95 = 95%) */
    uint32_t faulting_tid;
    uintptr_t faulting_addr;
    char explanation[256];
    
    /* Top correlated suspicious events in chronological causal order */
    crash_event_t causal_chain[16];
    size_t causal_chain_len;
} crash_analysis_result_t;

/* Analyzes the preserved ring buffer to reconstruct root cause */
int analyze_crash(const ring_buffer_t *rb, crash_analysis_result_t *out_result);

/* Outputs formatted crash report to file/stdout */
int generate_crash_report(const crash_analysis_result_t *result, const char *output_file);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_ENGINE_H */
