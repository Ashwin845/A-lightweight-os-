# QEMU Configuration & Debugging Guide

## QEMU Command Reference

### Basic Boot
```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin
```

### With Monitor (interactive shell)
```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin -monitor stdio
```

### Multiprocessor (2 CPUs)
```bash
qemu-system-x86_64 -m 1024 -smp 2 -kernel kernel.bin
```

### With GDB Debugging
```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin -s -S
```

### Redirecting Serial Output
```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin \
    -serial file:serial.log \
    -nographic
```

### Full Featured Launch
```bash
qemu-system-x86_64 \
    -m 2048 \                          # 2GB RAM
    -smp 2 \                            # 2 CPUs
    -kernel build/kernel.bin \
    -serial stdio \                     # Serial to stdout
    -monitor stdio \                    # Interactive monitor
    -d cpu_reset,guest_errors \         # Debug output
    -no-shutdown \
    -no-reboot
```

## QEMU Monitor Commands

Once in QEMU with `-monitor stdio`:

```
(qemu) help                      # Show all commands
(qemu) info registers           # Show CPU registers
(qemu) info memory              # Memory layout
(qemu) info cpus                # CPU info
(qemu) dump-guest-memory core.dump  # Core dump
(qemu) quit                     # Exit
```

## Debugging with GDB

### Setup

**Terminal 1: Start QEMU**
```bash
qemu-system-x86_64 -m 1024 -kernel build/kernel.bin -s -S
```

**Terminal 2: Start GDB**
```bash
gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) symbol-file build/kernel.elf
```

### GDB Workflow

```bash
# Load kernel symbols
(gdb) file build/kernel.elf

# Connect to QEMU debug server
(gdb) target remote localhost:1234

# Set breakpoints
(gdb) break kernel_main
(gdb) break vmm_initialize
(gdb) break *0x100000       # Physical address

# Show breakpoints
(gdb) info breakpoints

# Continue to next breakpoint
(gdb) continue

# Step
(gdb) step                  # Into function
(gdb) next                  # Over function
(gdb) ni                    # Next instruction

# Print variables
(gdb) print $rax
(gdb) print *((int*)0x12345)
(gdb) x/10i $rip            # Disassemble 10 instructions from RIP

# Show registers
(gdb) info registers
(gdb) display $rax          # Auto-show on each step

# Watch memory
(gdb) watch 0x12345
(gdb) info watchpoints

# View stack
(gdb) backtrace
(gdb) frame 0
```

## Debugging Common Issues

### Kernel Hangs in Initialization

```bash
# Add breakpoint at kernel_main
(gdb) break kernel_main
(gdb) continue

# Step through initialization
(gdb) next
(gdb) next
(gdb) info registers          # Check for issues
```

### Triple Fault (Reboot Loop)

Likely causes:
- Invalid GDT/IDT
- Corrupted stack
- Unhandled exception

Debug:
```bash
(gdb) break exception_handler_*
(gdb) continue              # See which exception triggers
(gdb) info registers        # Examine state
```

### Memory Access Violations

```bash
# Set watchpoint
(gdb) watch *(uint64_t*)0x123456

# Check MMU state
(gdb) info registers        # Look at CR3 (PML4 address)
```

## QEMU Advanced Debugging

### Serial Output Logging

```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin \
    -serial file:debug.log

# Monitor output in real-time
tail -f debug.log
```

### Trace Execution

```bash
qemu-system-x86_64 -m 1024 -kernel kernel.bin \
    -d trace:syscall \              # Trace syscalls
    -trace syscall_entry \          # Custom trace events
    2>&1 | tee trace.log
```

### CPU Trace Dump

```bash
(qemu) dump-guest-memory /tmp/memdump.bin
(qemu) dump-guest-memory /tmp/core.elf elf

# Analyze with GDB
gdb
(gdb) core-file /tmp/core.elf
(gdb) info registers
```

## Performance Profiling

### Measure Boot Time

```bash
time qemu-system-x86_64 -m 1024 -kernel kernel.bin -nographic
```

### Monitor CPU Usage

```bash
# In QEMU monitor
(qemu) info cpu

# Or with system tools
qemu-system-x86_64 [...] &
QEMU_PID=$!
top -p $QEMU_PID
```

## Multithreading Debugging

When debugging kernel with multiple CPUs:

```bash
(gdb) info threads
(gdb) thread 1              # Switch to CPU 1
(gdb) thread 2              # Switch to CPU 2
```

## Integration Testing

### Test Script

```bash
#!/bin/bash
# test_kernel.sh

TIMEOUT=10

echo "Building kernel..."
make clean && make || exit 1

echo "Launching QEMU..."
timeout $TIMEOUT qemu-system-x86_64 \
    -m 1024 \
    -kernel build/kernel.bin \
    -serial file:test_output.log \
    -nographic \
    2>&1

echo "Test output:"
cat test_output.log

# Check for expected messages
if grep -q "KERNEL INITIALIZATION COMPLETE" test_output.log; then
    echo "✓ Boot successful"
    exit 0
else
    echo "✗ Boot failed"
    exit 1
fi
```

Run: `bash test_kernel.sh`

## Troubleshooting QEMU Issues

### "Could not load or parse multiboot2 header"

Kernel not multiboot2 compliant:
```bash
strings build/kernel.bin | grep -i multiboot
# Should show multiboot string

# Check ELF header
file build/kernel.elf
readelf -h build/kernel.elf
```

### QEMU crashes on launch

```bash
# Try minimal configuration
qemu-system-x86_64 -kernel kernel.bin -nographic

# Update QEMU
sudo apt upgrade qemu-system-x86
```

### Extremely slow execution

```bash
# Use KVM acceleration (Linux only)
qemu-system-x86_64 -m 1024 -kernel kernel.bin -enable-kvm

# Use TCG (slower, cross-platform)
qemu-system-x86_64 -m 1024 -kernel kernel.bin -accel tcg
```

## Recommended Reading

- QEMU Manual: https://wiki.qemu.org
- GDB Documentation: https://sourceware.org/gdb/documentation/
- x86-64 Debugging: https://wiki.osdev.org/Debugging
