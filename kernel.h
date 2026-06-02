#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * KERNEL CORE ABSTRACTIONS & TYPE DEFINITIONS
 * ============================================================================ */

#ifndef NULL
#define NULL ((void*)0)
#endif
#define KERNEL_BASE 0xFFFFFFFF80000000UL
#define KERNEL_STACK_SIZE 0x4000 // 16KB per kernel stack
#define PAGE_SIZE 4096

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t base_class;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
} pci_device_t;

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
bool pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out);
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_initialize(void);

/* Kernel logging macro */
#define KLOG(fmt, ...) kprintf("[KERNEL] " fmt "\n", ##__VA_ARGS__)
#define KERROR(fmt, ...) kprintf("[ERROR] " fmt "\n", ##__VA_ARGS__)

/* ============================================================================
 * MEMORY MANAGEMENT EXPORTS
 * ============================================================================ */

// VMM - Virtual Memory Management
void vmm_initialize(void);
void vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags);
void vmm_unmap_page(uint64_t vaddr);
uint64_t vmm_translate(uint64_t vaddr);

// PMM - Physical Memory Management  
void pmm_initialize(uint64_t total_memory);
void pmm_add_available_range(uint64_t address, uint64_t length);
void pmm_reserve_range(uint64_t address, uint64_t length);
uint64_t pmm_allocate_frame(void);
void pmm_free_frame(uint64_t paddr);
uint64_t pmm_get_available(void);

// Heap - Kernel Allocator
void* kmalloc(size_t size, const char* owner);
void kfree(void* ptr);
void heap_coalesce(void);
void heap_initialize(void);

/* ============================================================================
 * PROCESS MANAGEMENT EXPORTS
 * ============================================================================ */

typedef enum {
    TASK_RUNNING = 0,
    TASK_READY = 1,
    TASK_SLEEPING = 2,
    TASK_ZOMBIE = 3
} task_state_t;

typedef struct tcb {
    uint32_t pid;
    const char* name;
    task_state_t state;
    uint32_t priority;
    uint32_t timeslice;
    uintptr_t stack_pointer;
    void (*entry)(void);
    uint32_t memory_kb;
    struct tcb* next;
} tcb_t;

void scheduler_initialize(void);
void schedule(void);
void task_yield(void);
void request_schedule(void);
bool scheduler_pending(void);
void switch_task_context(uintptr_t* old_rsp, uintptr_t new_rsp);
tcb_t* create_task(const char* name, void (*entry)(void), uint32_t priority);
void kill_task(uint32_t pid);
tcb_t* get_current_task(void);
void background_services_initialize(void);
/* ============================================================================
 * KEYBOARD & INPUT
 * ============================================================================
 */

void keyboard_initialize(void);
bool keyboard_has_data(void);
char keyboard_read_char(void);

/* ============================================================================
 * MOUSE & USB INPUT
 * ============================================================================
 */

void usb_initialize(void);
void usb_poll(void);
bool usb_has_keyboard(void);
bool usb_get_keyboard_char(char* c);
bool usb_has_mouse(void);
bool usb_get_mouse_report(int8_t* dx, int8_t* dy, bool* left, bool* right, bool* middle);

void mouse_initialize(void);
bool mouse_has_event(void);
void mouse_get_event(int* dx, int* dy, bool* left, bool* right, bool* middle);
int mouse_get_x(void);
int mouse_get_y(void);

/* ============================================================================ * AUDIO & SOUND
 * ============================================================================ */

void audio_initialize(void);
bool audio_is_ready(void);
void audio_set_volume(uint8_t volume);
void audio_play_tone(uint32_t frequency, uint32_t duration_ms);
void audio_stop_tone(void);
bool audio_play_pcm(const int16_t* samples, size_t sample_count, uint32_t sample_rate);

/* ============================================================================ * INTERRUPT & EXCEPTION HANDLING
 * ============================================================================
 */

void idt_initialize(void);
void set_interrupt_handler(int irq, void (*handler)(void));
void pit_initialize(uint32_t frequency);
void enable_interrupts(void);
void disable_interrupts(void);

void window_manager_initialize(void);
void window_manager_create_window(int x, int y, int width, int height, const char* title);
void window_manager_render(void);
void window_manager_set_focus(int window_id);
void window_manager_raise_window(int window_id);
void window_manager_lower_window(int window_id);

void gui_initialize(void);
void gui_open_window(const char* title, int x, int y, int width, int height);
void gui_draw_icon(int x, int y, const char* label);
void gui_render(void);
void gui_handle_right_click(int x, int y);
void gui_handle_mouse_click(int x, int y, bool left, bool right, bool middle);
void gui_set_resolution(int width, int height);

void notification_center_initialize(void);
void notification_post(const char* title, const char* message, int priority);
void notification_center_render(void);
void notification_center_toggle(void);

void control_center_initialize(void);
void control_center_toggle(void);
void control_center_render(void);

void clipboard_initialize(void);
void clipboard_copy(const char* data, size_t size);
const char* clipboard_paste(void);
void clipboard_clear(void);

void file_ops_initialize(void);
int file_op_delete(const char* path);
int file_op_copy(const char* src, const char* dst);
int file_op_mkdir(const char* path);
int file_op_move(const char* src, const char* dst);

void app_launcher_initialize(void);
int app_launch(const char* app_name);
void app_launcher_render(void);

typedef struct {
    volatile uint8_t locked;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spin_lock(spinlock_t* lock);
void spin_unlock(spinlock_t* lock);

/* ============================================================================
 * SYSTEM CALLS INTERFACE
 * ============================================================================ */

#define SYS_GET_PID     5
#define SYS_SND_BEEP    3
#define SYS_KPRINTF     1
#define SYS_OPEN_FILE   2
#define SYS_EXIT        60

uint64_t syscall_dispatcher(uint64_t syscall_id, uint64_t arg1, uint64_t arg2, uint64_t arg3);
void enable_syscall_engine(void);

/* ============================================================================
 * DEVICE DRIVER EXPORTS
 * ============================================================================ */

// VGA/Framebuffer graphics
void vga_initialize(void);
void vga_clear_screen(void);
void vga_write_char(int x, int y, char c, uint8_t color);
void fb_draw_pixel(int x, int y, uint32_t rgb);
void fb_swap_buffers(void);

// AHCI (SATA) Storage Driver
void ahci_initialize(void);
int ahci_read_sector(uint32_t lba, uint8_t* buffer);
int ahci_write_sector(uint32_t lba, uint8_t* buffer);

// Serial Port (COM1) for debugging
void serial_initialize(void);
void serial_putchar(char c);
char serial_getchar(void);
void spool_print(const char* text);
void spool_audio_tone(uint32_t frequency, uint32_t duration_ms);

/* ============================================================================
 * FILESYSTEM EXPORTS
 * ============================================================================ */

// FAT32 Filesystem
void fat32_initialize(void);
int fat32_open(const char* filename);
int fat32_read(int fd, uint8_t* buffer, size_t size);
int fat32_write(int fd, uint8_t* buffer, size_t size);
void fat32_close(int fd);
bool fat32_enumerate_root(void (*callback)(const char* filename, uint32_t size, void* ctx), void* ctx);

// ext2 Filesystem (optional)
void ext2_initialize(void);
/* ============================================================================
 * NETWORK DRIVER AND IP STACK
 * ============================================================================ */
bool network_initialize(void);
void network_poll(void);
uint32_t network_get_ip(void);
uint32_t network_get_gateway(void);
const uint8_t* network_get_mac(void);
/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

int kprintf(const char* fmt, ...);
void kpanic(const char* msg);
uint64_t rdmsr(uint32_t msr);
void wrmsr(uint32_t msr, uint64_t value);

#endif // KERNEL_H
