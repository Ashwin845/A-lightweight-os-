; Context switch assembly implementation
; Saves/restores CPU context for task switching

bits 64

section .text
    global switch_task_context
    
    ; void switch_task_context(uintptr_t* old_rsp, uintptr_t new_rsp)
    ; RDI = old_rsp (pointer to save RSP to)
    ; RSI = new_rsp (new task stack pointer)
    
    switch_task_context:
        ; Save all callee-saved registers on stack
        push rbx
        push rbp
        push r12
        push r13
        push r14
        push r15
        
        ; Save old stack pointer
        mov [rdi], rsp
        
        ; Load new stack pointer
        mov rsp, rsi
        
        ; Restore all registers from new stack
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbp
        pop rbx
        
        ; Return to new task
        ret
