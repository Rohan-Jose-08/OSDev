#ifndef _KERNEL_KMALLOC_H
#define _KERNEL_KMALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/memory.h>

// Heap configuration
#define HEAP_PHYS_START 0x00400000  // Physical start of heap (4MB)
#define HEAP_START (KERNEL_VIRT_BASE + HEAP_PHYS_START)
#define HEAP_SIZE  0x01000000  // Heap size (16MB)
#define HEAP_BLOCK_SIZE 64     // Minimum allocation unit (64 bytes)
#define HEAP_BLOCKS (HEAP_SIZE / HEAP_BLOCK_SIZE)

// Heap statistics structure
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    size_t num_allocations;
    size_t num_frees;
    size_t largest_free_block;
} heap_stats_t;

// Initialize the kernel heap
void kmalloc_init(void);

// Allocate memory from kernel heap
void* kmalloc(size_t size);

// Allocate aligned memory from kernel heap
void* kmalloc_a(size_t size, uint32_t align);

// Allocate zeroed memory from kernel heap
void* kcalloc(size_t num, size_t size);

// Free memory back to kernel heap
void kfree(void* ptr);

// Reallocate memory (resize allocation)
void* krealloc(void* ptr, size_t new_size);

// Get heap statistics
void kmalloc_get_stats(heap_stats_t* stats);

// Print heap statistics (for debugging)
void kmalloc_print_stats(void);

// Check if heap is initialized
bool kmalloc_is_initialized(void);

#endif /* _KERNEL_KMALLOC_H */
