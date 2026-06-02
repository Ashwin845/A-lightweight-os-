# Development Environment Setup

## Windows Setup (Using WSL2)

### 1. Install WSL2 with Ubuntu

```bash
# In PowerShell as Administrator
wsl --install -d Ubuntu-22.04
```

### 2. Install Cross-Compiler Toolchain

```bash
# In WSL terminal
sudo apt update && sudo apt upgrade -y

# Install build essentials
sudo apt install -y build-essential nasm wget

# Download and install x86_64-elf toolchain (recommended)
mkdir -p ~/osdev
cd ~/osdev

# Download GCC cross-compiler (or build from source)
wget https://github.com/lordmilko/i686-elf-tools/releases/download/7.1.0/i686-elf-tools-windows.zip

# For x86_64:
# Build from source: https://wiki.osdev.org/GCC_Cross-Compiler
```

### 3. Install QEMU

```bash
sudo apt install -y qemu-system-x86
```

### 4. Install Optional Tools

```bash
sudo apt install -y \
    gdb \              # Debugging
    bochs \            # Alternative emulator
    grub-pc-bin \      # GRUB bootloader
    xorriso \          # ISO creation
    git \              # Version control
    vim nano           # Text editors
```

## Linux Setup

```bash
sudo apt update && sudo apt upgrade -y

sudo apt install -y \
    gcc \
    binutils \
    nasm \
    make \
    qemu-system-x86 \
    gdb \
    build-essential \
    grub-pc-bin \
    xorriso
```

## macOS Setup (using Homebrew)

```bash
brew install nasm qemu gcc binutils

# Cross-compiler (via osxcross or MacPorts)
# Alternatively, use Docker
```

## Docker Setup (cross-platform)

```dockerfile
# Dockerfile.osdev
FROM ubuntu:22.04

RUN apt update && apt install -y \
    build-essential nasm wget git qemu-system-x86 gdb \
    grub-pc-bin xorriso binutils

# Download/install x86_64-elf toolchain
# (See OSdev Wiki for compilation)

WORKDIR /os
```

Usage:
```bash
docker build -f Dockerfile.osdev -t osdev .
docker run -it -v $(pwd):/os osdev make
```

## Verifying Installation

```bash
# Check cross-compiler
x86_64-elf-gcc --version

# Check NASM
nasm -version

# Check QEMU
qemu-system-x86_64 --version

# Check GRUB
grub-file --version
```

## Building Without Cross-Compiler

If you don't have a cross-compiler, use the bootloader-less approach:

1. Compile with native GCC: `-m64 -fno-pic`
2. Use linker script to map at `0x100000`
3. Boot directly with QEMU's `-kernel` option

```bash
make qemu  # Uses -kernel, bypasses bootloader
```

## Debugging with GDB

```bash
# Terminal 1: Start QEMU with GDB server
make qemu-debug

# Terminal 2: Start GDB and connect
gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### Useful GDB Commands

```bash
(gdb) symbol-file build/kernel.elf     # Load symbols
(gdb) set architecture i386:x86-64     # Set x86_64 mode
(gdb) info registers                   # Show CPU registers
(gdb) disassemble $pc                  # Disassemble at PC
(gdb) x/10i $pc                        # Display 10 instructions
(gdb) display $rax                     # Auto-display RAX on each step
(gdb) step                             # Step into
(gdb) next                             # Step over
(gdb) continue                         # Continue execution
```

## Troubleshooting

### "x86_64-elf-gcc not found"

Build GCC cross-compiler from source:
```bash
# See: https://wiki.osdev.org/GCC_Cross-Compiler
cd ~/osdev
git clone git://sourceware.org/binutils-gdb.git
git clone https://github.com/gcc-mirror/gcc.git

# Configure and build (takes 30+ minutes)
mkdir -p build-{binutils,gcc}
cd build-binutils
../binutils-gdb/configure --target=x86_64-elf --prefix=$HOME/opt/cross
make -j4 && make install

# Repeat for GCC
export PATH=$PATH:$HOME/opt/cross/bin
```

### QEMU doesn't boot

Check:
- Kernel built correctly: `file build/kernel.bin`
- Multiboot header present: `strings build/kernel.bin | grep -i multiboot`
- Try with verbose: `make qemu 2>&1 | head -50`

### Make errors

```bash
# Clean and rebuild
make clean
rm -rf build/
make

# Check for missing files
ls -la boot/ src/ drivers/ fs/ include/
```

## IDE Configuration (VS Code)

Create `.vscode/settings.json`:
```json
{
    "C_Cpp.default.includePath": ["${workspaceFolder}/include"],
    "C_Cpp.default.defines": ["__x86_64__", "__bare_metal__"],
    "C_Cpp.default.compilerPath": "/usr/bin/x86_64-elf-gcc",
    "editor.formatOnSave": true,
    "files.exclude": {
        "build": true,
        ".git": true
    }
}
```

Create `.vscode/tasks.json`:
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Kernel",
            "type": "shell",
            "command": "make",
            "group": {"kind": "build", "isDefault": true}
        },
        {
            "label": "Run in QEMU",
            "type": "shell",
            "command": "make qemu",
            "group": {"kind": "test"}
        }
    ]
}
```

## Next Steps

1. Read `README.md` for architecture overview
2. Build the kernel: `make clean && make`
3. Run in emulator: `make qemu`
4. Study the code: Start with `src/kernel.c`
5. Experiment: Modify and rebuild
6. Debug with GDB: `make qemu-debug`

Happy OS development! 🚀
