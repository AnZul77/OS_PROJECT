#ifndef CRASH_COLLECTOR_H
#define CRASH_COLLECTOR_H

#include "event.h"
#include "buffer.h"
#include <signal.h>
#include <stdbool.h>

#ifdef _WIN32
#ifndef HAVE_SIGINFO_T
typedef struct {
    int si_signo;
    int si_code;
    int si_errno;
    void *si_addr;
} siginfo_t;
#define HAVE_SIGINFO_T
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VM_REGIONS 64
#define MAX_PATH_LEN   256

/* Snapshot of a virtual memory region from /proc/self/maps */
typedef struct {
    uintptr_t start_addr;
    uintptr_t end_addr;
    char perms[5];          /* e.g., "r-xp", "rw-p" */
    uint64_t offset;
    char path[MAX_PATH_LEN];
} vm_region_t;

/* Process and environment state captured at crash time */
typedef struct {
    uint32_t pid;
    uint32_t crashing_tid;
    int crash_signal;               /* e.g., SIGSEGV */
    int signal_code;                /* si_code */
    uintptr_t fault_addr;           /* si_addr */
    uint64_t crash_timestamp_ns;
    
    /* Virtual memory map snapshot */
    vm_region_t vm_regions[MAX_VM_REGIONS];
    size_t vm_region_count;
    
    /* Open descriptor and thread metrics */
    int open_fd_count;
    int active_thread_count;
} process_state_t;

/* --- Dev 1: Signal Interception & State Collector APIs --- */

/* Installs signal handlers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGPIPE */
int signal_handler_install(ring_buffer_t *rb);

/* Restores original default signal handlers */
void signal_handler_uninstall(void);

/* Captures current process state into out_state */
int state_collector_capture(process_state_t *out_state, int signo, siginfo_t *info);

/* Checks if an address falls within mapped process virtual memory */
bool state_collector_is_addr_mapped(const process_state_t *state, uintptr_t addr);

/* Utility timing and ID helpers */
uint64_t crash_get_timestamp_ns(void);
uint32_t crash_get_current_tid(void);

/* Global ring buffer pointer accessible to hooks */
extern ring_buffer_t *g_active_ring_buffer;

#ifdef __cplusplus
}
#endif

#endif /* CRASH_COLLECTOR_H */
