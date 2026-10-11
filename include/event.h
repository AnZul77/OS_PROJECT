#ifndef CRASH_EVENT_H
#define CRASH_EVENT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event classification categories */
typedef enum {
    EVENT_CAT_PROCESS_THREAD = 1,
    EVENT_CAT_SYSCALL        = 2,
    EVENT_CAT_MEMORY         = 3,
    EVENT_CAT_IO             = 4,
    EVENT_CAT_IPC            = 5,
    EVENT_CAT_SIGNAL         = 6
} event_category_t;

/* Specific event operation types */
typedef enum {
    /* Process / Thread events */
    EVENT_THREAD_CREATE = 100,
    EVENT_THREAD_EXIT,
    EVENT_THREAD_SWITCH,
    
    /* Memory events */
    EVENT_MEM_ALLOC = 200,      /* malloc, calloc */
    EVENT_MEM_FREE,             /* free */
    EVENT_MEM_REALLOC,          /* realloc */
    EVENT_MEM_MMAP,             /* mmap */
    EVENT_MEM_MUNMAP,           /* munmap */
    
    /* Syscalls & I/O */
    EVENT_SYS_OPEN = 300,
    EVENT_SYS_CLOSE,
    EVENT_SYS_READ,
    EVENT_SYS_WRITE,
    
    /* IPC & Sync */
    EVENT_IPC_PIPE_WRITE = 400,
    EVENT_IPC_PIPE_READ,
    EVENT_IPC_SOCKET_SEND,
    EVENT_IPC_SOCKET_RECV,
    EVENT_SYNC_MUTEX_LOCK,
    EVENT_SYNC_MUTEX_UNLOCK,
    
    /* Fatal & Lifecycle signals */
    EVENT_SIG_RECEIVED = 500
} event_type_t;

/* Root cause failure classification */
typedef enum {
    CAUSE_UNKNOWN = 0,
    CAUSE_SEGFAULT,
    CAUSE_USE_AFTER_FREE,
    CAUSE_MEMORY_LEAK,
    CAUSE_DATA_RACE,
    CAUSE_DEADLOCK,
    CAUSE_RESOURCE_EXHAUSTION,
    CAUSE_IPC_FAILURE
} root_cause_type_t;

/* Uniform payload for ring buffer events (Fixed size for zero-allocation recording) */
typedef struct {
    uint64_t sequence_id;       /* Monotonically increasing sequence number */
    uint64_t timestamp_ns;      /* Nanoseconds timestamp */
    uint32_t pid;               /* Process ID */
    uint32_t tid;               /* Thread ID */
    
    event_category_t category;  /* Event Category */
    event_type_t type;          /* Event Type */
    
    uintptr_t addr;             /* Memory address, pointer, or buffer reference */
    size_t size;                /* Size allocated, bytes read/written */
    int32_t fd_or_status;       /* File descriptor, signal number, or return code */
    
    char label[32];             /* Brief identifier or symbol description */
} crash_event_t;

#ifdef __cplusplus
}
#endif

#endif /* CRASH_EVENT_H */
