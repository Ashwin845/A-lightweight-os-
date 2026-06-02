; Global Descriptor Table (GDT) for 64-bit mode setup
; Assembler: NASM

section .data
    align 8
    global gdt64
    gdt64:
        ; Null descriptor
        dq 0
        
        ; 64-bit Kernel Code Segment (0x08)
        dw 0xFFFF           ; Limit (ignored in 64-bit)
        dw 0                ; Base (low)
        db 0                ; Base (mid)
        db 0x9A             ; Present, Ring 0, Code, Non-conforming, Readable
        db 0xA0             ; Granularity: 4KB, 64-bit
        db 0                ; Base (high)
        
        ; 64-bit Kernel Data Segment (0x10)
        dw 0xFFFF           ; Limit
        dw 0                ; Base (low)
        db 0                ; Base (mid)
        db 0x92             ; Present, Ring 0, Data, Writable
        db 0xA0             ; Granularity: 4KB, 64-bit
        db 0                ; Base (high)
        
        ; 64-bit User Code Segment (0x18)
        dw 0xFFFF
        dw 0
        db 0
        db 0xFA             ; Present, Ring 3, Code, Non-conforming, Readable
        db 0xA0
        db 0
        
        ; 64-bit User Data Segment (0x20)
        dw 0xFFFF
        dw 0
        db 0
        db 0xF2             ; Present, Ring 3, Data, Writable
        db 0xA0
        db 0
        
        ; TSS segment placeholder (0x28) - will be filled at runtime
        dq 0
        dq 0
        
    global gdt64_pointer
gdt64_pointer:
        dw ($ - gdt64 - 1)  ; GDT limit
        dq gdt64            ; GDT base address (will be updated to use RIP-relative)
