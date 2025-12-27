#include <kernel/task.h>
#include <kernel/tty.h>
#include <kernel/memory.h>
#include <kernel/pagings.h>
#include <string.h>
#include <stdio.h>

// Task management
static task_t tasks[MAX_TASKS];
static task_t *current_task = NULL;
static task_t *ready_queue_head = NULL;
static uint32_t next_task_id = 1;
static bool task_scheduler_enabled = false;

// Timer tick counter
static uint32_t system_ticks = 0;

// Time quantum for round-robin scheduling (in timer ticks)
#define TIME_QUANTUM 5

// Guard-paged kernel stacks for tasks (guard + stack pages).
#define TASK_STACK_PAGES ((TASK_KERNEL_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE)
#define TASK_STACK_SLOT_SIZE ((TASK_STACK_PAGES + 1) * PAGE_SIZE)
#define KERNEL_STACK_BASE (KERNEL_VIRT_BASE + USER_SPACE_START)
#define PROCESS_STACK_REGION_SIZE (2 * PAGE_SIZE * 128)
#define TASK_STACK_BASE (KERNEL_STACK_BASE + PROCESS_STACK_REGION_SIZE)
#define TASK_STACK_SLOTS MAX_TASKS

static uint8_t task_stack_bitmap[(TASK_STACK_SLOTS + 7) / 8];

static inline bool task_stack_slot_used(uint32_t idx) {
	return (task_stack_bitmap[idx / 8] & (1u << (idx % 8))) != 0;
}

static inline void task_stack_slot_set(uint32_t idx) {
	task_stack_bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

static inline void task_stack_slot_clear(uint32_t idx) {
	task_stack_bitmap[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static void enqueue_task(task_t *task);

static bool ticks_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static void wake_sleeping_tasks(uint32_t now) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t *task = &tasks[i];
        if (task->state != TASK_BLOCKED || !task->sleeping) {
            continue;
        }
        if (!ticks_reached(now, task->sleep_until)) {
            continue;
        }
        task->sleeping = false;
        task->sleep_until = 0;
        task->state = TASK_READY;
        task->time_slice = TIME_QUANTUM;
        enqueue_task(task);
    }
}

// Allocate a kernel stack
static uint32_t allocate_kernel_stack(void) {
    uint32_t *kernel_dir = page_kernel_directory();
    if (!kernel_dir) {
        return 0;
    }
    for (uint32_t i = 0; i < TASK_STACK_SLOTS; i++) {
        if (task_stack_slot_used(i)) {
            continue;
        }
        uint32_t slot_base = TASK_STACK_BASE + i * TASK_STACK_SLOT_SIZE;
        uint32_t stack_virt = slot_base + PAGE_SIZE;
        uint32_t mapped = 0;
        bool ok = true;

        for (uint32_t page = 0; page < TASK_STACK_PAGES; page++) {
            uint32_t phys = frame_alloc();
            if (!phys) {
                ok = false;
                break;
            }
            if (!page_map(kernel_dir, stack_virt + page * PAGE_SIZE, phys, PAGE_RW)) {
                frame_free(phys);
                ok = false;
                break;
            }
            mapped++;
        }

        if (!ok) {
            for (uint32_t page = 0; page < mapped; page++) {
                page_unmap(kernel_dir, stack_virt + page * PAGE_SIZE, true);
            }
            return 0;
        }

        page_unmap(kernel_dir, slot_base, false);
        task_stack_slot_set(i);
        return stack_virt + TASK_KERNEL_STACK_SIZE;
    }
    return 0;
}

// Free a kernel stack
static void free_kernel_stack(uint32_t stack_top) {
    if (stack_top == 0 || stack_top < TASK_STACK_BASE + PAGE_SIZE + TASK_KERNEL_STACK_SIZE) {
        return;
    }
    uint32_t stack_virt = stack_top - TASK_KERNEL_STACK_SIZE;
    uint32_t slot_base = stack_virt - PAGE_SIZE;
    uint32_t idx = (slot_base - TASK_STACK_BASE) / TASK_STACK_SLOT_SIZE;
    if (idx >= TASK_STACK_SLOTS) {
        return;
    }
    uint32_t *kernel_dir = page_kernel_directory();
    if (kernel_dir) {
        for (uint32_t page = 0; page < TASK_STACK_PAGES; page++) {
            page_unmap(kernel_dir, stack_virt + page * PAGE_SIZE, true);
        }
    }
    task_stack_slot_clear(idx);
}

// Add task to ready queue
static void enqueue_task(task_t *task) {
    task->next = NULL;
    if (!ready_queue_head) {
        ready_queue_head = task;
    } else {
        task_t *current = ready_queue_head;
        while (current->next) {
            current = current->next;
        }
        current->next = task;
    }
}

// Remove task from ready queue
static task_t* dequeue_task(void) {
    if (!ready_queue_head) {
        return NULL;
    }
    task_t *task = ready_queue_head;
    ready_queue_head = ready_queue_head->next;
    task->next = NULL;
    return task;
}

// Find a free task slot
static task_t* allocate_task(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED || tasks[i].id == 0) {
            memset(&tasks[i], 0, sizeof(task_t));
            return &tasks[i];
        }
    }
    return NULL;
}

// Initialize the kernel task scheduler
void task_scheduler_init(void) {
    // Initialize all tasks
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].id = 0;
        tasks[i].state = TASK_TERMINATED;
        tasks[i].next = NULL;
        tasks[i].sleep_until = 0;
        tasks[i].sleeping = false;
        task_stack_slot_clear((uint32_t)i);
    }
    
    current_task = NULL;
    ready_queue_head = NULL;
    next_task_id = 1;
    system_ticks = 0;
    
    // Create idle task (task ID 0 - runs when nothing else can run)
    // For now, we'll handle this implicitly
    
    task_scheduler_enabled = true;
}

// Create a new task
task_t* task_create(const char *name, void (*entry_point)(void), uint32_t priority) {
    if (!task_scheduler_enabled) {
        return NULL;
    }
    
    task_t *task = allocate_task();
    if (!task) {
        printf("Error: No free task slots\n");
        return NULL;
    }
    
    // Allocate kernel stack
    uint32_t stack_top = allocate_kernel_stack();
    if (!stack_top) {
        printf("Error: Failed to allocate kernel stack\n");
        return NULL;
    }
    
    // Initialize task
    task->id = next_task_id++;
    strncpy(task->name, name, 31);
    task->name[31] = '\0';
    task->state = TASK_READY;
    task->priority = priority;
    task->time_slice = TIME_QUANTUM;
    task->total_time = 0;
    task->kernel_stack = stack_top;
    task->page_directory = NULL; // For now, all tasks share kernel space
    task->next = NULL;
    
    // Initialize registers for new task
    memset(&task->regs, 0, sizeof(registers_t));
    task->regs.eip = (uint32_t)entry_point;
    task->regs.esp = stack_top - 16; // Leave some space for safety
    task->regs.ebp = task->regs.esp;
    task->regs.eflags = 0x202; // Enable interrupts (IF flag)
    task->regs.cs = 0x08;      // Kernel code segment
    task->regs.ds = 0x10;      // Kernel data segment
    task->regs.es = 0x10;
    task->regs.fs = 0x10;
    task->regs.gs = 0x10;
    task->regs.ss = 0x10;
    
    // Add to ready queue
    enqueue_task(task);
    
    printf("KThread %u '%s' created (priority %u)\n", task->id, task->name, task->priority);
    
    return task;
}

// Get current task
task_t* task_current(void) {
    return current_task;
}

// Get task by ID
task_t* task_get_by_id(uint32_t id) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].id == id && tasks[i].state != TASK_TERMINATED) {
            return &tasks[i];
        }
    }
    return NULL;
}

// Terminate the current task
void task_exit(void) {
    if (!current_task) {
        return;
    }
    
    printf("KThread %u '%s' terminated\n", current_task->id, current_task->name);
    
    current_task->state = TASK_TERMINATED;
    current_task->sleeping = false;
    current_task->sleep_until = 0;
    free_kernel_stack(current_task->kernel_stack);
    
    // Force a context switch
    task_yield();
}

// Yield CPU to another task
void task_yield(void) {
    if (!task_scheduler_enabled) {
        return;
    }

    if (!current_task) {
        task_t *next_task = dequeue_task();
        if (!next_task) {
            return;
        }
        current_task = next_task;
        current_task->state = TASK_RUNNING;
        context_switch(NULL, &current_task->regs);
        return;
    }
    
    // If current task is still runnable, add it back to ready queue
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        current_task->time_slice = TIME_QUANTUM;
        enqueue_task(current_task);
    }
    
    // Get next task from ready queue
    task_t *next_task = dequeue_task();
    if (!next_task) {
        // No tasks to run - idle
        current_task = NULL;
        return;
    }
    
    task_t *old_task = current_task;
    current_task = next_task;
    current_task->state = TASK_RUNNING;
    
    if (old_task && old_task->state != TASK_TERMINATED) {
        // Perform context switch
        context_switch(&old_task->regs, &current_task->regs);
    } else {
        // First task or old task terminated - just load new context
        // In a real implementation, we'd use a different method here
        context_switch(NULL, &current_task->regs);
    }
}

// Block current task
void task_block(void) {
    if (!current_task) {
        return;
    }
    
    current_task->state = TASK_BLOCKED;
    task_yield();
}

// Unblock a specific task
void task_unblock(task_t *task) {
    if (!task || task->state != TASK_BLOCKED) {
        return;
    }
    
    task->state = TASK_READY;
    task->sleeping = false;
    task->sleep_until = 0;
    task->time_slice = TIME_QUANTUM;
    enqueue_task(task);
}

// Sleep for specified ticks
void task_sleep(uint32_t ticks) {
    if (!current_task) {
        return;
    }

    if (ticks == 0) {
        task_yield();
        return;
    }

    if (!ready_queue_head) {
        uint32_t wake = system_ticks + ticks;
        while (!ticks_reached(system_ticks, wake)) {
            __asm__ volatile ("hlt");
        }
        return;
    }

    current_task->sleeping = true;
    current_task->sleep_until = system_ticks + ticks;
    current_task->state = TASK_BLOCKED;
    task_yield();
}

// Scheduler tick (called by timer interrupt)
void task_scheduler_tick(void) {
    system_ticks++;
    
    if (task_scheduler_enabled) {
        wake_sleeping_tasks(system_ticks);
    }

    if (!task_scheduler_enabled || !current_task) {
        return;
    }
    
    // Update current task's time
    current_task->total_time++;
    
    // Decrement time slice
    if (current_task->time_slice > 0) {
        current_task->time_slice--;
    }
    
    // If time slice expired, trigger context switch
    if (current_task->time_slice == 0) {
        task_yield();
    }
}

// List all tasks
void task_list(void) {
    printf("TID\tState\t\tPriority  Time\tName\n");
    printf("---\t--------\t--------  ----\t--------------------------------\n");
    
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].id != 0 && tasks[i].state != TASK_TERMINATED) {
            const char *state_str;
            switch (tasks[i].state) {
                case TASK_READY:    state_str = "READY"; break;
                case TASK_RUNNING:  state_str = "RUNNING"; break;
                case TASK_BLOCKED:  state_str = "BLOCKED"; break;
                default:            state_str = "UNKNOWN"; break;
            }
            
            printf("%u\t%s\t\t%u\t  %u\t%s\n", 
                   tasks[i].id, state_str, tasks[i].priority, 
                   tasks[i].total_time, tasks[i].name);
        }
    }
}

// Kill a task by ID
bool task_kill(uint32_t id) {
    task_t *task = task_get_by_id(id);
    if (!task) {
        return false;
    }
    
    if (task == current_task) {
        task_exit();
    } else {
        task->state = TASK_TERMINATED;
        task->sleeping = false;
        task->sleep_until = 0;
        free_kernel_stack(task->kernel_stack);
        printf("KThread %u '%s' killed\n", task->id, task->name);
    }
    
    return true;
}
