/*
 * test_memleak.c - Synthetic Memory Leak Workload
 * Project: Crash Archaeologist 2.0
 *
 * Failure Mechanism:
 * Continuously allocates heap buffers in a loop without invoking free(),
 * establishing a monotonic upward net allocation slope.
 *
 * Safe Execution Strategy:
 * Strictly bounded to 20 allocations of 1024 bytes (total 20 KB).
 * This safely guarantees that the developer's system RAM is never exhausted,
 * while generating sufficient sequential allocator records for the
 * Crash Archaeologist memory leak heuristic (alloc_count >= 10, free_count == 0).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_LEAK_BLOCKS 20
#define BLOCK_SIZE 1024

int main(void) {
    printf("[test_memleak] Starting Bounded Memory Leak Workload...\n");
    printf("[test_memleak] Allocating %d blocks of %d bytes without freeing...\n",
           NUM_LEAK_BLOCKS, BLOCK_SIZE);

    void *allocated_blocks[NUM_LEAK_BLOCKS];

    for (int i = 0; i < NUM_LEAK_BLOCKS; i++) {
        allocated_blocks[i] = malloc(BLOCK_SIZE);
        if (!allocated_blocks[i]) {
            perror("malloc failed");
            return 1;
        }
        memset(allocated_blocks[i], 0x42, BLOCK_SIZE);
        printf("[test_memleak] Block %2d allocated at %p\n", i + 1, allocated_blocks[i]);
    }

    printf("[test_memleak] All %d blocks allocated (%d KB net heap growth).\n",
           NUM_LEAK_BLOCKS, (NUM_LEAK_BLOCKS * BLOCK_SIZE) / 1024);
    printf("[test_memleak] Exiting without freeing (intentional leak for recorder inspection).\n");
    return 0;
}
