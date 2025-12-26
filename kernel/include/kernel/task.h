#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_TASKS 64
#define TASK_KERNEL_STACK_SIZE 8192

// Task states
typedef enum {
    TASK_READY = 0,      // Ready to run
    TASK_RUNNING,        // Currently running
    TASK_BLOCKED,        // Blocked (waiting for I/O, sleep, etc.)
    TASK_TERMINATED      // Task has finished
} task_state_t;

// CPU register state for context switching
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi;
    uint32_t esp, ebp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} __attribute__((packed)) registers_t;

// Task Control Block (TCB)
typedef struct task {
    uint32_t id;                    // Task ID
    char name[32];                  // Task name
    task_state_t state;             // Current state
    registers_t regs;               // Saved CPU registers
    uint32_t kernel_stack;          // Kernel stack pointer
    uint32_t *page_directory;       // Page directory (for memory isolation)
    uint32_t priority;              // Task priority (0 = highest)
    uint32_t time_slice;            // Remaining time slice in ticks
    uint32_t total_time;            // Total CPU time used
    uint32_t sleep_until;           // Tick when sleep ends (if sleeping)
    bool sleeping;                  // Sleep flag for blocked tasks
    struct task *next;              // Next task in queue
} task_t;

// Initialize the task scheduler
void scheduler_init(void);

// Create a new task
task_t* task_create(const char *name, void (*entry_point)(void), uint32_t priority);

// Terminate the current task
void task_exit(void);

// Yield CPU to another task
void task_yield(void);

// Block current task
void task_block(void);

// Unblock a specific task
void task_unblock(task_t *task);

// Get current running task
task_t* task_current(void);

// Switch to next task (called by timer interrupt)
void scheduler_tick(void);

// Context switch function (implemented in assembly)
extern void context_switch(registers_t *old_regs, registers_t *new_regs);

// Get task by ID
task_t* task_get_by_id(uint32_t id);

// List all tasks (for debugging/shell)
void task_list(void);

// Kill a task by ID
bool task_kill(uint32_t id);

// Sleep for specified ticks
void task_sleep(uint32_t ticks);

#endif
