#include "collector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#endif

ring_buffer_t *g_active_ring_buffer = NULL;

uint64_t crash_get_timestamp_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}

uint32_t crash_get_current_tid(void) {
#ifdef _WIN32
    return (uint32_t)GetCurrentThreadId();
#elif defined(SYS_gettid)
    return (uint32_t)syscall(SYS_gettid);
#else
    return (uint32_t)(uintptr_t)pthread_self();
#endif
}

static void parse_proc_maps(process_state_t *out_state) {
    out_state->vm_region_count = 0;
#ifndef _WIN32
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), fp) && out_state->vm_region_count < MAX_VM_REGIONS) {
        vm_region_t *region = &out_state->vm_regions[out_state->vm_region_count];
        memset(region, 0, sizeof(vm_region_t));
        
        unsigned long long start = 0, end = 0, offset = 0;
        char perms[16] = {0};
        char path[MAX_PATH_LEN] = {0};
        
        int matched = sscanf(line, "%llx-%llx %15s %llx %*s %*s %255[^\n]",
                             &start, &end, perms, &offset, path);
        
        if (matched >= 3) {
            region->start_addr = (uintptr_t)start;
            region->end_addr = (uintptr_t)end;
            strncpy(region->perms, perms, sizeof(region->perms) - 1);
            region->offset = (uint64_t)offset;
            if (matched >= 5) {
                strncpy(region->path, path, sizeof(region->path) - 1);
            } else {
                strcpy(region->path, "[anon]");
            }
            out_state->vm_region_count++;
        }
    }
    fclose(fp);
#else
    /* Windows fallback using VirtualQuery */
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (out_state->vm_region_count < MAX_VM_REGIONS &&
           VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT) {
            vm_region_t *region = &out_state->vm_regions[out_state->vm_region_count];
            region->start_addr = (uintptr_t)mbi.BaseAddress;
            region->end_addr = region->start_addr + mbi.RegionSize;
            
            if (mbi.Protect & PAGE_EXECUTE_READWRITE) strcpy(region->perms, "rwxp");
            else if (mbi.Protect & PAGE_EXECUTE_READ) strcpy(region->perms, "r-xp");
            else if (mbi.Protect & PAGE_READWRITE)    strcpy(region->perms, "rw-p");
            else if (mbi.Protect & PAGE_READONLY)     strcpy(region->perms, "r--p");
            else                                      strcpy(region->perms, "---p");
            
            strcpy(region->path, "[committed]");
            out_state->vm_region_count++;
        }
        uintptr_t next_addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next_addr <= addr) break;
        addr = next_addr;
    }
#endif
}

static int count_open_fds(void) {
#ifndef _WIN32
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        return 3; /* Fallback: stdin, stdout, stderr */
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
#else
    return 3;
#endif
}

int state_collector_capture(process_state_t *out_state, int signo, siginfo_t *info) {
    if (!out_state) {
        return -1;
    }
    
    memset(out_state, 0, sizeof(process_state_t));
    
#ifdef _WIN32
    out_state->pid = (uint32_t)_getpid();
#else
    out_state->pid = (uint32_t)getpid();
#endif
    out_state->crashing_tid = crash_get_current_tid();
    out_state->crash_signal = signo;
    out_state->crash_timestamp_ns = crash_get_timestamp_ns();
    
    if (info) {
        out_state->signal_code = info->si_code;
        out_state->fault_addr = (uintptr_t)info->si_addr;
    }
    
    out_state->open_fd_count = count_open_fds();
    out_state->active_thread_count = 1;
    
    parse_proc_maps(out_state);
    
    return 0;
}

bool state_collector_is_addr_mapped(const process_state_t *state, uintptr_t addr) {
    if (!state) {
        return false;
    }
    
    for (size_t i = 0; i < state->vm_region_count; i++) {
        if (addr >= state->vm_regions[i].start_addr &&
            addr < state->vm_regions[i].end_addr) {
            return true;
        }
    }
    return false;
}
