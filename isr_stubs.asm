; Minimal interrupt descriptor table (IDT) assembly setup
; The C initializer will load the IDT and PIC will be remapped.

bits 64

; ISR Macros (generated in C, but used here for exceptions and IRQs)
%macro ISR_NOERRCODE 1
    global isr%1
    isr%1:
        push 0              ; Push fake error code
        push %1             ; Push interrupt number
        jmp common_isr
%endmacro

%macro ISR_ERRCODE 1
    global isr%1
    isr%1:
        push %1             ; Push interrupt number
        jmp common_isr
%endmacro

%macro IRQ_NOERRCODE 1
    global irq%1
    irq%1:
        push 0              ; Push fake error code
        push %1             ; Push interrupt number (remapped IRQ base offset from C)
        jmp common_isr
%endmacro

; Generate ISRs 0-20 (basic exceptions)
ISR_NOERRCODE 0    ; Division by zero
ISR_NOERRCODE 1    ; Debug
ISR_NOERRCODE 2    ; NMI
ISR_NOERRCODE 3    ; Breakpoint
ISR_NOERRCODE 4    ; Overflow
ISR_NOERRCODE 5    ; Bound range exceeded
ISR_NOERRCODE 6    ; Invalid opcode
ISR_NOERRCODE 7    ; Device not available
ISR_ERRCODE 8      ; Double fault
ISR_NOERRCODE 9    ; Coprocessor segment overrun
ISR_ERRCODE 10     ; Invalid TSS
ISR_ERRCODE 11     ; Segment not present
ISR_ERRCODE 12     ; Stack segment fault
ISR_ERRCODE 13     ; General protection fault
ISR_ERRCODE 14     ; Page fault
ISR_NOERRCODE 15   ; Reserved
ISR_NOERRCODE 16   ; FPU exception
ISR_ERRCODE 17     ; Alignment check
ISR_NOERRCODE 18   ; Machine check
ISR_NOERRCODE 19   ; SIMD exception
ISR_NOERRCODE 20   ; Virtualization exception

; Generate IRQ stubs 0-15
IRQ_NOERRCODE 32
IRQ_NOERRCODE 33
IRQ_NOERRCODE 34
IRQ_NOERRCODE 35
IRQ_NOERRCODE 36
IRQ_NOERRCODE 37
IRQ_NOERRCODE 38
IRQ_NOERRCODE 39
IRQ_NOERRCODE 40
IRQ_NOERRCODE 41
IRQ_NOERRCODE 42
IRQ_NOERRCODE 43
IRQ_NOERRCODE 44
IRQ_NOERRCODE 45
IRQ_NOERRCODE 46
IRQ_NOERRCODE 47

; Common ISR handler
common_isr:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass pointer to saved register frame to C
    mov rdi, rsp
    extern isr_handler
    call isr_handler
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16          ; Pop fake error code and interrupt number
    iretq
