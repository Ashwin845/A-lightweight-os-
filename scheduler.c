/* 
 * ============================================================================
 * PROCESS SCHEDULER & CONTEXT SWITCHING
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * TASK CONTROL BLOCK & SCHEDULER STATE
 * ============================================================================ */

typedef struct cpu_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_context_t;

#define MAX_TASKS 256
#define TASK_STACK_SIZE 0x4000
static tcb_t task_table[MAX_TASKS];
static uint8_t task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));
static uint32_t task_count = 0;
static uint32_t next_pid = 1;

static uintptr_t kernel_stack_pointer = 0;

tcb_t* current_task = NULL;
tcb_t* ready_queue_head = NULL;
static volatile bool schedule_requested = false;

extern void switch_task_context(uintptr_t* old_rsp, uintptr_t new_rsp);

void request_schedule(void) {
    schedule_requested = true;
}

static void task_entry_wrapper(void) {
    if (current_task && current_task->entry) {
        current_task->entry();
    }

    if (current_task) {
        KLOG("Task '%s' exited", current_task->name);
        current_task->state = TASK_ZOMBIE;
        kill_task(current_task->pid);
        task_yield();
    }

    while (1) {
        asm volatile("hlt");
    }
}

/* ============================================================================
 * SCHEDULER IMPLEMENTATION
 * ============================================================================ */

void scheduler_initialize(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_table[i].pid = 0;
        task_table[i].name = NULL;
        task_table[i].state = TASK_ZOMBIE;
        task_table[i].next = NULL;
    }
    task_count = 0;
    KLOG("Scheduler initialized with max %d tasks", MAX_TASKS);
}

tcb_t* create_task(const char* name, void (*entry)(void), uint32_t priority) {
    if (task_count >= MAX_TASKS) {
        KERROR("Scheduler: Max tasks reached");
        return NULL;
    }

    tcb_t* tcb = &task_table[task_count];
    tcb->pid = next_pid++;
    tcb->name = name;
    tcb->state = TASK_READY;
    tcb->priority = priority;
    tcb->timeslice = 20;
    tcb->memory_kb = 0;
    tcb->entry = entry;
    tcb->stack_pointer = 0;
    tcb->next = NULL;

    uintptr_t* stack_top = (uintptr_t*)((uintptr_t)&task_stacks[task_count][TASK_STACK_SIZE]);
    stack_top -= 7;
    stack_top[0] = 0;                  // r15
    stack_top[1] = 0;                  // r14
    stack_top[2] = 0;                  // r13
    stack_top[3] = 0;                  // r12
    stack_top[4] = 0;                  // rbp
    stack_top[5] = 0;                  // rbx
    stack_top[6] = (uintptr_t)task_entry_wrapper;
    tcb->stack_pointer = (uintptr_t)stack_top;

    if (ready_queue_head == NULL) {
        ready_queue_head = tcb;
        current_task = tcb;
        tcb->state = TASK_RUNNING;
    } else {
        tcb_t* iter = ready_queue_head;
        while (iter->next != NULL) {
            iter = iter->next;
        }
        iter->next = tcb;
    }

    task_count++;
    KLOG("Task created: '%s' (PID %u)", name, tcb->pid);
    return tcb;
}

void kill_task(uint32_t pid) {
    tcb_t* tcb = ready_queue_head;
    tcb_t* prev = NULL;
    
    while (tcb != NULL) {
        if (tcb->pid == pid) {
            tcb->state = TASK_ZOMBIE;
            
            /* Unlink from queue */
            if (prev == NULL) {
                ready_queue_head = tcb->next;
            } else {
                prev->next = tcb->next;
            }

            if (current_task == tcb) {
                current_task = NULL;
            }
            
            KLOG("Task killed: PID %u", pid);
            return;
        }
        prev = tcb;
        tcb = tcb->next;
    }
    
    KERROR("kill_task: PID %u not found", pid);
}

tcb_t* get_current_task(void) {
    return current_task;
}

void task_yield(void) {
    if (current_task == NULL || ready_queue_head == NULL) {
        return;
    }

    if (schedule_requested) {
        schedule_requested = false;
    }

    tcb_t* old_task = current_task;
    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }

    tcb_t* next_task = old_task->next;
    while (next_task != NULL && next_task->state != TASK_READY) {
        next_task = next_task->next;
    }

    if (next_task == NULL) {
        next_task = ready_queue_head;
        while (next_task != NULL && next_task->state != TASK_READY) {
            next_task = next_task->next;
        }
    }

    if (next_task == NULL || next_task == old_task) {
        old_task->state = TASK_RUNNING;
        return;
    }

    current_task = next_task;
    current_task->state = TASK_RUNNING;
    current_task->timeslice = 20;

    switch_task_context(&old_task->stack_pointer, current_task->stack_pointer);
}

void schedule(void) {
    task_yield();
}

bool scheduler_pending(void) {
    return schedule_requested;
}

/* ============================================================================
 * CONTEXT SWITCH HELPER (Assembly stub)
 * ============================================================================ */

// This would be implemented in assembly (see context_switch.asm)
// For now, we declare the external function
extern void switch_task_context(uintptr_t* old_rsp, uintptr_t new_rsp);
