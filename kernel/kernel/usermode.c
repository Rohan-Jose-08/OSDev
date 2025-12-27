#include <kernel/usermode.h>
#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/gdt.h>
#include <string.h>

extern void trampoline_enter_user_mode(uint32_t user_cr3, uint32_t kernel_esp);

typedef struct {
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;
	uint32_t useresp;
	uint32_t userss;
} __attribute__((packed)) user_iret_frame_t;

static char current_args[USERMODE_MAX_ARGS];
static uint32_t current_args_len = 0;

static void usermode_set_args(const char *args, uint32_t len) {
	if (!args || len == 0) {
		current_args[0] = '\0';
		current_args_len = 0;
		return;
	}
	if (len >= USERMODE_MAX_ARGS) {
		len = USERMODE_MAX_ARGS - 1;
	}
	memcpy(current_args, args, len);
	current_args[len] = '\0';
	current_args_len = len;
}

static bool usermode_run_scheduler(void) {
	if (process_scheduler_is_active()) {
		if (process_current()) {
			return true;
		}
		process_scheduler_stop();
	}

	process_scheduler_start();
	process_t *next = process_next_ready();
	if (!next) {
		process_scheduler_stop();
		return false;
	}

	process_set_current(next);
	next->state = PROCESS_RUNNING;
	process_activate(next);
	user_iret_frame_t *iret_frame =
		(user_iret_frame_t *)(next->kernel_stack_top - sizeof(*iret_frame));
	iret_frame->eip = next->frame.eip;
	iret_frame->cs = GDT_USER_CODE;
	iret_frame->eflags = next->frame.eflags | 0x200;
	iret_frame->useresp = next->frame.useresp;
	iret_frame->userss = GDT_USER_DATA;
	trampoline_enter_user_mode(virt_to_phys(next->page_directory),
	                           (uint32_t)iret_frame);

	process_set_current(NULL);
	process_scheduler_stop();
	return true;
}

bool usermode_run_elf_impl(const char *path) {
	syscall_reset_exit();

	int pid = process_spawn(path, current_args, current_args_len);
	if (pid < 0) {
		return false;
	}
	return usermode_run_scheduler();
}

uint32_t usermode_last_exit_code(void) {
	return syscall_exit_status();
}

bool usermode_run_elf_with_args(const char *path, const char *args) {
	if (!path) {
		return false;
	}
	usermode_set_args(args, args ? (uint32_t)strlen(args) : 0);
	return usermode_run_elf(path);
}

bool usermode_run_ready(void) {
	syscall_reset_exit();
	return usermode_run_scheduler();
}

uint32_t usermode_get_args(char *dst, uint32_t max_len) {
	return process_get_args(process_current(), dst, max_len);
}

void usermode_set_cwd(const char *path) {
	process_set_default_cwd(path);
	process_set_cwd(process_current(), path);
}

const char *usermode_get_cwd(void) {
	return process_get_cwd(process_current());
}
