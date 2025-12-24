#include <kernel/usermode.h>
#include <kernel/elf.h>
#include <kernel/syscall.h>
#include <kernel/fs.h>
#include <kernel/user_programs.h>
#include <stdio.h>
#include <string.h>

extern void enter_user_mode(uint32_t entry, uint32_t user_stack);

static char current_args[USERMODE_MAX_ARGS];
static uint32_t current_args_len = 0;
static char pending_exec_path[USERMODE_MAX_PATH];
static char pending_exec_args[USERMODE_MAX_ARGS];
static uint32_t pending_exec_args_len = 0;
static bool exec_requested = false;
static char current_cwd[USERMODE_MAX_PATH] = "/";

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

bool usermode_run_elf_impl(const char *path) {
	const char *next_path = path;

	while (next_path) {
		elf_image_t image;
		fs_inode_t inode;

		syscall_reset_exit();
		exec_requested = false;

		if (!fs_stat(next_path, &inode)) {
			if (user_program_install_if_embedded(next_path)) {
				fs_stat(next_path, &inode);
			}
		}

		if (!elf_load_file(next_path, &image)) {
			return false;
		}

		uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
		if (image.max_vaddr >= stack_bottom) {
			printf("User stack overlaps program image\n");
			return false;
		}

		memset((void *)stack_bottom, 0, USER_STACK_SIZE);

		enter_user_mode(image.entry, USER_STACK_TOP);

		if (exec_requested) {
			next_path = pending_exec_path;
			usermode_set_args(pending_exec_args, pending_exec_args_len);
			exec_requested = false;
			continue;
		}

		break;
	}

	return true;
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

void usermode_request_exec(const char *path, const char *args, uint32_t args_len) {
	if (!path || path[0] == '\0') {
		return;
	}
	strncpy(pending_exec_path, path, sizeof(pending_exec_path) - 1);
	pending_exec_path[sizeof(pending_exec_path) - 1] = '\0';

	if (args && args_len > 0) {
		if (args_len >= USERMODE_MAX_ARGS) {
			args_len = USERMODE_MAX_ARGS - 1;
		}
		memcpy(pending_exec_args, args, args_len);
		pending_exec_args[args_len] = '\0';
		pending_exec_args_len = args_len;
	} else {
		pending_exec_args[0] = '\0';
		pending_exec_args_len = 0;
	}

	exec_requested = true;
}

uint32_t usermode_get_args(char *dst, uint32_t max_len) {
	uint32_t total = current_args_len;
	if (!dst || max_len == 0) {
		return total;
	}
	uint32_t to_copy = total;
	if (to_copy > max_len) {
		to_copy = max_len;
	}
	memcpy(dst, current_args, to_copy);
	return total;
}

void usermode_set_cwd(const char *path) {
	if (!path || path[0] == '\0') {
		return;
	}
	strncpy(current_cwd, path, sizeof(current_cwd) - 1);
	current_cwd[sizeof(current_cwd) - 1] = '\0';
}

const char *usermode_get_cwd(void) {
	return current_cwd;
}
