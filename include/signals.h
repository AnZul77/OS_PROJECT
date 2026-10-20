#ifndef CRASH_SIGNALS_H
#define CRASH_SIGNALS_H

#include "collector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Signal strings and human-readable names */
const char* crash_signal_name(int signo);
const char* crash_signal_code_name(int signo, int code);

/* Handler lifecycle functions (implemented in src/collector/signal_handler.c) */
int signal_handler_install(ring_buffer_t *rb);
void signal_handler_uninstall(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_SIGNALS_H */
