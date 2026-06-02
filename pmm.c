/* 
 * ============================================================================
 * PHYSICAL MEMORY MANAGER (Frame allocator & buddy system)
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * FRAME ALLOCATOR (BITMAP-BASED)
 * ============================================================================ */

#define FRAMES_PER_BITMAP_ENTRY 64  // 64-bit entry
#define FRAME_SIZE 4096

typedef struct {
    uint64_t* bitmap;
    uint64_t total_frames;
    uint64_t free_frames;
    uint64_t base_address;
} pmm_t;

static pmm_t pmm_state;

/* ============================================================================
 * PHYSICAL MEMORY MANAGER IMPLEMENTATION
 * ============================================================================ */

void pmm_initialize(uint64_t total_memory) {
    pmm_state.total_frames = (total_memory + FRAME_SIZE - 1) / FRAME_SIZE;
    pmm_state.free_frames = 0;
    pmm_state.base_address = 0;

    /* Allocate bitmap (1 bit per frame) */
    uint64_t bitmap_size = (pmm_state.total_frames + 63) / 64;

    // In real kernel, we'd use static allocation or early heap
    static uint64_t bitmap_storage[131072];  // Support up to 32GB

    if (bitmap_size > 131072) {
        KERROR("PMM: Total memory too large for bitmap");
        bitmap_size = 131072;
        pmm_state.total_frames = bitmap_size * 64;
    }

    pmm_state.bitmap = bitmap_storage;

    /* Mark all frames as reserved by default */
    for (uint64_t i = 0; i < bitmap_size; i++) {
        pmm_state.bitmap[i] = 0ULL;
    }

    KLOG("PMM initialized for %lu bytes (%lu frames)",
         total_memory, pmm_state.total_frames);
}

void pmm_add_available_range(uint64_t address, uint64_t length) {
    if (length == 0) {
        return;
    }

    uint64_t start_frame = address / FRAME_SIZE;
    uint64_t end_frame = (address + length + FRAME_SIZE - 1) / FRAME_SIZE;
    uint64_t bitmap_end = (pmm_state.total_frames + 63) / 64;

    for (uint64_t frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
        uint64_t bitmap_idx = frame / 64;
        uint64_t bit_idx = frame % 64;
        if (bitmap_idx >= bitmap_end) {
            break;
        }
        if (!(pmm_state.bitmap[bitmap_idx] & (1ULL << bit_idx))) {
            pmm_state.bitmap[bitmap_idx] |= (1ULL << bit_idx);
            pmm_state.free_frames++;
        }
    }
}

void pmm_reserve_range(uint64_t address, uint64_t length) {
    if (length == 0) {
        return;
    }

    uint64_t start_frame = address / FRAME_SIZE;
    uint64_t end_frame = (address + length + FRAME_SIZE - 1) / FRAME_SIZE;
    uint64_t bitmap_end = (pmm_state.total_frames + 63) / 64;

    for (uint64_t frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
        uint64_t bitmap_idx = frame / 64;
        uint64_t bit_idx = frame % 64;
        if (bitmap_idx >= bitmap_end) {
            break;
        }
        if (pmm_state.bitmap[bitmap_idx] & (1ULL << bit_idx)) {
            pmm_state.bitmap[bitmap_idx] &= ~(1ULL << bit_idx);
            pmm_state.free_frames--;
        }
    }
}

uint64_t pmm_allocate_frame(void) {
    /* Find first free frame in bitmap */
    for (uint64_t bitmap_idx = 0; bitmap_idx < (pmm_state.total_frames + 63) / 64; bitmap_idx++) {
        if (pmm_state.bitmap[bitmap_idx] != 0) {
            // Found a bitmap entry with free frames
            for (uint64_t bit_idx = 0; bit_idx < 64; bit_idx++) {
                if (pmm_state.bitmap[bitmap_idx] & (1UL << bit_idx)) {
                    // Found a free frame
                    pmm_state.bitmap[bitmap_idx] &= ~(1UL << bit_idx);
                    pmm_state.free_frames--;
                    
                    uint64_t frame_num = bitmap_idx * 64 + bit_idx;
                    uint64_t paddr = frame_num * FRAME_SIZE;
                    
                    return paddr;
                }
            }
        }
    }
    
    KERROR("PMM: Out of memory!");
    return 0;
}

void pmm_free_frame(uint64_t paddr) {
    uint64_t frame_num = paddr / FRAME_SIZE;
    uint64_t bitmap_idx = frame_num / 64;
    uint64_t bit_idx = frame_num % 64;
    
    if (bitmap_idx >= (pmm_state.total_frames + 63) / 64) {
        KERROR("PMM: Freeing invalid frame address 0x%lx", paddr);
        return;
    }
    
    pmm_state.bitmap[bitmap_idx] |= (1UL << bit_idx);
    pmm_state.free_frames++;
}

uint64_t pmm_get_available(void) {
    return pmm_state.free_frames * FRAME_SIZE;
}
