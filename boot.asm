; Multiboot2 bootloader entry point (x86_64 GRUB-compatible)
; Assembler: NASM
; This file sets up the multiboot2 header and switches to long mode (x86_64)

bits 32

%define ALIGN 1<<0
%define MEMINFO 1<<1
%define MAGIC 0xe85250d6
%define ARCH 0  ; i386 architecture
%define CHECKSUM -(MAGIC + ARCH + 8)

; Multiboot2 header
section .multiboot_header
header_start:
    magic dd MAGIC
    arch dd ARCH
    header_length dd (header_end - header_start)
    checksum dd -(MAGIC + ARCH + (header_end - header_start))
    
    ; Information request tag
    ; (use explicit zero padding instead of align to avoid 0x90 fill)
    dw 1            ; Type: Information Request
    dw 0            ; Flags
    dd 16           ; Size
    dd 6            ; Request: Memory map information
    dd 0            ; Padding to 8-byte boundary
    ; End tag
    dd 0            ; Padding to 8-byte boundary
    dw 0            ; Type: End
    dw 0            ; Flags
    dd 8            ; Size
    
header_end:

; Stack space for bootloader
section .bss
    align 16
    stack_bottom:
        resb 16384  ; 16KB stack
    stack_top:

section .bss
    align 4096
boot_pml4:
    resq 512
boot_pdpt:
    resq 512

section .text
    extern kernel_main
    extern gdt64
    extern gdt64_pointer

    global _start
    _start:
        ; Disable interrupts
        cli
        
        ; Save multiboot info pointer (ebx) and magic (eax)
        mov edi, ebx        ; edi = multiboot info
        mov esi, eax        ; esi = multiboot magic
        
        ; Enable SSE instructions (required by System V ABI)
        mov eax, cr0
        and al, 0xFB        ; Clear EM
        or al, 0x02         ; Set MP
        mov cr0, eax
        
        mov eax, cr4
        or ax, 0x600        ; Set OSFXSR and OSXMMEXCPT
        mov cr4, eax
        
        ; Setup temporary stack
        mov esp, stack_top
        
        ; Setup 64-bit GDT
        lgdt [rel gdt64_pointer]

        ; Build identity-mapped page tables for long mode transition
        mov ecx, boot_pdpt
        mov ebx, 0
        mov eax, 0x00000083            ; Present, RW, PS
        mov edi, 4                     ; map first 4GB for boot
    long_mode_paging_loop:
        mov edx, ebx
        or edx, eax
        mov [ecx], edx
        mov dword [ecx + 4], 0
        add ebx, 0x40000000            ; next 1GB page
        add ecx, 8
        dec edi
        jne long_mode_paging_loop

        mov eax, boot_pdpt
        or eax, 0x03
        mov [boot_pml4], eax
        mov dword [boot_pml4 + 4], 0

        ; Enable PAE and long mode
        mov eax, cr4
        or eax, (1 << 5)               ; PAE
        mov cr4, eax

        mov ecx, 0xC0000080            ; IA32_EFER
        rdmsr
        or eax, (1 << 8)               ; LME
        wrmsr

        mov eax, boot_pml4
        mov cr3, eax

        mov eax, cr0
        or eax, 0x80000001             ; PG + PE
        mov cr0, eax

        ; Perform far jump to 64-bit code segment
        jmp 0x08:bits64_entry
        
    bits 64
    bits64_entry:
        ; Reload data segments for 64-bit
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        
        ; Multiboot info pointer and magic are already in rdi/rsi from the 32-bit entry
        ; Call the kernel main entry point directly.
        call kernel_main
        
        ; Halt
        cli
        hlt
        jmp $
