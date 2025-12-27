#include <kernel/process.h>
#include <kernel/cpu.h>
#include <kernel/elf.h>
#include <kernel/fs.h>
#include <kernel/gdt.h>
#include <kernel/kmalloc.h>
#include <kernel/memory.h>
#include <kernel/pagings.h>
#include <kernel/kpti.h>
#include <kernel/user_programs.h>
#include <string.h>

static process_t *current_process = NULL;
static process_t *ready_heads[PROCESS_PRIORITY_LEVELS];
static process_t *ready_tails[PROCESS_PRIORITY_LEVELS];
static process_t *all_head = NULL;
static uint32_t next_pid = 1;
static char default_cwd[USERMODE_MAX_PATH] = "/";
static bool scheduler_active = false;

// Pipe support (simple blocking pipes for user processes).
#define PIPE_BUFFER_SIZE 1024
#define PIPE_WAIT_NONE 0
#define PIPE_WAIT_READ 1
#define PIPE_WAIT_WRITE 2

struct pipe {
	uint8_t buffer[PIPE_BUFFER_SIZE];
	uint32_t read_pos;
	uint32_t write_pos;
	uint32_t count;
	uint32_t readers;
	uint32_t writers;
};

// Kernel stack allocator with guard pages.
#define KERNEL_STACK_BASE (KERNEL_VIRT_BASE + USER_SPACE_START)
#define KERNEL_STACK_SLOT_SIZE (2 * PAGE_SIZE)
#define KERNEL_STACK_SLOTS 128

static uint8_t kernel_stack_bitmap[(KERNEL_STACK_SLOTS + 7) / 8];
static void *kernel_stack_deferred[8];
static uint32_t kernel_stack_deferred_count = 0;

static void pipe_wake_readers(pipe_t *pipe);
static void pipe_wake_writers(pipe_t *pipe);
static void pipe_wait_clear(process_t *proc);

static inline bool kernel_stack_slot_used(uint32_t idx) {
	return (kernel_stack_bitmap[idx / 8] & (1u << (idx % 8))) != 0;
}

static inline void kernel_stack_slot_set(uint32_t idx) {
	kernel_stack_bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

static inline void kernel_stack_slot_clear(uint32_t idx) {
	kernel_stack_bitmap[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static bool kernel_stack_is_current(void *base) {
	uint32_t esp = 0;
	__asm__ volatile ("movl %%esp, %0" : "=r"(esp));
	uint32_t start = (uint32_t)base;
	return esp >= start && esp < (start + PAGE_SIZE);
}

static bool kernel_stack_deferred_has(void *base) {
	for (uint32_t i = 0; i < kernel_stack_deferred_count; i++) {
		if (kernel_stack_deferred[i] == base) {
			return true;
		}
	}
	return false;
}

static uint32_t align_up_page(uint32_t value) {
	return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static void process_init_fds(process_t *proc) {
	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		proc->fds[i].used = false;
		proc->fds[i].type = PROCESS_FD_NONE;
		proc->fds[i].path[0] = '\0';
		proc->fds[i].offset = 0;
		proc->fds[i].pipe = NULL;
	}
	for (int i = 0; i < 3 && i < PROCESS_MAX_FDS; i++) {
		proc->fds[i].used = true;
		proc->fds[i].type = PROCESS_FD_TTY;
	}
}

static bool kernel_stack_alloc(void **out_base, uint32_t *out_top) {
	if (!out_base || !out_top) {
		return false;
	}
	uint32_t *kernel_dir = page_kernel_directory();
	if (!kernel_dir) {
		return false;
	}
	for (uint32_t i = 0; i < KERNEL_STACK_SLOTS; i++) {
		if (kernel_stack_slot_used(i)) {
			continue;
		}
		uint32_t slot_base = KERNEL_STACK_BASE + i * KERNEL_STACK_SLOT_SIZE;
		uint32_t stack_virt = slot_base + PAGE_SIZE;
		uint32_t phys = frame_alloc();
		if (!phys) {
			return false;
		}
		if (!page_map(kernel_dir, stack_virt, phys, PAGE_RW)) {
			frame_free(phys);
			return false;
		}
		page_unmap(kernel_dir, slot_base, false);
		kernel_stack_slot_set(i);
		*out_base = (void *)stack_virt;
		*out_top = stack_virt + PAGE_SIZE;
		return true;
	}
	return false;
}

static void kernel_stack_free_now(void *base) {
	if (!base) {
		return;
	}
	uint32_t stack_virt = (uint32_t)base;
	if (stack_virt < KERNEL_STACK_BASE + PAGE_SIZE) {
		return;
	}
	uint32_t slot_base = stack_virt - PAGE_SIZE;
	uint32_t idx = (slot_base - KERNEL_STACK_BASE) / KERNEL_STACK_SLOT_SIZE;
	if (idx >= KERNEL_STACK_SLOTS) {
		return;
	}
	uint32_t *kernel_dir = page_kernel_directory();
	if (kernel_dir) {
		page_unmap(kernel_dir, stack_virt, true);
	}
	kernel_stack_slot_clear(idx);
}

static void kernel_stack_free(void *base) {
	if (!base) {
		return;
	}
	if (kernel_stack_is_current(base)) {
		if (!kernel_stack_deferred_has(base) &&
		    kernel_stack_deferred_count < (sizeof(kernel_stack_deferred) / sizeof(kernel_stack_deferred[0]))) {
			kernel_stack_deferred[kernel_stack_deferred_count++] = base;
		}
		return;
	}
	kernel_stack_free_now(base);
}

static void kernel_stack_flush_deferred(void) {
	if (kernel_stack_deferred_count == 0) {
		return;
	}
	uint32_t esp = 0;
	__asm__ volatile ("movl %%esp, %0" : "=r"(esp));
	uint32_t out = 0;
	for (uint32_t i = 0; i < kernel_stack_deferred_count; i++) {
		void *base = kernel_stack_deferred[i];
		uint32_t start = (uint32_t)base;
		if (esp >= start && esp < (start + PAGE_SIZE)) {
			kernel_stack_deferred[out++] = base;
			continue;
		}
		kernel_stack_free_now(base);
	}
	kernel_stack_deferred_count = out;
}

static void process_all_add(process_t *proc) {
	proc->all_next = all_head;
	all_head = proc;
}

static void process_ready_remove(process_t *proc) {
	if (!proc) {
		return;
	}
	for (uint8_t i = 0; i < PROCESS_PRIORITY_LEVELS; i++) {
		process_t *prev = NULL;
		process_t *cur = ready_heads[i];
		while (cur) {
			if (cur == proc) {
				if (prev) {
					prev->next = cur->next;
				} else {
					ready_heads[i] = cur->next;
				}
				if (ready_tails[i] == cur) {
					ready_tails[i] = prev;
				}
				cur->next = NULL;
				return;
			}
			prev = cur;
			cur = cur->next;
		}
	}
}

static void process_all_remove(process_t *proc) {
	process_t **cursor = &all_head;
	while (*cursor) {
		if (*cursor == proc) {
			*cursor = proc->all_next;
			proc->all_next = NULL;
			return;
		}
		cursor = &(*cursor)->all_next;
	}
}

static uint8_t process_clamp_priority(uint8_t priority) {
	if (priority >= PROCESS_PRIORITY_LEVELS) {
		return PROCESS_PRIORITY_DEFAULT;
	}
	return priority;
}

static bool process_ready_any(void) {
	for (uint8_t i = 0; i < PROCESS_PRIORITY_LEVELS; i++) {
		if (ready_heads[i]) {
			return true;
		}
	}
	return false;
}

static int process_ready_highest_priority(void) {
	for (uint8_t i = 0; i < PROCESS_PRIORITY_LEVELS; i++) {
		if (ready_heads[i]) {
			return (int)i;
		}
	}
	return -1;
}

static void process_ready_enqueue(process_t *proc) {
	if (!proc) {
		return;
	}
	uint8_t priority = process_clamp_priority(proc->priority);
	proc->priority = priority;
	proc->time_slice = PROCESS_TIME_QUANTUM;
	proc->reschedule = false;
	proc->next = NULL;
	if (!ready_tails[priority]) {
		ready_heads[priority] = proc;
		ready_tails[priority] = proc;
	} else {
		ready_tails[priority]->next = proc;
		ready_tails[priority] = proc;
	}
}

static process_t *process_ready_dequeue(void) {
	for (uint8_t i = 0; i < PROCESS_PRIORITY_LEVELS; i++) {
		process_t *proc = ready_heads[i];
		if (!proc) {
			continue;
		}
		ready_heads[i] = proc->next;
		if (!ready_heads[i]) {
			ready_tails[i] = NULL;
		}
		proc->next = NULL;
		return proc;
	}
	return NULL;
}

static process_t *process_find(uint32_t pid) {
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->pid == pid) {
			return proc;
		}
	}
	return NULL;
}

static process_t *process_find_zombie(uint32_t pid) {
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->pid == pid && proc->state == PROCESS_ZOMBIE) {
			return proc;
		}
	}
	return NULL;
}

static process_t *process_find_any_zombie(void) {
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->state == PROCESS_ZOMBIE) {
			return proc;
		}
	}
	return NULL;
}

static bool process_user_ptr_ok(process_t *proc, uint32_t addr, uint32_t size) {
	if (size == 0) {
		return true;
	}
	if (addr < USER_SPACE_START) {
		return false;
	}
	uint32_t end = addr + size;
	if (end < addr || end > USER_SPACE_END) {
		return false;
	}
	if (!proc || !proc->page_directory) {
		return false;
	}
	if (!page_user_range_mapped(proc->page_directory, addr, size)) {
		return false;
	}
	return true;
}

static void process_close_all_fds(process_t *proc) {
	if (!proc) {
		return;
	}
	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		process_fd_close(proc, i);
	}
}

static void process_sanitize_fds(process_t *proc) {
	if (!proc) {
		return;
	}
	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		process_fd_t *fd = &proc->fds[i];
		if (!fd->used) {
			fd->type = PROCESS_FD_NONE;
			fd->path[0] = '\0';
			fd->offset = 0;
			fd->pipe = NULL;
			continue;
		}
		if (fd->type == PROCESS_FD_NONE) {
			fd->used = false;
			fd->path[0] = '\0';
			fd->offset = 0;
			fd->pipe = NULL;
			continue;
		}
		if ((fd->type == PROCESS_FD_PIPE_READ || fd->type == PROCESS_FD_PIPE_WRITE) &&
		    !fd->pipe) {
			fd->used = false;
			fd->type = PROCESS_FD_NONE;
			fd->path[0] = '\0';
			fd->offset = 0;
			fd->pipe = NULL;
		}
	}
}

static void process_write_status(process_t *proc, int status) {
	if (!proc || proc->wait_status_ptr == 0) {
		return;
	}
	if (!process_user_ptr_ok(proc, proc->wait_status_ptr, sizeof(int))) {
		return;
	}
	page_copy_to_user(proc->page_directory, proc->wait_status_ptr,
	                  &status, sizeof(status));
}

static void process_wake_waiters(process_t *exiting, int exit_code, bool *had_waiter) {
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->state != PROCESS_BLOCKED || !proc->waiting) {
			continue;
		}
		if (proc->wait_pid >= 0 && (uint32_t)proc->wait_pid != exiting->pid) {
			continue;
		}
		proc->waiting = false;
		proc->wait_pid = 0;
		process_write_status(proc, exit_code);
		proc->wait_status_ptr = 0;
		proc->frame.eax = exiting->pid;
		proc->state = PROCESS_READY;
		process_ready_enqueue(proc);
		if (had_waiter) {
			*had_waiter = true;
		}
	}
}

void process_init(void) {
	current_process = NULL;
	memset(kernel_stack_bitmap, 0, sizeof(kernel_stack_bitmap));
	for (uint8_t i = 0; i < PROCESS_PRIORITY_LEVELS; i++) {
		ready_heads[i] = NULL;
		ready_tails[i] = NULL;
	}
	all_head = NULL;
	next_pid = 1;
	default_cwd[0] = '/';
	default_cwd[1] = '\0';
	scheduler_active = false;
}

process_t *process_create(const char *name) {
	process_t *proc = kmalloc(sizeof(process_t));
	if (!proc) {
		return NULL;
	}
	memset(proc, 0, sizeof(process_t));

	proc->pid = next_pid++;
	if (name && *name) {
		strncpy(proc->name, name, sizeof(proc->name) - 1);
	} else {
		strncpy(proc->name, "user", sizeof(proc->name) - 1);
	}
	proc->name[sizeof(proc->name) - 1] = '\0';
	proc->state = PROCESS_READY;
	proc->priority = PROCESS_PRIORITY_DEFAULT;
	proc->time_slice = PROCESS_TIME_QUANTUM;
	proc->total_time = 0;
	proc->reschedule = false;

	strncpy(proc->cwd, default_cwd, sizeof(proc->cwd) - 1);
	proc->cwd[sizeof(proc->cwd) - 1] = '\0';

	proc->page_directory = NULL;
	proc->entry = 0;
	proc->user_stack_top = USER_STACK_TOP;
	proc->heap_base = 0;
	proc->heap_end = 0;
	proc->kernel_stack_base = NULL;
	proc->kernel_stack_top = 0;
	proc->uid = PROCESS_DEFAULT_UID;
	proc->gid = PROCESS_DEFAULT_GID;
	if (!kernel_stack_alloc(&proc->kernel_stack_base, &proc->kernel_stack_top)) {
		process_destroy(proc);
		return NULL;
	}

	process_init_fds(proc);
	proc->pipe_wait = NULL;
	proc->pipe_wait_op = PIPE_WAIT_NONE;
	proc->pipe_wait_buf = 0;
	proc->pipe_wait_len = 0;
	proc->pipe_wait_done = 0;

	process_all_add(proc);
	return proc;
}

void process_destroy(process_t *proc) {
	if (!proc) {
		return;
	}
	if (current_process == proc) {
		current_process = NULL;
	}
	pipe_wait_clear(proc);
	process_close_all_fds(proc);
	if (proc->page_directory) {
		page_directory_destroy(proc->page_directory);
		proc->page_directory = NULL;
	}
	if (proc->kernel_stack_base) {
		kernel_stack_free(proc->kernel_stack_base);
		proc->kernel_stack_base = NULL;
		proc->kernel_stack_top = 0;
	}
	process_all_remove(proc);
	kfree(proc);
}

void process_activate(process_t *proc) {
	if (!proc) {
		return;
	}
	if (proc->kernel_stack_top) {
		tss_set_kernel_stack(proc->kernel_stack_top);
	}
}

void process_activate_user(process_t *proc) {
	if (!proc || !proc->page_directory) {
		return;
	}
	write_cr3(virt_to_phys(proc->page_directory));
	if (proc->kernel_stack_top) {
		tss_set_kernel_stack(proc->kernel_stack_top);
	}
}

void process_activate_kernel(void) {
	uint32_t *kernel_dir = page_kernel_directory();
	if (kernel_dir) {
		write_cr3(virt_to_phys(kernel_dir));
	}
}

process_t *process_current(void) {
	return current_process;
}

void process_set_current(process_t *proc) {
	current_process = proc;
}

uint32_t process_get_count(void) {
	uint32_t count = 0;
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		count++;
	}
	return count;
}

uint32_t process_list(process_info_t *out, uint32_t max) {
	if (!out || max == 0) {
		return 0;
	}
	uint32_t count = 0;
	for (process_t *proc = all_head; proc && count < max; proc = proc->all_next) {
		process_info_t *info = &out[count];
		memset(info, 0, sizeof(*info));
		info->pid = proc->pid;
		info->state = (uint8_t)proc->state;
		info->priority = proc->priority;
		info->time_slice = proc->time_slice;
		info->total_time = proc->total_time;
		strncpy(info->name, proc->name, PROCESS_NAME_MAX - 1);
		info->name[PROCESS_NAME_MAX - 1] = '\0';
		count++;
	}
	return count;
}

void process_set_default_cwd(const char *path) {
	if (!path || path[0] == '\0') {
		return;
	}
	strncpy(default_cwd, path, sizeof(default_cwd) - 1);
	default_cwd[sizeof(default_cwd) - 1] = '\0';
}

const char *process_default_cwd(void) {
	return default_cwd;
}

void process_set_cwd(process_t *proc, const char *path) {
	if (!proc || !path || path[0] == '\0') {
		return;
	}
	strncpy(proc->cwd, path, sizeof(proc->cwd) - 1);
	proc->cwd[sizeof(proc->cwd) - 1] = '\0';
}

const char *process_get_cwd(process_t *proc) {
	if (!proc) {
		return default_cwd;
	}
	return proc->cwd;
}

void process_set_args(process_t *proc, const char *args, uint32_t len) {
	if (!proc) {
		return;
	}
	if (!args || len == 0) {
		proc->args[0] = '\0';
		proc->args_len = 0;
		return;
	}
	if (len >= USERMODE_MAX_ARGS) {
		len = USERMODE_MAX_ARGS - 1;
	}
	memcpy(proc->args, args, len);
	proc->args[len] = '\0';
	proc->args_len = len;
}

uint32_t process_get_args(process_t *proc, char *dst, uint32_t max_len) {
	if (!proc) {
		return 0;
	}
	uint32_t total = proc->args_len;
	if (!dst || max_len == 0) {
		return total;
	}
	uint32_t to_copy = total;
	if (to_copy > max_len) {
		to_copy = max_len;
	}
	memcpy(dst, proc->args, to_copy);
	return total;
}

static bool process_block_and_switch(trap_frame_t *frame, process_t *current) {
	if (!frame || !current) {
		return true;
	}
	current->state = PROCESS_BLOCKED;
	memcpy(&current->frame, frame, sizeof(*frame));

	while (!process_ready_any()) {
		cpu_hlt();
	}

	process_t *next = process_ready_dequeue();
	if (!next) {
		current->state = PROCESS_RUNNING;
		return true;
	}

	current_process = next;
	next->state = PROCESS_RUNNING;
	next->reschedule = false;
	if (next->time_slice == 0) {
		next->time_slice = PROCESS_TIME_QUANTUM;
	}
	process_activate(next);
	kernel_stack_flush_deferred();
	uint32_t kernel_esp = frame->esp;
	memcpy(frame, &next->frame, sizeof(*frame));
	frame->esp = kernel_esp;
	return false;
}

bool process_brk(process_t *proc, uint32_t new_end, uint32_t *out_end) {
	if (!proc || !proc->page_directory) {
		return false;
	}
	if (proc->heap_base == 0) {
		return false;
	}

	uint32_t limit = USER_STACK_TOP - USER_STACK_SIZE;
	uint32_t current = proc->heap_end;

	if (new_end == 0) {
		if (out_end) {
			*out_end = current;
		}
		return true;
	}

	if (new_end < proc->heap_base || new_end > limit) {
		return false;
	}

	if (new_end > current) {
		uint32_t map_start = align_up_page(current);
		uint32_t map_end = align_up_page(new_end);
		for (uint32_t addr = map_start; addr < map_end; addr += PAGE_SIZE) {
			if (!page_map_alloc(proc->page_directory, addr, PAGE_RW | PAGE_USER, NULL)) {
				for (uint32_t undo = map_start; undo < addr; undo += PAGE_SIZE) {
					page_unmap(proc->page_directory, undo, true);
				}
				return false;
			}
			page_memset_user(proc->page_directory, addr, 0, PAGE_SIZE);
		}
	} else if (new_end < current) {
		uint32_t unmap_start = align_up_page(new_end);
		uint32_t unmap_end = align_up_page(current);
		for (uint32_t addr = unmap_start; addr < unmap_end; addr += PAGE_SIZE) {
			page_unmap(proc->page_directory, addr, true);
		}
	}

	proc->heap_end = new_end;
	if (out_end) {
		*out_end = new_end;
	}
	return true;
}

static void process_setup_frame(process_t *proc) {
	memset(&proc->frame, 0, sizeof(proc->frame));
	proc->frame.eip = proc->entry;
	proc->frame.cs = GDT_USER_CODE;
	proc->frame.eflags = 0x202;
	proc->frame.useresp = proc->user_stack_top;
	proc->frame.userss = GDT_USER_DATA;
	proc->frame.ds = GDT_USER_DATA;
	proc->frame.es = GDT_USER_DATA;
	proc->frame.fs = GDT_USER_DATA;
	proc->frame.gs = GDT_USER_DATA;
}

static bool process_clone_cow(uint32_t *parent_dir, uint32_t *child_dir, bool *out_modified) {
	if (!parent_dir || !child_dir) {
		return false;
	}
	bool modified = false;
	uint32_t start_pde = USER_SPACE_START >> 22;
	uint32_t end_pde = USER_SPACE_END >> 22;

	for (uint32_t i = start_pde; i < end_pde; i++) {
		uint32_t pde = parent_dir[i];
		if ((pde & PAGE_PRESENT) == 0) {
			continue;
		}
		uint32_t *parent_table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
		uint32_t table_phys = frame_alloc();
		if (!table_phys) {
			return false;
		}
		uint32_t *child_table = (uint32_t *)phys_to_virt(table_phys);
		memset(child_table, 0, PAGE_SIZE);
		child_dir[i] = table_phys | (pde & 0xFFF);

		for (uint32_t j = 0; j < 1024; j++) {
			uint32_t pte = parent_table[j];
			if ((pte & PAGE_PRESENT) == 0) {
				continue;
			}
			uint32_t phys = pte & ~0xFFF;
			uint32_t flags = pte & 0xFFF;
			if (flags & PAGE_USER) {
				if (flags & PAGE_RW) {
					uint32_t cow_flags = (flags & ~PAGE_RW) | PAGE_COW;
					parent_table[j] = phys | cow_flags;
					child_table[j] = phys | cow_flags;
					frame_ref_inc(phys);
					modified = true;
				} else {
					child_table[j] = pte;
					frame_ref_inc(phys);
				}
			} else {
				child_table[j] = pte;
			}
		}
	}

	if (out_modified) {
		*out_modified = modified;
	}
	return true;
}

bool process_exec(process_t *proc, const char *path, const char *args, uint32_t args_len) {
	if (!proc || !path || path[0] == '\0') {
		return false;
	}

	fs_inode_t inode;
	if (!fs_stat(path, &inode)) {
		if (user_program_install_if_embedded(path)) {
			fs_stat(path, &inode);
		}
	}

	uint32_t *new_dir = page_directory_create();
	if (!new_dir) {
		return false;
	}

	elf_image_t image;
	if (!elf_load_file(path, &image, new_dir)) {
		page_directory_destroy(new_dir);
		return false;
	}

	uint32_t guard_base = USER_STACK_TOP - USER_STACK_SIZE;
	uint32_t stack_bottom = guard_base + PAGE_SIZE;
	if (image.max_vaddr >= stack_bottom) {
		page_directory_destroy(new_dir);
		return false;
	}

	for (uint32_t addr = stack_bottom; addr < USER_STACK_TOP; addr += PAGE_SIZE) {
		if (!page_map_alloc(new_dir, addr, PAGE_RW | PAGE_USER, NULL)) {
			page_directory_destroy(new_dir);
			return false;
		}
	}
	uint32_t stack_bytes = USER_STACK_TOP - stack_bottom;
	if (!page_memset_user(new_dir, stack_bottom, 0, stack_bytes)) {
		page_directory_destroy(new_dir);
		return false;
	}

	uint32_t heap_base = align_up_page(image.max_vaddr);
	if (heap_base < ELF_USER_LOAD_MIN) {
		heap_base = ELF_USER_LOAD_MIN;
	}
	if (heap_base > guard_base) {
		page_directory_destroy(new_dir);
		return false;
	}

	kpti_map_kernel_pages(new_dir, proc);

	if (proc->page_directory) {
		page_directory_destroy(proc->page_directory);
	}

	proc->page_directory = new_dir;
	proc->entry = image.entry;
	proc->user_stack_top = USER_STACK_TOP;
	proc->heap_base = heap_base;
	proc->heap_end = heap_base;
	proc->pipe_wait = NULL;
	proc->pipe_wait_op = PIPE_WAIT_NONE;
	proc->pipe_wait_buf = 0;
	proc->pipe_wait_len = 0;
	proc->pipe_wait_done = 0;
	proc->waiting = false;
	proc->wait_pid = 0;
	proc->wait_status_ptr = 0;
	proc->sleeping = false;
	proc->sleep_until = 0;
	process_sanitize_fds(proc);
	process_set_args(proc, args, args_len);
	process_setup_frame(proc);
	return true;
}

int process_spawn(const char *path, const char *args, uint32_t args_len) {
	process_t *proc = process_create(path);
	if (!proc) {
		return -1;
	}
	if (!process_exec(proc, path, args, args_len)) {
		process_destroy(proc);
		return -1;
	}
	proc->state = PROCESS_READY;
	process_ready_enqueue(proc);
	return (int)proc->pid;
}

int process_fork(trap_frame_t *frame) {
	process_t *parent = current_process;
	if (!parent || !frame || !parent->page_directory) {
		return -1;
	}

	process_t *child = process_create(parent->name);
	if (!child) {
		return -1;
	}
	child->priority = parent->priority;
	strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);
	child->cwd[sizeof(child->cwd) - 1] = '\0';
	process_set_args(child, parent->args, parent->args_len);
	memcpy(child->fds, parent->fds, sizeof(child->fds));
	process_sanitize_fds(child);
	for (int i = 0; i < PROCESS_MAX_FDS; i++) {
		if (!child->fds[i].used) {
			continue;
		}
		if (child->fds[i].type == PROCESS_FD_PIPE_READ) {
			pipe_retain_read(child->fds[i].pipe);
		} else if (child->fds[i].type == PROCESS_FD_PIPE_WRITE) {
			pipe_retain_write(child->fds[i].pipe);
		}
	}
	child->entry = parent->entry;
	child->user_stack_top = parent->user_stack_top;
	child->heap_base = parent->heap_base;
	child->heap_end = parent->heap_end;
	child->uid = parent->uid;
	child->gid = parent->gid;
	child->pipe_wait = NULL;
	child->pipe_wait_op = PIPE_WAIT_NONE;
	child->pipe_wait_buf = 0;
	child->pipe_wait_len = 0;
	child->pipe_wait_done = 0;

	uint32_t *child_dir = page_directory_create();
	if (!child_dir) {
		process_destroy(child);
		return -1;
	}
	child->page_directory = child_dir;

	bool modified = false;
	if (!process_clone_cow(parent->page_directory, child_dir, &modified)) {
		process_destroy(child);
		return -1;
	}
	kpti_map_kernel_pages(child_dir, child);

	memcpy(&child->frame, frame, sizeof(*frame));
	child->frame.eax = 0;
	child->state = PROCESS_READY;
	process_ready_enqueue(child);
	return (int)child->pid;
}

process_t *process_next_ready(void) {
	return process_ready_dequeue();
}

bool process_schedule(trap_frame_t *frame) {
	if (!scheduler_active || !frame) {
		return false;
	}
	if ((frame->cs & 0x3) != 0x3) {
		return false;
	}
	process_t *current = current_process;
	if (!current) {
		return false;
	}
	int ready_prio = process_ready_highest_priority();
	if (ready_prio < 0) {
		memcpy(&current->frame, frame, sizeof(*frame));
		if (current->time_slice == 0) {
			current->time_slice = PROCESS_TIME_QUANTUM;
			current->reschedule = false;
		}
		return false;
	}

	bool should_preempt = current->reschedule || current->time_slice == 0;
	if (ready_prio < current->priority) {
		should_preempt = true;
	}
	if (!should_preempt) {
		memcpy(&current->frame, frame, sizeof(*frame));
		return false;
	}

	memcpy(&current->frame, frame, sizeof(*frame));
	if (current->state == PROCESS_RUNNING) {
		current->state = PROCESS_READY;
		process_ready_enqueue(current);
	}

	process_t *next = process_ready_dequeue();
	if (!next) {
		current->state = PROCESS_RUNNING;
		current->reschedule = false;
		if (current->time_slice == 0) {
			current->time_slice = PROCESS_TIME_QUANTUM;
		}
		return false;
	}

	current_process = next;
	next->state = PROCESS_RUNNING;
	next->reschedule = false;
	if (next->time_slice == 0) {
		next->time_slice = PROCESS_TIME_QUANTUM;
	}
	process_activate(next);
	kernel_stack_flush_deferred();
	uint32_t kernel_esp = frame->esp;
	memcpy(frame, &next->frame, sizeof(*frame));
	frame->esp = kernel_esp;
	return true;
}

bool process_exit_current(trap_frame_t *frame, int code) {
	process_t *current = current_process;
	if (!current) {
		return false;
	}
	current->exit_code = code;
	bool had_waiter = false;
	process_wake_waiters(current, code, &had_waiter);
	pipe_wait_clear(current);
	process_close_all_fds(current);

	current->state = PROCESS_ZOMBIE;
	if (current->page_directory) {
		process_activate_kernel();
		page_directory_destroy(current->page_directory);
		current->page_directory = NULL;
	}

	current_process = NULL;
	process_t *next = process_ready_dequeue();
	if (!next) {
		process_scheduler_stop();
		return false;
	}

	current_process = next;
	next->state = PROCESS_RUNNING;
	next->reschedule = false;
	if (next->time_slice == 0) {
		next->time_slice = PROCESS_TIME_QUANTUM;
	}
	process_activate(next);
	kernel_stack_flush_deferred();
	uint32_t kernel_esp = frame->esp;
	memcpy(frame, &next->frame, sizeof(*frame));
	frame->esp = kernel_esp;
	return true;
}

bool process_wait(trap_frame_t *frame, int32_t pid, uint32_t status_ptr,
                  int *out_pid, int *out_status) {
	if (!frame || !out_pid || !out_status) {
		return true;
	}
	process_t *current = current_process;
	if (!current) {
		*out_pid = -1;
		*out_status = -1;
		return true;
	}

	process_t *zombie = NULL;
	if (pid < 0) {
		zombie = process_find_any_zombie();
	} else {
		zombie = process_find_zombie((uint32_t)pid);
	}

	if (zombie) {
		*out_pid = zombie->pid;
		*out_status = zombie->exit_code;
		process_destroy(zombie);
		return true;
	}

	if (pid >= 0 && !process_find((uint32_t)pid)) {
		*out_pid = -1;
		*out_status = -1;
		return true;
	}

	current->waiting = true;
	current->wait_pid = pid;
	current->wait_status_ptr = status_ptr;
	current->state = PROCESS_BLOCKED;
	memcpy(&current->frame, frame, sizeof(*frame));

	while (!process_ready_any()) {
		cpu_hlt();
	}

	process_t *next = process_ready_dequeue();
	if (!next) {
		current->waiting = false;
		current->state = PROCESS_RUNNING;
		*out_pid = -1;
		*out_status = -1;
		return true;
	}

	current_process = next;
	next->state = PROCESS_RUNNING;
	next->reschedule = false;
	if (next->time_slice == 0) {
		next->time_slice = PROCESS_TIME_QUANTUM;
	}
	process_activate(next);
	kernel_stack_flush_deferred();
	uint32_t kernel_esp = frame->esp;
	memcpy(frame, &next->frame, sizeof(*frame));
	frame->esp = kernel_esp;
	return false;
}

bool process_sleep_until(trap_frame_t *frame, uint32_t wake_tick) {
	if (!frame) {
		return true;
	}
	process_t *current = current_process;
	if (!current) {
		return true;
	}

	if (!process_ready_any()) {
		return true;
	}

	current->sleeping = true;
	current->sleep_until = wake_tick;
	current->state = PROCESS_BLOCKED;
	memcpy(&current->frame, frame, sizeof(*frame));

	process_t *next = process_ready_dequeue();
	if (!next) {
		current->sleeping = false;
		current->state = PROCESS_RUNNING;
		return true;
	}

	current_process = next;
	next->state = PROCESS_RUNNING;
	next->reschedule = false;
	if (next->time_slice == 0) {
		next->time_slice = PROCESS_TIME_QUANTUM;
	}
	process_activate(next);
	kernel_stack_flush_deferred();
	uint32_t kernel_esp = frame->esp;
	memcpy(frame, &next->frame, sizeof(*frame));
	frame->esp = kernel_esp;
	return false;
}

void process_tick(uint32_t now_ticks) {
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->state != PROCESS_BLOCKED || !proc->sleeping) {
			continue;
		}
		if (now_ticks < proc->sleep_until) {
			continue;
		}
		proc->sleeping = false;
		proc->sleep_until = 0;
		proc->frame.eax = 0;
		proc->state = PROCESS_READY;
		process_ready_enqueue(proc);
	}

	if (!scheduler_active) {
		return;
	}
	process_t *current = current_process;
	if (!current || current->state != PROCESS_RUNNING) {
		return;
	}
	current->total_time++;
	if (current->time_slice > 0) {
		current->time_slice--;
		if (current->time_slice == 0) {
			current->reschedule = true;
		}
	}
}

static void process_set_scheduler_active(bool active) {
	scheduler_active = active;
}

bool process_scheduler_is_active(void) {
	return scheduler_active;
}

void process_scheduler_start(void) {
	process_set_scheduler_active(true);
}

void process_scheduler_stop(void) {
	process_set_scheduler_active(false);
}

process_t *process_spawn_proc(const char *path, const char *args, uint32_t args_len) {
	process_t *proc = process_create(path);
	if (!proc) {
		return NULL;
	}
	if (!process_exec(proc, path, args, args_len)) {
		process_destroy(proc);
		return NULL;
	}
	proc->state = PROCESS_READY;
	process_ready_enqueue(proc);
	return proc;
}

pipe_t *pipe_create(void) {
	pipe_t *pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
	if (!pipe) {
		return NULL;
	}
	memset(pipe, 0, sizeof(*pipe));
	return pipe;
}

void pipe_retain_read(pipe_t *pipe) {
	if (pipe) {
		pipe->readers++;
	}
}

void pipe_retain_write(pipe_t *pipe) {
	if (pipe) {
		pipe->writers++;
	}
}

static void pipe_maybe_free(pipe_t *pipe) {
	if (!pipe) {
		return;
	}
	if (pipe->readers == 0 && pipe->writers == 0) {
		kfree(pipe);
	}
}

void pipe_release_read(pipe_t *pipe) {
	if (!pipe) {
		return;
	}
	if (pipe->readers > 0) {
		pipe->readers--;
	}
	pipe_wake_writers(pipe);
	pipe_maybe_free(pipe);
}

void pipe_release_write(pipe_t *pipe) {
	if (!pipe) {
		return;
	}
	if (pipe->writers > 0) {
		pipe->writers--;
	}
	pipe_wake_readers(pipe);
	pipe_maybe_free(pipe);
}

void process_fd_close(process_t *proc, int fd) {
	if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) {
		return;
	}
	process_fd_t *entry = &proc->fds[fd];
	if (!entry->used) {
		return;
	}
	if (entry->type == PROCESS_FD_PIPE_READ) {
		pipe_release_read(entry->pipe);
	} else if (entry->type == PROCESS_FD_PIPE_WRITE) {
		pipe_release_write(entry->pipe);
	}
	entry->used = false;
	entry->type = PROCESS_FD_NONE;
	entry->path[0] = '\0';
	entry->offset = 0;
	entry->pipe = NULL;
}

bool process_fd_set_pipe(process_t *proc, int fd, pipe_t *pipe, bool writable) {
	if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS || !pipe) {
		return false;
	}
	process_fd_close(proc, fd);
	proc->fds[fd].used = true;
	proc->fds[fd].type = writable ? PROCESS_FD_PIPE_WRITE : PROCESS_FD_PIPE_READ;
	proc->fds[fd].pipe = pipe;
	proc->fds[fd].path[0] = '\0';
	proc->fds[fd].offset = 0;
	if (writable) {
		pipe_retain_write(pipe);
	} else {
		pipe_retain_read(pipe);
	}
	return true;
}

static void pipe_peek(pipe_t *pipe, uint8_t *dst, uint32_t len) {
	if (!pipe || !dst || len == 0) {
		return;
	}
	uint32_t first = PIPE_BUFFER_SIZE - pipe->read_pos;
	if (len <= first) {
		memcpy(dst, &pipe->buffer[pipe->read_pos], len);
	} else {
		memcpy(dst, &pipe->buffer[pipe->read_pos], first);
		memcpy(dst + first, pipe->buffer, len - first);
	}
}

static void pipe_discard(pipe_t *pipe, uint32_t len) {
	if (!pipe || len == 0) {
		return;
	}
	pipe->read_pos = (pipe->read_pos + len) % PIPE_BUFFER_SIZE;
	pipe->count -= len;
}

static void pipe_write_buffer(pipe_t *pipe, const uint8_t *src, uint32_t len) {
	if (!pipe || !src || len == 0) {
		return;
	}
	uint32_t first = PIPE_BUFFER_SIZE - pipe->write_pos;
	if (len <= first) {
		memcpy(&pipe->buffer[pipe->write_pos], src, len);
	} else {
		memcpy(&pipe->buffer[pipe->write_pos], src, first);
		memcpy(pipe->buffer, src + first, len - first);
	}
	pipe->write_pos = (pipe->write_pos + len) % PIPE_BUFFER_SIZE;
	pipe->count += len;
}

static int pipe_read_now(process_t *proc, pipe_t *pipe, uint32_t user_buf, uint32_t len) {
	if (!proc || !pipe || len == 0) {
		return 0;
	}
	uint32_t to_read = len;
	if (to_read > pipe->count) {
		to_read = pipe->count;
	}
	if (to_read == 0) {
		return 0;
	}
	uint8_t tmp[PIPE_BUFFER_SIZE];
	pipe_peek(pipe, tmp, to_read);
	if (!page_copy_to_user(proc->page_directory, user_buf, tmp, to_read)) {
		return -1;
	}
	pipe_discard(pipe, to_read);
	return (int)to_read;
}

static int pipe_write_now(process_t *proc, pipe_t *pipe, uint32_t user_buf,
                          uint32_t len, uint32_t offset) {
	if (!proc || !pipe || len == 0) {
		return 0;
	}
	uint32_t remaining = len;
	uint32_t written = 0;
	uint8_t tmp[PIPE_BUFFER_SIZE];
	while (remaining > 0) {
		uint32_t space = PIPE_BUFFER_SIZE - pipe->count;
		if (space == 0) {
			break;
		}
		uint32_t chunk = remaining;
		if (chunk > space) {
			chunk = space;
		}
		if (chunk > sizeof(tmp)) {
			chunk = sizeof(tmp);
		}
		if (!page_copy_from_user(proc->page_directory, tmp, user_buf + offset + written, chunk)) {
			return -1;
		}
		pipe_write_buffer(pipe, tmp, chunk);
		written += chunk;
		remaining -= chunk;
	}
	return (int)written;
}

static void pipe_wait_clear(process_t *proc) {
	proc->pipe_wait = NULL;
	proc->pipe_wait_op = PIPE_WAIT_NONE;
	proc->pipe_wait_buf = 0;
	proc->pipe_wait_len = 0;
	proc->pipe_wait_done = 0;
}

static void pipe_complete_read(process_t *proc) {
	if (!proc || !proc->pipe_wait || proc->pipe_wait_op != PIPE_WAIT_READ) {
		return;
	}
	pipe_t *pipe = proc->pipe_wait;
	int result = 0;
	if (pipe->count > 0) {
		result = pipe_read_now(proc, pipe, proc->pipe_wait_buf, proc->pipe_wait_len);
	} else if (pipe->writers == 0) {
		result = 0;
	} else {
		return;
	}
	if (result > 0) {
		pipe_wake_writers(pipe);
	}
	proc->frame.eax = (result < 0) ? (uint32_t)-1 : (uint32_t)result;
	pipe_wait_clear(proc);
	proc->state = PROCESS_READY;
	process_ready_enqueue(proc);
}

static void pipe_complete_write(process_t *proc) {
	if (!proc || !proc->pipe_wait || proc->pipe_wait_op != PIPE_WAIT_WRITE) {
		return;
	}
	pipe_t *pipe = proc->pipe_wait;
	if (pipe->readers == 0) {
		proc->frame.eax = (uint32_t)-1;
		pipe_wait_clear(proc);
		proc->state = PROCESS_READY;
		process_ready_enqueue(proc);
		return;
	}
	uint32_t remaining = proc->pipe_wait_len - proc->pipe_wait_done;
	int wrote = pipe_write_now(proc, pipe, proc->pipe_wait_buf, remaining, proc->pipe_wait_done);
	if (wrote < 0) {
		proc->frame.eax = (uint32_t)-1;
		pipe_wait_clear(proc);
		proc->state = PROCESS_READY;
		process_ready_enqueue(proc);
		return;
	}
	if (wrote > 0) {
		pipe_wake_readers(pipe);
	}
	proc->pipe_wait_done += (uint32_t)wrote;
	if (proc->pipe_wait_done >= proc->pipe_wait_len) {
		proc->frame.eax = proc->pipe_wait_len;
		pipe_wait_clear(proc);
		proc->state = PROCESS_READY;
		process_ready_enqueue(proc);
	}
}

static void pipe_wake_readers(pipe_t *pipe) {
	if (!pipe) {
		return;
	}
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->state != PROCESS_BLOCKED || proc->pipe_wait != pipe ||
		    proc->pipe_wait_op != PIPE_WAIT_READ) {
			continue;
		}
		pipe_complete_read(proc);
	}
}

static void pipe_wake_writers(pipe_t *pipe) {
	if (!pipe) {
		return;
	}
	for (process_t *proc = all_head; proc; proc = proc->all_next) {
		if (proc->state != PROCESS_BLOCKED || proc->pipe_wait != pipe ||
		    proc->pipe_wait_op != PIPE_WAIT_WRITE) {
			continue;
		}
		pipe_complete_write(proc);
	}
}

bool process_pipe_read(trap_frame_t *frame, process_t *proc, pipe_t *pipe,
                       uint32_t user_buf, uint32_t len, int *out_read) {
	if (!frame || !proc || !pipe || !out_read) {
		return true;
	}
	if (len == 0) {
		*out_read = 0;
		return true;
	}
	if (pipe->count > 0) {
		int read = pipe_read_now(proc, pipe, user_buf, len);
		*out_read = read;
		pipe_wake_writers(pipe);
		return true;
	}
	if (pipe->writers == 0) {
		*out_read = 0;
		return true;
	}
	proc->pipe_wait = pipe;
	proc->pipe_wait_op = PIPE_WAIT_READ;
	proc->pipe_wait_buf = user_buf;
	proc->pipe_wait_len = len;
	proc->pipe_wait_done = 0;
	if (process_block_and_switch(frame, proc)) {
		pipe_wait_clear(proc);
		*out_read = -1;
		return true;
	}
	return false;
}

bool process_pipe_write(trap_frame_t *frame, process_t *proc, pipe_t *pipe,
                        uint32_t user_buf, uint32_t len, int *out_written) {
	if (!frame || !proc || !pipe || !out_written) {
		return true;
	}
	if (len == 0) {
		*out_written = 0;
		return true;
	}
	if (pipe->readers == 0) {
		*out_written = -1;
		return true;
	}
	int wrote = pipe_write_now(proc, pipe, user_buf, len, 0);
	if (wrote < 0) {
		*out_written = -1;
		return true;
	}
	if (wrote > 0) {
		pipe_wake_readers(pipe);
	}
	if ((uint32_t)wrote >= len) {
		*out_written = wrote;
		return true;
	}
	proc->pipe_wait = pipe;
	proc->pipe_wait_op = PIPE_WAIT_WRITE;
	proc->pipe_wait_buf = user_buf;
	proc->pipe_wait_len = len;
	proc->pipe_wait_done = (uint32_t)wrote;
	if (process_block_and_switch(frame, proc)) {
		pipe_wait_clear(proc);
		*out_written = -1;
		return true;
	}
	return false;
}

bool process_kill_other(uint32_t pid, int exit_code) {
	process_t *target = process_find(pid);
	if (!target || target == current_process) {
		return false;
	}
	bool had_waiter = false;
	target->exit_code = exit_code;
	process_wake_waiters(target, exit_code, &had_waiter);
	process_close_all_fds(target);
	target->waiting = false;
	target->wait_pid = 0;
	target->wait_status_ptr = 0;
	target->sleeping = false;
	target->sleep_until = 0;
	pipe_wait_clear(target);
	target->state = PROCESS_ZOMBIE;
	if (target->page_directory) {
		page_directory_destroy(target->page_directory);
		target->page_directory = NULL;
	}
	process_ready_remove(target);
	return true;
}
