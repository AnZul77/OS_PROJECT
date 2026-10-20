#include "signals.h"
#include "collector.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static PVOID g_veh_handle = NULL;
#else
#include <unistd.h>
#include <sys/mman.h>
static stack_t g_alt_stack;
#endif

static process_state_t g_last_crash_state;
static bool g_has_crashed = false;

const char* crash_signal_name(int signo) {
    switch (signo) {
        case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
        case SIGABRT: return "SIGABRT (Aborted)";
        case SIGFPE:  return "SIGFPE (Arithmetic / Floating-Point Exception)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
#ifdef SIGBUS
        case SIGBUS:  return "SIGBUS (Bus Error)";
#endif
#ifdef SIGPIPE
        case SIGPIPE: return "SIGPIPE (Broken Pipe)";
#endif
        default:      return "UNKNOWN_SIGNAL";
    }
}

const char* crash_signal_code_name(int signo, int code) {
    (void)code;
    if (signo == SIGSEGV) {
#ifdef SEGV_MAPERR
        if (code == SEGV_MAPERR) return "SEGV_MAPERR (Address not mapped to object)";
#endif
#ifdef SEGV_ACCERR
        if (code == SEGV_ACCERR) return "SEGV_ACCERR (Invalid permissions for mapped object)";
#endif
        return "SEGV_UNKNOWN_CODE";
    }
    if (signo == SIGFPE) {
#ifdef FPE_INTDIV
        if (code == FPE_INTDIV) return "FPE_INTDIV (Integer divide-by-zero)";
#endif
#ifdef FPE_INTOVF
        if (code == FPE_INTOVF) return "FPE_INTOVF (Integer overflow)";
#endif
        return "FPE_UNKNOWN_CODE";
    }
    return "CODE_UNSPECIFIED";
}

/* Master signal dispatcher */
static void handle_crash_event(int signo, siginfo_t *info) {
    if (g_has_crashed) {
        /* Prevent recursive crash entry */
        return;
    }
    g_has_crashed = true;
    
    /* Step 1: Immediately freeze the flight recorder */
    if (g_active_ring_buffer) {
        ring_buffer_freeze(g_active_ring_buffer);
    }
    
    /* Step 2: Collect process environment snapshot */
    state_collector_capture(&g_last_crash_state, signo, info);
    
    /* Step 3: Record terminal crash event into buffer if not completely full */
    if (g_active_ring_buffer) {
        crash_event_t crash_ev;
        memset(&crash_ev, 0, sizeof(crash_ev));
        crash_ev.category = EVENT_CAT_SIGNAL;
        crash_ev.type = EVENT_SIG_RECEIVED;
        crash_ev.pid = g_last_crash_state.pid;
        crash_ev.tid = g_last_crash_state.crashing_tid;
        crash_ev.timestamp_ns = g_last_crash_state.crash_timestamp_ns;
        crash_ev.addr = g_last_crash_state.fault_addr;
        crash_ev.fd_or_status = signo;
        strncpy(crash_ev.label, crash_signal_name(signo), sizeof(crash_ev.label) - 1);
        
        /* Force record crash marker */
        ring_buffer_record(g_active_ring_buffer, &crash_ev);
    }
    
    /* Async-safe diagnostic announcement */
    fprintf(stderr, "\n[CRASH ARCHAEOLOGIST] Trapped fatal signal: %s (%d)\n",
            crash_signal_name(signo), signo);
    fprintf(stderr, "[CRASH ARCHAEOLOGIST] Fault Address: 0x%llx | TID: %u\n",
            (unsigned long long)g_last_crash_state.fault_addr,
            g_last_crash_state.crashing_tid);
    fprintf(stderr, "[CRASH ARCHAEOLOGIST] Flight buffer frozen with %zu events preserved.\n",
            ring_buffer_get_count(g_active_ring_buffer));
}

#ifdef _WIN32
static LONG WINAPI windows_vectored_exception_handler(PEXCEPTION_POINTERS ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    int signo = 0;
    
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        signo = SIGSEGV;
    } else if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
        signo = SIGILL;
    } else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        signo = SIGFPE;
    } else {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    si.si_signo = signo;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        si.si_addr = (void *)ep->ExceptionRecord->ExceptionInformation[1];
        si.si_code = (int)ep->ExceptionRecord->ExceptionInformation[0];
    }
    
    handle_crash_event(signo, &si);
    
    /* Allow process to terminate with diagnosis printed */
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void posix_sigaction_handler(int signo, siginfo_t *info, void *ucontext) {
    (void)ucontext;
    handle_crash_event(signo, info);
    
    /* Reset signal and re-raise so kernel crash semantics remain intact */
    signal(signo, SIG_DFL);
    raise(signo);
}
#endif

int signal_handler_install(ring_buffer_t *rb) {
    g_active_ring_buffer = rb;
    g_has_crashed = false;
    
#ifdef _WIN32
    g_veh_handle = AddVectoredExceptionHandler(1, windows_vectored_exception_handler);
    return (g_veh_handle != NULL) ? 0 : -1;
#else
    /* Setup dedicated alternate stack for signal handling (e.g. stack overflow) */
    g_alt_stack.ss_sp = malloc(SIGSTKSZ);
    if (g_alt_stack.ss_sp) {
        g_alt_stack.ss_size = SIGSTKSZ;
        g_alt_stack.ss_flags = 0;
        sigaltstack(&g_alt_stack, NULL);
    }
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posix_sigaction_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS,  &sa, NULL);
#endif
#ifdef SIGPIPE
    sigaction(SIGPIPE, &sa, NULL);
#endif
    return 0;
#endif
}

void signal_handler_uninstall(void) {
#ifdef _WIN32
    if (g_veh_handle) {
        RemoveVectoredExceptionHandler(g_veh_handle);
        g_veh_handle = NULL;
    }
#else
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE,  SIG_DFL);
    signal(SIGILL,  SIG_DFL);
#ifdef SIGBUS
    signal(SIGBUS,  SIG_DFL);
#endif
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_DFL);
#endif
    if (g_alt_stack.ss_sp) {
        free(g_alt_stack.ss_sp);
        g_alt_stack.ss_sp = NULL;
    }
#endif
    g_active_ring_buffer = NULL;
}
