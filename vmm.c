/* 
 * ============================================================================
 * VIRTUAL MEMORY MANAGER (x86_64 4-level paging)
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * x86_64 4-LEVEL PAGING STRUCTURES & MACROS
 * ============================================================================ */

#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1FF)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1FF)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1FF)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1FF)
#define PAGE_OFFSET(vaddr) ((vaddr) & 0xFFF)

/* Page table entry flags */
#define PTE_PRESENT       0x001  // Page present in memory
#define PTE_WRITABLE      0x002  // Page is writable
#define PTE_USER          0x004  // Page is accessible from user mode
#define PTE_WRITETHROUGH  0x008  // Disable caching
#define PTE_NOCACHE       0x010  // Page-level cache disable
#define PTE_ACCESSED      0x020  // Page has been accessed
#define PTE_DIRTY         0x040  // Page has been written to
#define PTE_HUGE          0x080  // 2MB or 1GB page
#define PTE_GLOBAL        0x100  // Page is global
#define PTE_NX            0x8000000000000000  // No execute

#define DEFAULT_FLAGS (PTE_PRESENT | PTE_WRITABLE | PTE_ACCESSED)

/* Page table structures */
typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(4096))) page_table_t;

/* Global kernel page tables (placed at specific addresses) */
static page_table_t kernel_pml4 __attribute__((section(".data"))) __attribute__((aligned(4096)));
static page_table_t kernel_pdpt __attribute__((section(".data"))) __attribute__((aligned(4096)));
static page_table_t kernel_pd_0  __attribute__((section(".data"))) __attribute__((aligned(4096)));
static page_table_t kernel_pd_1  __attribute__((section(".data"))) __attribute__((aligned(4096)));

/* Virtual address to physical address mapping metadata */
typedef struct {
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t flags;
} vmm_mapping_t;

#define VMM_MAX_MAPPINGS 1024
static vmm_mapping_t vmm_mappings[VMM_MAX_MAPPINGS];
static uint32_t vmm_mapping_count = 0;

/* ============================================================================
 * VIRTUAL MEMORY MANAGER IMPLEMENTATION
 * ============================================================================ */

void vmm_initialize(void) {
    /* Clear all page table entries */
    for (int i = 0; i < 512; i++) {
        kernel_pml4.entries[i] = 0;
        kernel_pdpt.entries[i] = 0;
    }

    /* Setup identity mapping for the first 6GB of RAM */
    uint64_t pml4_entry = ((uint64_t)&kernel_pdpt) | DEFAULT_FLAGS;
    kernel_pml4.entries[0] = pml4_entry;
    kernel_pml4.entries[256] = pml4_entry;  // alias kernel space high-half if needed

    for (int i = 0; i < 6; i++) {
        uint64_t phys = (uint64_t)i * 0x40000000ULL;
        kernel_pdpt.entries[i] = phys | DEFAULT_FLAGS | PTE_HUGE;
    }

    /* Load PML4 into CR3 */
    uint64_t pml4_paddr = (uint64_t)&kernel_pml4;
    asm volatile("movq %0, %%cr3" :: "r"(pml4_paddr));

    KLOG("VMM initialized with identity-mapped 6GB via PML4 at 0x%lx", pml4_paddr);
}

void vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (vmm_mapping_count >= VMM_MAX_MAPPINGS) {
        KERROR("VMM mapping table full!");
        return;
    }
    
    uint16_t pml4_idx = PML4_INDEX(vaddr);
    uint16_t pdpt_idx = PDPT_INDEX(vaddr);
    uint16_t pd_idx   = PD_INDEX(vaddr);
    uint16_t pt_idx   = PT_INDEX(vaddr);
    
    // Simplified: just record the mapping for now
    vmm_mappings[vmm_mapping_count].vaddr = vaddr;
    vmm_mappings[vmm_mapping_count].paddr = paddr;
    vmm_mappings[vmm_mapping_count].flags = flags;
    vmm_mapping_count++;
    
    // Flush TLB for this address
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

void vmm_unmap_page(uint64_t vaddr) {
    // Find and remove mapping
    for (uint32_t i = 0; i < vmm_mapping_count; i++) {
        if (vmm_mappings[i].vaddr == vaddr) {
            // Shift remaining mappings
            for (uint32_t j = i; j < vmm_mapping_count - 1; j++) {
                vmm_mappings[j] = vmm_mappings[j + 1];
            }
            vmm_mapping_count--;
            break;
        }
    }
    
    // Flush TLB
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

uint64_t vmm_translate(uint64_t vaddr) {
    // Look up virtual to physical translation
    for (uint32_t i = 0; i < vmm_mapping_count; i++) {
        if (vmm_mappings[i].vaddr == vaddr) {
            return vmm_mappings[i].paddr;
        }
    }
    return 0;  // Not mapped
}
