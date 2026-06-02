; SYSCALL assembly entry point for 64-bit system calls
; When a user program executes SYSCALL, control jumps here
; RCX = user RIP, R11 = user RFLAGS
; RAX = syscall ID, RDI/RSI/RDX = args 1-3

bits 64

section .text
    extern syscall_dispatcher
    extern current_task
    
    global syscall_entry
    syscall_entry:
        ; RAX = syscall ID (arg0)
        ; RDI = arg1
        ; RSI = arg2
        ; RDX = arg3
        ; (RCX and R11 already saved by CPU)
        
        ; Save user stack pointer in R12 (callee-saved)
        mov r12, rsp
        
        ; Switch to kernel stack
        mov rsp, [rel kernel_stack_top]
        
        ; Call C dispatcher with syscall args
        ; syscall_dispatcher(syscall_id, arg1, arg2, arg3)
        ; Already in RDI, RSI, RDX (syscall dispatcher args same as Unix ABI)
        call syscall_dispatcher
        
        ; RAX now contains return value
        ; RCX = user RIP (from CPU)
        ; R11 = user RFLAGS (from CPU)
        
        ; Restore user stack
        mov rsp, r12
        
        ; Return to user code
        sysretq

section .data
    kernel_stack_top: dq 0x2000000  ; Mock kernel stack
