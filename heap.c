/* 
 * ============================================================================
 * KERNEL HEAP ALLOCATOR (SLAB + BUDDY HYBRID)
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * SLAB ALLOCATOR STRUCTURES
 * ============================================================================ */

typedef struct slab_object {
    uintptr_t address;
    size_t size;
    bool used;
    const char* owner;
    struct slab_object* next;
} slab_object_t;

#define SLAB_CACHE_16   16
#define SLAB_CACHE_32   32
#define SLAB_CACHE_64   64
#define SLAB_CACHE_128  128
#define SLAB_CACHE_256  256

/* Slab caches */
#define SLAB_OBJECTS_PER_CACHE 32

static struct {
    slab_object_t objects[SLAB_OBJECTS_PER_CACHE];
    uint32_t free_count;
} slab_caches[5];

/* Buddy allocator for larger blocks */
typedef struct buddy_block {
    uintptr_t address;
    size_t size;
    bool used;
    const char* owner;
    struct buddy_block* next;
} buddy_block_t;

#define BUDDY_BLOCKS_MAX 64
static buddy_block_t buddy_blocks[BUDDY_BLOCKS_MAX];
static uint32_t buddy_block_count = 0;

/* ============================================================================
 * SLAB CACHE INITIALIZATION
 * ============================================================================ */

static void init_slab_caches(void) {
    for (int cache_idx = 0; cache_idx < 5; cache_idx++) {
        slab_caches[cache_idx].free_count = SLAB_OBJECTS_PER_CACHE;
        for (int i = 0; i < SLAB_OBJECTS_PER_CACHE; i++) {
            slab_caches[cache_idx].objects[i].address = 0x100000 + (cache_idx * 0x10000) + (i * 512);
            slab_caches[cache_idx].objects[i].size = 0;
            slab_caches[cache_idx].objects[i].used = false;
            slab_caches[cache_idx].objects[i].owner = "Free";
            slab_caches[cache_idx].objects[i].next = NULL;
        }
    }
}

static void init_buddy_blocks(void) {
    for (int i = 0; i < BUDDY_BLOCKS_MAX; i++) {
        buddy_blocks[i].address = 0x200000 + (i * 0x100000);
        buddy_blocks[i].size = 0x100000;  // 1MB blocks
        buddy_blocks[i].used = false;
        buddy_blocks[i].owner = "Free";
        buddy_blocks[i].next = NULL;
    }
    buddy_block_count = BUDDY_BLOCKS_MAX;
}

/* ============================================================================
 * HEAP ALLOCATOR IMPLEMENTATION
 * ============================================================================ */

void heap_initialize(void) {
    init_slab_caches();
    init_buddy_blocks();
    KLOG("Kernel heap initialized");
}

void* kmalloc(size_t size, const char* owner_name) {
    if (size == 0) return NULL;
    
    /* Route to appropriate slab cache */
    int cache_idx = -1;
    if (size <= SLAB_CACHE_16) {
        cache_idx = 0;
    } else if (size <= SLAB_CACHE_32) {
        cache_idx = 1;
    } else if (size <= SLAB_CACHE_64) {
        cache_idx = 2;
    } else if (size <= SLAB_CACHE_128) {
        cache_idx = 3;
    } else if (size <= SLAB_CACHE_256) {
        cache_idx = 4;
    }
    
    /* Try slab cache allocation */
    if (cache_idx >= 0 && cache_idx < 5) {
        for (int i = 0; i < SLAB_OBJECTS_PER_CACHE; i++) {
            if (!slab_caches[cache_idx].objects[i].used) {
                slab_caches[cache_idx].objects[i].used = true;
                slab_caches[cache_idx].objects[i].size = size;
                slab_caches[cache_idx].objects[i].owner = owner_name;
                slab_caches[cache_idx].free_count--;
                return (void*)slab_caches[cache_idx].objects[i].address;
            }
        }
    }
    
    /* Fallback to buddy allocator */
    for (uint32_t i = 0; i < buddy_block_count; i++) {
        if (!buddy_blocks[i].used && buddy_blocks[i].size >= size) {
            buddy_blocks[i].used = true;
            buddy_blocks[i].size = size;
            buddy_blocks[i].owner = owner_name;
            return (void*)buddy_blocks[i].address;
        }
    }
    
    KERROR("kmalloc: Out of memory for allocation of %zu bytes", size);
    return NULL;
}

void kfree(void* address) {
    if (address == NULL) return;
    
    uintptr_t addr = (uintptr_t)address;
    
    /* Check slab caches */
    for (int cache_idx = 0; cache_idx < 5; cache_idx++) {
        for (int i = 0; i < SLAB_OBJECTS_PER_CACHE; i++) {
            if (slab_caches[cache_idx].objects[i].address == addr && 
                slab_caches[cache_idx].objects[i].used) {
                slab_caches[cache_idx].objects[i].used = false;
                slab_caches[cache_idx].objects[i].owner = "Free";
                slab_caches[cache_idx].objects[i].size = 0;
                slab_caches[cache_idx].free_count++;
                return;
            }
        }
    }
    
    /* Check buddy blocks */
    for (uint32_t i = 0; i < buddy_block_count; i++) {
        if (buddy_blocks[i].address == addr && buddy_blocks[i].used) {
            buddy_blocks[i].used = false;
            buddy_blocks[i].owner = "Free";
            return;
        }
    }
    
    KERROR("kfree: Invalid pointer 0x%lx", addr);
}

void heap_coalesce(void) {
    /* Buddy coalescing: merge adjacent free blocks of same size */
    for (uint32_t i = 0; i < buddy_block_count - 1; i++) {
        if (!buddy_blocks[i].used && !buddy_blocks[i + 1].used &&
            buddy_blocks[i].size == buddy_blocks[i + 1].size &&
            (buddy_blocks[i].address + buddy_blocks[i].size) == buddy_blocks[i + 1].address) {
            
            buddy_blocks[i].size *= 2;
            
            // Mark next block as dead
            buddy_blocks[i + 1].size = 0;
            buddy_blocks[i + 1].owner = "Merged";
        }
    }
}
