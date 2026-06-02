/* 
 * ============================================================================
 * MAIN KERNEL ENTRY POINT & INITIALIZATION
 * ============================================================================
 */

#include "../include/kernel.h"
#include <stdarg.h>

/* Multiboot info structure (from GRUB bootloader) */
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} multiboot_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} multiboot_tag_mmap_t;

typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} multiboot_mmap_entry_t;

static uint64_t get_multiboot_memory_top(void* multiboot_info) {
    if (!multiboot_info) {
        return 0;
    }

    multiboot_info_t* info = (multiboot_info_t*)multiboot_info;
    uint8_t* tag_ptr = (uint8_t*)multiboot_info + sizeof(multiboot_info_t);
    uint64_t max_addr = 0;

    while ((uint8_t*)tag_ptr < ((uint8_t*)multiboot_info + info->total_size)) {
        multiboot_tag_t* tag = (multiboot_tag_t*)tag_ptr;
        if (tag->type == 0) {
            break;
        }

        if (tag->type == 6) {
            multiboot_tag_mmap_t* mmap_tag = (multiboot_tag_mmap_t*)tag;
            uint8_t* entry_ptr = tag_ptr + sizeof(multiboot_tag_mmap_t);
            uint8_t* tag_end = tag_ptr + tag->size;

            while (entry_ptr < tag_end) {
                multiboot_mmap_entry_t* entry = (multiboot_mmap_entry_t*)entry_ptr;
                uint64_t end_addr = entry->addr + entry->len;
                if (end_addr > max_addr) {
                    max_addr = end_addr;
                }
                entry_ptr += mmap_tag->entry_size;
            }
        }

        uint32_t size = (tag->size + 7) & ~7u;
        tag_ptr += size;
    }

    return max_addr;
}

static bool mark_multiboot_memory(void* multiboot_info) {
    if (!multiboot_info) {
        return false;
    }

    multiboot_info_t* info = (multiboot_info_t*)multiboot_info;
    uint8_t* tag_ptr = (uint8_t*)multiboot_info + sizeof(multiboot_info_t);
    bool found_mmap = false;

    while ((uint8_t*)tag_ptr < ((uint8_t*)multiboot_info + info->total_size)) {
        multiboot_tag_t* tag = (multiboot_tag_t*)tag_ptr;
        if (tag->type == 0) {
            break;
        }

        if (tag->type == 6) {
            found_mmap = true;
            multiboot_tag_mmap_t* mmap_tag = (multiboot_tag_mmap_t*)tag;
            uint8_t* entry_ptr = tag_ptr + sizeof(multiboot_tag_mmap_t);
            uint8_t* tag_end = tag_ptr + tag->size;

            while (entry_ptr < tag_end) {
                multiboot_mmap_entry_t* entry = (multiboot_mmap_entry_t*)entry_ptr;
                if (entry->type == 1) {
                    pmm_add_available_range(entry->addr, entry->len);
                }
                entry_ptr += mmap_tag->entry_size;
            }
        }

        uint32_t size = (tag->size + 7) & ~7u;
        tag_ptr += size;
    }

    return found_mmap;
}

/* Global kernel state */
static bool kernel_initialized = false;
static char kprintf_buffer[512];

static void idle_task_func(void) {
    while (1) {
        asm volatile("hlt");
        task_yield();
    }
}

static size_t kstrlen(const char* str);

static void draw_mouse_status(int x, int y, int mx, int my, bool left, bool right, bool middle) {
    if (x < 0 || x >= 78 || y < 0 || y >= 24) {
        return;
    }

    vga_write_char(x + 0, y, 'M', 0x0F);
    vga_write_char(x + 1, y, 'X', 0x0F);
    vga_write_char(x + 2, y, ':', 0x0F);
    vga_write_char(x + 3, y, '0' + (mx / 10), 0x0F);
    vga_write_char(x + 4, y, '0' + (mx % 10), 0x0F);
    vga_write_char(x + 5, y, ' ', 0x0F);
    vga_write_char(x + 6, y, 'M', 0x0F);
    vga_write_char(x + 7, y, 'Y', 0x0F);
    vga_write_char(x + 8, y, ':', 0x0F);
    vga_write_char(x + 9, y, '0' + (my / 10), 0x0F);
    vga_write_char(x + 10, y, '0' + (my % 10), 0x0F);
    vga_write_char(x + 11, y, ' ', 0x0F);
    vga_write_char(x + 12, y, left ? 'L' : '-', 0x0F);
    vga_write_char(x + 13, y, right ? 'R' : '-', 0x0F);
    vga_write_char(x + 14, y, middle ? 'M' : '-', 0x0F);
}

static void shell_task(void) {
    const char* prompt = "[.C OS]> ";
    int x = 0;
    int y = 22;
    int pos = 0;
    char line[80] = {0};

    for (const char* p = prompt; *p; p++) {
        vga_write_char(x++, y, *p, 0x0F);
    }

    while (1) {
        if (keyboard_has_data()) {
            char c = keyboard_read_char();
            if (c == '\r' || c == '\n') {
                line[pos] = '\0';
                kprintf("Shell command: %s\n", line);
                x = 0;
                y++;
                pos = 0;
                if (y >= 24) {
                    y = 23;
                }
                for (const char* p = prompt; *p; p++) {
                    vga_write_char(x++, y, *p, 0x0F);
                }
            } else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    x--;
                    vga_write_char(x, y, ' ', 0x0F);
                }
            } else if (pos < (int)sizeof(line) - 1) {
                line[pos++] = c;
                vga_write_char(x++, y, c, 0x0F);
            }
        }

        if (mouse_has_event()) {
            int dx = 0;
            int dy = 0;
            bool left = false;
            bool right = false;
            bool middle = false;
            mouse_get_event(&dx, &dy, &left, &right, &middle);
            int mouse_x = mouse_get_x();
            int mouse_y = mouse_get_y();
            draw_mouse_status(0, 23, mouse_x, mouse_y, left, right, middle);
            if (left || right || middle) {
                gui_handle_mouse_click(mouse_x, mouse_y, left, right, middle);
            }
            KLOG("Mouse event dx=%d dy=%d left=%u right=%u middle=%u", dx, dy, left, right, middle);
        }

        network_poll();
        task_yield();
    }
}

static void pit_tick(void) {
    request_schedule();
}

/* ============================================================================
 * KERNEL ENTRY POINT (called from boot.asm)
 * ============================================================================ */

void kernel_main(void* multiboot_info, uint32_t multiboot_magic) {
    /* Initialize serial port for early debugging */
    serial_initialize();
    kprintf("\n=== .C OS Bootloader Stage Complete ===\n");
    kprintf("Multiboot Info: 0x%p, Magic: 0x%x\n", multiboot_info, multiboot_magic);
    
    /* Initialize Global Descriptor Table (GDT) */
    KLOG("Initializing GDT...");
    // GDT is already set by boot.asm, but we can validate it here
    
    /* Initialize Interrupt Descriptor Table (IDT) */
    KLOG("Initializing IDT and interrupts...");
    idt_initialize();
    set_interrupt_handler(32, pit_tick);
    pit_initialize(100);
    
    /* Initialize Physical Memory Manager (PMM) */
    KLOG("Initializing Physical Memory Manager...");
    uint64_t max_memory = get_multiboot_memory_top(multiboot_info);
    if (max_memory == 0) {
        KERROR("Multiboot memory map not found; defaulting to 6GB physical memory layout");
        max_memory = 0x180000000ULL; // Default to 6GB
    }
    pmm_initialize(max_memory);
    if (!mark_multiboot_memory(multiboot_info)) {
        pmm_add_available_range(0x100000, max_memory - 0x100000);
    }
    pmm_reserve_range(0x0, 0x200000);  // Reserve the first 2MB for bootloader/kernel/BIOS use
    uint64_t available = pmm_get_available();
    KLOG("Physical address space configured: %lu MB", max_memory / (1024 * 1024));
    KLOG("Available physical memory: %lu MB", available / (1024 * 1024));
    
    /* Initialize Virtual Memory Manager (VMM) */
    KLOG("Initializing Virtual Memory Manager...");
    vmm_initialize();
    
    /* Initialize kernel heap allocator */
    KLOG("Initializing kernel heap...");
    heap_initialize();
    
    /* Initialize system calls */
    KLOG("Setting up syscall interface...");
    enable_syscall_engine();
    
    /* Initialize process scheduler */
    KLOG("Initializing scheduler...");
    scheduler_initialize();
    
    /* Initialize device drivers */
    KLOG("Initializing VGA display driver...");
    vga_initialize();
    vga_clear_screen();

    KLOG("Initializing GUI layer...");
    gui_initialize();
    gui_set_resolution(80, 24);
    
    KLOG("Initializing notification center...");
    notification_center_initialize();
    notification_post("Welcome", "System started", 0);
    
    KLOG("Initializing control center...");
    control_center_initialize();
    
    KLOG("Initializing clipboard...");
    clipboard_initialize();
    
    KLOG("Initializing file operations...");
    file_ops_initialize();
    
    KLOG("Initializing application launcher...");
    app_launcher_initialize();
    
    KLOG("Opening desktop windows...");
    gui_open_window("System Console", 10, 3, 40, 12);
    gui_open_window("Task Monitor", 52, 3, 26, 10);
    gui_open_window("File Manager", 15, 14, 50, 8);

    KLOG("Initializing window manager...");
    window_manager_initialize();
    window_manager_create_window(2, 2, 36, 10, "System Console");
    window_manager_create_window(40, 2, 36, 10, "Task Monitor");
    gui_render();
    
    KLOG("Initializing serial port driver...");
    // Already done
    
    KLOG("Initializing USB host controller...");
    usb_initialize();

    KLOG("Initializing audio subsystem...");
    audio_initialize();
    if (audio_is_ready()) {
        audio_set_volume(128);
        audio_play_tone(880, 100);
    }

    KLOG("Initializing network subsystem...");
    if (network_initialize()) {
        uint32_t ip = network_get_ip();
        uint32_t gw = network_get_gateway();
        KLOG("Network ready: IP=%u.%u.%u.%u GW=%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
             (gw >> 24) & 0xFF, (gw >> 16) & 0xFF, (gw >> 8) & 0xFF, gw & 0xFF);
    }

    KLOG("Initializing mouse subsystem...");
    mouse_initialize();
    
    KLOG("Initializing AHCI storage driver...");
    ahci_initialize();
    
    /* Initialize filesystem */
    KLOG("Initializing FAT32 filesystem...");
    fat32_initialize();
    
    /* Enable interrupts */
    KLOG("Enabling interrupts...");
    enable_interrupts();
    
    kernel_initialized = true;
    KLOG("======== KERNEL INITIALIZATION COMPLETE ========");
    KLOG("Welcome to .C OS!");
    
    /* Initialize keyboard driver */
    KLOG("Initializing keyboard subsystem...");
    keyboard_initialize();

    /* Create initial system processes */
    KLOG("Creating initial system processes...");
    tcb_t* idle_proc = create_task("idle", idle_task_func, 0);
    if (idle_proc) {
        KLOG("Idle process created with PID %u", idle_proc->pid);
    }
    tcb_t* shell_proc = create_task("shell", shell_task, 1);
    if (shell_proc) {
        KLOG("Shell process created with PID %u", shell_proc->pid);
    }

    KLOG("Starting background service tasks...");
    background_services_initialize();
    
    /* Main kernel loop */
    KLOG("Entering main kernel loop...");
    while (1) {
        if (scheduler_pending()) {
            schedule();
        }
        // Halt CPU until next interrupt
        asm volatile("hlt");
    }
}

/* ============================================================================
 * KERNEL PRINTF IMPLEMENTATION (for logging)
 * ============================================================================ */

int kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // Simple format string parsing
    int written = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                int val = va_arg(args, int);
                written += 10;  // Approximation
            } else if (*fmt == 'x' || *fmt == 'X') {
                uint64_t val = va_arg(args, uint64_t);
                written += 16;
            } else if (*fmt == 's') {
                const char* str = va_arg(args, const char*);
                written += kstrlen(str);
            } else if (*fmt == 'p') {
                void* ptr = va_arg(args, void*);
                written += 18;  // "0x" + 16 hex digits
            } else if (*fmt == 'u') {
                uint64_t val = va_arg(args, uint64_t);
                written += 20;
            } else if (*fmt == '%') {
                written++;
            }
            fmt++;
        } else if (*fmt == '\n') {
            serial_putchar('\n');
            written++;
            fmt++;
        } else {
            serial_putchar(*fmt);
            written++;
            fmt++;
        }
    }
    
    va_end(args);
    return written;
}

static size_t kstrlen(const char* str) {
    size_t len = 0;
    while (*str++) len++;
    return len;
}

/* ============================================================================
 * KERNEL PANIC - Fatal error handler
 * ============================================================================ */

void kpanic(const char* msg) {
    disable_interrupts();
    vga_clear_screen();
    KERROR("KERNEL PANIC: %s", msg);
    KERROR("System halted.");
    
    while (1) {
        asm volatile("cli; hlt");
    }
}
