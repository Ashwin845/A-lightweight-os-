/* 
 * ============================================================================
 * SYSTEM CALL INTERFACE & DISPATCHER
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * SYSCALL DISPATCHER IMPLEMENTATION
 * ============================================================================ */

uint64_t syscall_dispatcher(uint64_t syscall_id, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    /* Security check: verify user-space addresses */
    #define KERNEL_SPACE_START 0xFFFFFFFF80000000UL
    
    if ((arg1 >= KERNEL_SPACE_START) || (arg2 >= KERNEL_SPACE_START) || (arg3 >= KERNEL_SPACE_START)) {
        KERROR("Syscall: Illegal privilege violation (address in kernel space)");
        return (uint64_t)-1;
    }
    
    switch (syscall_id) {
        case SYS_GET_PID: {
            tcb_t* current = get_current_task();
            if (current) {
                return current->pid;
            }
            return 0;
        }
        
        case SYS_EXIT: {
            uint32_t exit_code = (uint32_t)arg1;
            tcb_t* current = get_current_task();
            if (current) {
                KLOG("Task PID %u exiting with code %u", current->pid, exit_code);
                kill_task(current->pid);
            }
            return 0;
        }
        
        case SYS_SND_BEEP: {
            uint16_t frequency = (uint16_t)arg1;  // Hz
            uint16_t duration = (uint16_t)arg2;   // ms
            
            // PC Speaker beep via ports 0x61 and 0x43
            // Mock implementation - just log
            KLOG("Beep requested: %u Hz for %u ms", frequency, duration);
            return 0;
        }
        
        case SYS_KPRINTF: {
            // arg1 = format string address
            // arg2, arg3 = additional parameters
            const char* fmt = (const char*)arg1;
            if (fmt) {
                kprintf("[User] %s\n", fmt);
            }
            return 0;
        }
        
        case SYS_OPEN_FILE: {
            // arg1 = filename pointer
            // arg2 = flags
            const char* filename = (const char*)arg1;
            uint32_t flags = (uint32_t)arg2;
            
            KLOG("Open file syscall: '%s' (flags 0x%x)", filename, flags);
            return 0;  // Mock: return fake file descriptor
        }
        
        default: {
            KERROR("Unknown syscall ID: %lu", syscall_id);
            return (uint64_t)-2;
        }
    }
}

/* ============================================================================
 * SYSCALL MSR SETUP (for SYSCALL instruction)
 * ============================================================================ */

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_SFMASK 0xC0000084

uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    asm volatile("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

// Forward declare the syscall entry point (to be implemented in assembly)
extern void syscall_entry(void);

void enable_syscall_engine(void) {
    /* Enable SYSCALL/SYSRET (bit 0 of EFER) */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1;  // SCE bit
    wrmsr(MSR_EFER, efer);
    
    /* Set up STAR: kernel and user segment selectors */
    uint64_t star = 0;
    star |= (0x08UL << 32);  // Kernel code segment (bits 32-47)
    star |= (0x18UL << 48);  // User code segment (bits 48-63)
    wrmsr(MSR_STAR, star);
    
    /* Set LSTAR: 64-bit syscall entry point */
    wrmsr(MSR_LSTAR, (uint64_t)&syscall_entry);
    
    /* Set SFMASK: flags to clear on entry */
    wrmsr(MSR_SFMASK, 0x200);  // Clear IF (interrupt flag)
    
    KLOG("SYSCALL/SYSRET engine enabled");
}

uint32_t get_pid(void) {
    tcb_t* current = get_current_task();
    return current ? current->pid : 0;
}

uint32_t get_ppid(void) {
    return 0;
}
