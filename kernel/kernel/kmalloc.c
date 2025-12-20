#include <kernel/kmalloc.h>
#include <kernel/tty.h>
#include <stdio.h>
#include <string.h>

// Bitmap allocator implementation
// Each bit represents one HEAP_BLOCK_SIZE block
static uint32_t heap_bitmap[HEAP_BLOCKS / 32];
static bool heap_initialized = false;
static heap_stats_t heap_stats;

// Allocation header stored before each allocated block
typedef struct {
    size_t size;        // Size of allocation in blocks
    uint32_t magic;     // Magic number for validation
} alloc_header_t;

#define ALLOC_MAGIC 0xDEADBEEF
#define HEADER_BLOCKS ((sizeof(alloc_header_t) + HEAP_BLOCK_SIZE - 1) / HEAP_BLOCK_SIZE)

// Helper functions
static inline void bitmap_set(uint32_t bit) {
    heap_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_clear(uint32_t bit) {
    heap_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline bool bitmap_test(uint32_t bit) {
    return (heap_bitmap[bit / 32] & (1 << (bit % 32))) != 0;
}

// Find a contiguous run of free blocks
static int find_free_blocks(size_t num_blocks) {
    size_t count = 0;
    int start = -1;
    
    for (size_t i = 0; i < HEAP_BLOCKS; i++) {
        if (!bitmap_test(i)) {
            if (count == 0) {
                start = i;
            }
            count++;
            if (count >= num_blocks) {
                return start;
            }
        } else {
            count = 0;
            start = -1;
        }
    }
    
    return -1; // Not found
}

// Mark blocks as used
static void mark_blocks_used(int start, size_t num_blocks) {
    for (size_t i = 0; i < num_blocks; i++) {
        bitmap_set(start + i);
    }
}

// Mark blocks as free
static void mark_blocks_free(int start, size_t num_blocks) {
    for (size_t i = 0; i < num_blocks; i++) {
        bitmap_clear(start + i);
    }
}

// Get largest contiguous free block
static size_t get_largest_free_block(void) {
    size_t max_count = 0;
    size_t count = 0;
    
    for (size_t i = 0; i < HEAP_BLOCKS; i++) {
        if (!bitmap_test(i)) {
            count++;
            if (count > max_count) {
                max_count = count;
            }
        } else {
            count = 0;
        }
    }
    
    return max_count * HEAP_BLOCK_SIZE;
}

// Initialize the kernel heap
void kmalloc_init(void) {
    if (heap_initialized) {
        return;
    }
    
    // Clear the bitmap
    memset(heap_bitmap, 0, sizeof(heap_bitmap));
    
    // Initialize statistics
    heap_stats.total_size = HEAP_SIZE;
    heap_stats.used_size = 0;
    heap_stats.free_size = HEAP_SIZE;
    heap_stats.num_allocations = 0;
    heap_stats.num_frees = 0;
    heap_stats.largest_free_block = HEAP_SIZE;
    
    heap_initialized = true;
    
    printf("Kernel heap initialized: start=0x%x, size=%d MB\n", 
           HEAP_START, HEAP_SIZE / (1024 * 1024));
}

// Allocate memory from kernel heap
void* kmalloc(size_t size) {
    if (!heap_initialized || size == 0) {
        return NULL;
    }
    
    // Calculate total blocks needed (including header)
    size_t total_blocks = HEADER_BLOCKS + ((size + HEAP_BLOCK_SIZE - 1) / HEAP_BLOCK_SIZE);
    
    // Find free blocks
    int start_block = find_free_blocks(total_blocks);
    if (start_block < 0) {
        printf("kmalloc: Out of memory (requested %d bytes, %d blocks)\n", size, total_blocks);
        return NULL;
    }
    
    // Mark blocks as used
    mark_blocks_used(start_block, total_blocks);
    
    // Calculate address
    void* ptr = (void*)(HEAP_START + start_block * HEAP_BLOCK_SIZE);
    
    // Write header
    alloc_header_t* header = (alloc_header_t*)ptr;
    header->size = total_blocks;
    header->magic = ALLOC_MAGIC;
    
    // Update statistics
    heap_stats.used_size += total_blocks * HEAP_BLOCK_SIZE;
    heap_stats.free_size -= total_blocks * HEAP_BLOCK_SIZE;
    heap_stats.num_allocations++;
    
    // Return pointer after header
    return (void*)((uint8_t*)ptr + sizeof(alloc_header_t));
}

// Allocate aligned memory
void* kmalloc_a(size_t size, uint32_t align) {
    if (!heap_initialized || size == 0 || align == 0) {
        return NULL;
    }
    
    // For simplicity, allocate extra space and adjust pointer
    void* ptr = kmalloc(size + align);
    if (!ptr) {
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);
    
    return (void*)aligned;
}

// Allocate zeroed memory
void* kcalloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = kmalloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

// Free memory back to kernel heap
void kfree(void* ptr) {
    if (!heap_initialized || !ptr) {
        return;
    }
    
    // Get header
    alloc_header_t* header = (alloc_header_t*)((uint8_t*)ptr - sizeof(alloc_header_t));
    
    // Validate magic number
    if (header->magic != ALLOC_MAGIC) {
        printf("kfree: Invalid pointer or corrupted header (magic=0x%x)\n", header->magic);
        return;
    }
    
    // Calculate block number
    uintptr_t addr = (uintptr_t)header;
    if (addr < HEAP_START || addr >= HEAP_START + HEAP_SIZE) {
        printf("kfree: Pointer out of heap bounds (addr=0x%x)\n", addr);
        return;
    }
    
    int start_block = (addr - HEAP_START) / HEAP_BLOCK_SIZE;
    size_t num_blocks = header->size;
    
    // Mark blocks as free
    mark_blocks_free(start_block, num_blocks);
    
    // Update statistics
    heap_stats.used_size -= num_blocks * HEAP_BLOCK_SIZE;
    heap_stats.free_size += num_blocks * HEAP_BLOCK_SIZE;
    heap_stats.num_frees++;
    
    // Clear magic to detect double-free
    header->magic = 0;
}

// Reallocate memory (resize allocation)
void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    // Get old header
    alloc_header_t* header = (alloc_header_t*)((uint8_t*)ptr - sizeof(alloc_header_t));
    
    // Validate magic number
    if (header->magic != ALLOC_MAGIC) {
        return NULL;
    }
    
    // Calculate old size (excluding header)
    size_t old_size = (header->size - HEADER_BLOCKS) * HEAP_BLOCK_SIZE;
    
    // Allocate new memory
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy data
    size_t copy_size = (new_size < old_size) ? new_size : old_size;
    memcpy(new_ptr, ptr, copy_size);
    
    // Free old memory
    kfree(ptr);
    
    return new_ptr;
}

// Get heap statistics
void kmalloc_get_stats(heap_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    *stats = heap_stats;
    stats->largest_free_block = get_largest_free_block();
}

// Print heap statistics
void kmalloc_print_stats(void) {
    heap_stats_t stats;
    kmalloc_get_stats(&stats);
    
    printf("=== Kernel Heap Statistics ===\n");
    printf("Total size:          %d KB (%d MB)\n", 
           stats.total_size / 1024, stats.total_size / (1024 * 1024));
    printf("Used:                %d KB\n", stats.used_size / 1024);
    printf("Free:                %d KB\n", stats.free_size / 1024);
    printf("Allocations:         %d\n", stats.num_allocations);
    printf("Frees:               %d\n", stats.num_frees);
    printf("Largest free block:  %d KB\n", stats.largest_free_block / 1024);
    printf("Fragmentation:       %.1f%%\n", 
           (stats.free_size > 0) ? 
           (100.0 * (1.0 - (float)stats.largest_free_block / stats.free_size)) : 0.0);
}

// Check if heap is initialized
bool kmalloc_is_initialized(void) {
    return heap_initialized;
}
