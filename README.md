# .C OS - Complete Bare-Metal x86_64 Operating System

A comprehensive, educational bare-metal kernel for x86_64 architecture, written in C and Assembly. Built to run on QEMU and real hardware, featuring a monolithic kernel design with full memory management, multitasking, device drivers, and filesystem support.

## Architecture Overview

```
┌─────────────────────────────────────────┐
│     USER SPACE (Ring 3)                 │
│  - User Programs                        │
│  - System Call Interface                │
└──────────────┬──────────────────────────┘
               │ syscall/sysret
┌──────────────▼──────────────────────────┐
│     KERNEL SPACE (Ring 0)               │
│  ┌──────────────────────────────────┐   │
│  │  System Call Dispatcher          │   │
│  ├──────────────────────────────────┤   │
│  │  Process Scheduler               │   │
│  │  (Round-Robin, Context Switching)│   │
│  ├──────────────────────────────────┤   │
│  │  Memory Management               │   │
│  │  - Virtual Memory (4-level paging)   │
│  │  - Physical Memory (bitmap PMM)  │   │
│  │  - Kernel Heap (slab+buddy)      │   │
│  ├──────────────────────────────────┤   │
│  │  Device Drivers                  │   │
│  │  - VGA/Framebuffer Graphics      │   │
│  │  - Serial Port (COM1)            │   │
│  │  - AHCI Storage (SATA)           │   │
│  ├──────────────────────────────────┤   │
│  │  Filesystem                      │   │
│  │  - FAT32 Support                 │   │
│  ├──────────────────────────────────┤   │
│  │  Interrupt/Exception Handling    │   │
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

## Project Structure

```
.C os/
├── boot/                    # Bootloader & boot code
│   ├── boot.asm            # Entry point (multiboot2)
│   ├── gdt.asm             # Global Descriptor Table
│   ├── idt.asm             # Interrupt Descriptor Table
│   ├── syscall_entry.asm   # System call handler
│   ├── context_switch.asm  # Task context switching
│   └── multiboot2.h        # Multiboot2 header definitions
│
├── src/                     # Kernel core
│   ├── kernel.c            # Main kernel entry
│   ├── vmm.c               # Virtual Memory Manager
│   ├── pmm.c               # Physical Memory Manager
│   ├── heap.c              # Kernel allocator (slab+buddy)
│   ├── scheduler.c         # Process scheduler
│   ├── interrupts.c        # IRQ/Exception handlers
│   ├── syscalls.c          # System call dispatcher
│   └── serial.c            # Serial port driver
│
├── drivers/                 # Device drivers
│   ├── vga.c               # VGA text & framebuffer
│   └── ahci.c              # SATA storage driver
│
├── fs/                      # Filesystem implementations
│   └── fat32.c             # FAT32 filesystem
│
├── include/                 # Header files
│   └── kernel.h            # Kernel API definitions
│
├── libc/                    # Standard library (minimal)
│   └── (To be implemented)
│
├── build/                   # Build output
│   ├── kernel.elf          # Linked ELF kernel
│   ├── kernel.bin          # Binary kernel image
│   └── kernel.iso          # Bootable ISO
│
├── Makefile                # Build system
├── link.ld                 # Linker script
└── README.md               # This file
```

## Key Features Implemented

### 1. **Memory Management**
- **Virtual Memory (VMM)**: x86_64 4-level paging with page table structures
- **Physical Memory (PMM)**: Bitmap-based frame allocator
- **Kernel Heap**: Hybrid slab/buddy allocator with coalescing

### 2. **Process Management**
- **Scheduler**: Round-robin scheduling with timeslice preemption
- **Task Control Block (TCB)**: Process state tracking
- **Context Switching**: Full CPU register save/restore
- **Task States**: RUNNING, READY, SLEEPING, ZOMBIE

### 3. **System Calls**
- SYSCALL/SYSRET MSR-based interface (fast)
- System call dispatcher with privilege checking
- Implemented syscalls:
  - `SYS_GET_PID` - Get current process ID
  - `SYS_EXIT` - Terminate process
  - `SYS_SND_BEEP` - PC speaker control
  - `SYS_KPRINTF` - Kernel logging proxy
  - `SYS_OPEN_FILE` - File operations

### 4. **Interrupt & Exception Handling**
- IDT (Interrupt Descriptor Table) setup
- Exception handlers (division by zero, page fault, GPF, etc.)
- IRQ handlers for hardware interrupts
- PIT (timer) interrupt for task scheduling

### 5. **Device Drivers**
- **VGA Text Mode**: 80x25 character display
- **Framebuffer Graphics**: Linear framebuffer with pixel drawing
- **Serial Port (COM1)**: 115200 baud for debugging output
- **AHCI Storage**: SATA disk interface (mock implementation)

### 6. **Filesystem**
- **FAT32**: Cluster-based filesystem with directory entries
- File open/read/write/close operations

### 7. **Bootloader**
- **Multiboot2 compliant** (GRUB compatible)
- Automatic switch to 64-bit long mode
- GDT setup for 64-bit execution

## Building

### Prerequisites

**Linux/WSL:**
```bash
# Install cross-compiler toolchain
sudo apt-get install gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
# or use OSDEV tools:
# x86_64-elf-gcc, x86_64-elf-ld, etc.
```

**QEMU (emulator):**
```bash
sudo apt-get install qemu-system-x86
```

**NASM (assembler):**
```bash
sudo apt-get install nasm
```

### Building the Kernel

```bash
# Build kernel binary
make clean && make

# Output: build/kernel.bin

# Build and disassemble
make disasm  # Creates build/kernel.disasm

# Extract symbols
make symbols # Creates build/kernel.symbols
```

## Running

### QEMU Emulation (recommended)

```bash
# Standard run
make qemu

# With GDB debugging
make qemu-debug
# Then in another terminal: gdb
# (gdb) target remote localhost:1234
# (gdb) symbol-file build/kernel.elf
# (gdb) continue
```

### Bochs Emulation

```bash
make bochs
```

### Real Hardware

1. Build ISO:
   ```bash
   make iso
   ```

2. Burn to USB:
   ```bash
   sudo dd if=build/kernel.iso of=/dev/sdX bs=4M
   ```

3. Boot from USB

## Code Examples

### Allocating Memory

```c
#include "kernel.h"

void* buffer = kmalloc(256, "my_driver");
if (buffer) {
    // Use buffer
    kfree(buffer);
}
```

### Creating a Task

```c
void my_task_function(void) {
    kprintf("Task running!\n");
}

tcb_t* task = create_task("my_task", my_task_function, 5);
if (task) {
    kprintf("Task PID: %u\n", task->pid);
}
```

### Writing to Display

```c
// Text mode
vga_write_char(10, 5, 'A', VGA_WHITE << 4 | VGA_BLACK);

// Graphics mode
fb_draw_pixel(100, 100, 0xFF0000);  // Red pixel
fb_draw_rectangle(50, 50, 100, 100, 0x00FF00);  // Green rectangle
fb_swap_buffers();
```

### System Call from User Space

```c
// User program
#define SYS_GET_PID 5

uint32_t pid;
asm volatile(
    "mov $5, %%rax\n\t"  // syscall ID
    "syscall\n\t"
    "mov %%rax, %0"
    : "=r"(pid)
    :
    : "rax"
);
kprintf("My PID: %u\n", pid);
```

## Architecture Details

### x86_64 4-Level Paging

Virtual address decomposition:
```
Bits 63-48: Sign extension
Bits 47-39: PML4 index (512 entries)
Bits 38-30: PDPT index (512 entries)
Bits 29-21: PD index (512 entries)
Bits 20-12: PT index (512 entries)
Bits 11-0:  Page offset (4KB pages)
```

### Task Context Format

When a task is preempted, CPU state is saved:
```c
struct cpu_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;  // Registers
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;  // Processor state
};
```

## Known Limitations & TODO

- [ ] Multi-CPU support (SMP)
- [ ] Advanced filesystem features (ext4, NTFS)
- [ ] Full AHCI driver implementation (currently mock)
- [ ] Network stack (Ethernet, TCP/IP)
- [ ] ACPI table parsing
- [ ] USB support
- [ ] Power management
- [ ] Signal handling
- [ ] Process forking
- [ ] Dynamic memory expansion
- [ ] Comprehensive error handling

## Security Notes

- **Privilege Separation**: User space vs. kernel space (Ring 3 vs. Ring 0)
- **Syscall Validation**: Syscall dispatcher checks for kernel memory access violations
- **Stack Canaries**: Not yet implemented
- **DEP/NX**: Supported via NX bit in page tables

## References

- **OSDev.org**: https://wiki.osdev.org
- **x86-64 ABI**: System V AMD64 ABI
- **Intel 64 Manual**: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- **Multiboot2 Specification**: https://www.gnu.org/software/grub/manual/multiboot2/

## License

Educational use. Built for learning OS kernel development.

## Author Notes

This is a comprehensive implementation of core OS concepts in bare-metal x86_64 code. It serves as both:
1. A reference implementation for OS kernel architecture
2. A foundation for further development and experimentation
3. An educational resource for systems programming

The code is heavily commented and documented for learning purposes.

---

**Happy kernel hacking!** 🚀
