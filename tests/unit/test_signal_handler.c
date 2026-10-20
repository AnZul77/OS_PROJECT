#include "signals.h"
#include "collector.h"
#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_PASS(name) printf("  [PASS] %s\n", name)

static void test_signal_naming(void) {
    const char *name_segv = crash_signal_name(SIGSEGV);
    assert(strstr(name_segv, "SIGSEGV") != NULL);
    
    const char *name_abrt = crash_signal_name(SIGABRT);
    assert(strstr(name_abrt, "SIGABRT") != NULL);
    
    const char *name_fpe = crash_signal_name(SIGFPE);
    assert(strstr(name_fpe, "SIGFPE") != NULL);
    
    const char *name_ill = crash_signal_name(SIGILL);
    assert(strstr(name_ill, "SIGILL") != NULL);
    
    TEST_PASS("test_signal_naming");
}

static void test_state_collector_snapshot(void) {
    process_state_t state;
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    si.si_signo = SIGSEGV;
    si.si_code = 1; /* SEGV_MAPERR */
    si.si_addr = (void *)0xDEADBEEF;
    
    int ret = state_collector_capture(&state, SIGSEGV, &si);
    assert(ret == 0);
    assert(state.crash_signal == SIGSEGV);
    assert(state.fault_addr == (uintptr_t)0xDEADBEEF);
    assert(state.pid > 0);
    assert(state.crashing_tid > 0);
    assert(state.crash_timestamp_ns > 0);
    
    /* Address 0xDEADBEEF should NOT be mapped */
    bool mapped = state_collector_is_addr_mapped(&state, (uintptr_t)0xDEADBEEF);
    assert(!mapped);
    
    /* Code segment / function pointer address should be mapped if VM regions were collected */
    if (state.vm_region_count > 0) {
        uintptr_t func_addr = (uintptr_t)&test_state_collector_snapshot;
        assert(state_collector_is_addr_mapped(&state, func_addr));
    }
    
    TEST_PASS("test_state_collector_snapshot");
}

static void test_handler_install_uninstall(void) {
    ring_buffer_t rb;
    ring_buffer_init(&rb, 64);
    
    int ret = signal_handler_install(&rb);
    assert(ret == 0);
    assert(g_active_ring_buffer == &rb);
    
    signal_handler_uninstall();
    assert(g_active_ring_buffer == NULL);
    
    ring_buffer_destroy(&rb);
    TEST_PASS("test_handler_install_uninstall");
}

int main(void) {
    printf("====================================================\n");
    printf("  CRASH ARCHAEOLOGIST: Dev 1 Signal & State Tests\n");
    printf("====================================================\n");
    
    test_signal_naming();
    test_state_collector_snapshot();
    test_handler_install_uninstall();
    
    printf("====================================================\n");
    printf("  ALL DEV 1 SIGNAL & STATE TESTS PASSED (3/3)\n");
    printf("====================================================\n");
    return 0;
}
