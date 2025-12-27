#include <kernel/syscall.h>
#include <kernel/tty.h>
#include <kernel/fs.h>
#include <kernel/usermode.h>
#include <kernel/elf.h>
#include <kernel/shell.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <kernel/io.h>
#include <kernel/speaker.h>
#include <kernel/audio.h>
#include <kernel/desktop.h>
#include <kernel/graphics.h>
#include <kernel/graphics_demo.h>
#include <kernel/paint.h>
#include <kernel/calculator.h>
#include <kernel/file_manager.h>
#include <kernel/mouse.h>
#include <kernel/process.h>
#include <kernel/pagings.h>
#include <kernel/kmalloc.h>
#include <kernel/user_programs.h>
#include <string.h>

volatile uint32_t syscall_exit_requested = 0;
volatile uint32_t syscall_exit_code = 0;
volatile uint32_t usermode_return_esp = 0;
volatile uint32_t usermode_saved_ebx = 0;
volatile uint32_t usermode_saved_esi = 0;
volatile uint32_t usermode_saved_edi = 0;
volatile uint32_t usermode_saved_ebp = 0;
volatile uint32_t usermode_abort_requested = 0;

#define ALIAS_NAME_MAX 32
#define ALIAS_CMD_MAX 256

typedef struct {
	uint32_t size;
	uint32_t type;
	uint16_t permissions;
	uint16_t uid;
	uint16_t gid;
	uint16_t reserved;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
} user_stat_t;

typedef struct {
	char name[FS_MAX_FILENAME];
	uint32_t type;
	uint32_t size;
} user_dirent_t;

typedef struct {
	uint32_t total_size;
	uint32_t used_size;
	uint32_t free_size;
	uint32_t largest_free_block;
} user_heap_stats_t;

typedef struct {
	uint32_t pid;
	uint8_t state;
	uint8_t priority;
	uint16_t reserved;
	uint32_t time_slice;
	uint32_t total_time;
	char name[PROCESS_NAME_MAX];
} user_process_info_t;

typedef struct {
	int32_t x;
	int32_t y;
	uint8_t color;
} user_gfx_pixel_t;

typedef struct {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	uint8_t color;
} user_gfx_rect_t;

typedef struct {
	int32_t x1;
	int32_t y1;
	int32_t x2;
	int32_t y2;
	uint8_t color;
} user_gfx_line_t;

typedef struct {
	int32_t x;
	int32_t y;
	char c;
	uint8_t fg;
	uint8_t bg;
} user_gfx_char_t;

typedef struct {
	int32_t x;
	int32_t y;
	uint8_t fg;
	uint8_t bg;
	const char *text;
} user_gfx_print_t;

typedef struct {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t stride;
	const uint8_t *pixels;
} user_gfx_blit_t;

static bool user_range_ok(uint32_t addr, uint32_t size) {
	if (size == 0) {
		return true;
	}
	if (addr < ELF_USER_LOAD_MIN) {
		return false;
	}
	uint32_t end = addr + size;
	if (end < addr || end > USER_STACK_TOP) {
		return false;
	}
	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		return false;
	}
	if (!page_user_range_mapped(proc->page_directory, addr, size)) {
		return false;
	}
	return true;
}

static bool user_range_ok_mul(uint32_t addr, uint32_t count, uint32_t size) {
	if (count == 0 || size == 0) {
		return true;
	}
	if (count > UINT32_MAX / size) {
		return false;
	}
	return user_range_ok(addr, count * size);
}

static bool copy_user_out(void *dst, uint32_t dst_len, const void *src, uint32_t src_len) {
	if (src_len == 0) {
		return true;
	}
	if (src_len > dst_len) {
		return false;
	}
	if (!dst || !src || !user_range_ok((uint32_t)dst, dst_len)) {
		return false;
	}
	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		return false;
	}
	return page_copy_to_user(proc->page_directory, (uint32_t)dst, src, src_len);
}

static bool copy_user_in(void *dst, uint32_t dst_len, const void *src, uint32_t src_len) {
	if (src_len == 0) {
		return true;
	}
	if (src_len > dst_len) {
		return false;
	}
	if (!dst || !src || !user_range_ok((uint32_t)src, src_len)) {
		return false;
	}
	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		return false;
	}
	return page_copy_from_user(proc->page_directory, dst, (uint32_t)src, src_len);
}

static bool copy_user_string(char *dst, size_t dst_size, const char *user_ptr) {
	if (!dst || dst_size == 0 || !user_ptr) {
		return false;
	}
	dst[0] = '\0';

	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		return false;
	}

	for (size_t i = 0; i < dst_size - 1; i++) {
		uint32_t addr = (uint32_t)(user_ptr + i);
		if (!user_range_ok(addr, 1)) {
			return false;
		}
		char c = '\0';
		if (!page_copy_from_user(proc->page_directory, &c, addr, 1)) {
			return false;
		}
		dst[i] = c;
		if (c == '\0') {
			return true;
		}
	}

	dst[dst_size - 1] = '\0';
	return false;
}

static process_t *syscall_require_process(syscall_frame_t *frame) {
	process_t *proc = process_current();
	if (!proc) {
		if (frame) {
			frame->eax = (uint32_t)-1;
		}
		return NULL;
	}
	return proc;
}

void syscall_dispatch(syscall_frame_t *frame) {
	switch (frame->eax) {
		case SYSCALL_WRITE: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			const char *buf = (const char *)frame->ebx;
			uint32_t len = frame->ecx;
			if (len == 0) {
				frame->eax = 0;
				break;
			}
			if (!buf) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			process_fd_t *out = &proc->fds[1];
			if (!out->used || out->type == PROCESS_FD_TTY || out->type == PROCESS_FD_NONE) {
				uint32_t remaining = len;
				uint32_t offset = 0;
				char tmp[256];
				while (remaining > 0) {
					uint32_t chunk = remaining;
					if (chunk > sizeof(tmp)) {
						chunk = sizeof(tmp);
					}
					if (!copy_user_in(tmp, chunk, buf + offset, chunk)) {
						frame->eax = (uint32_t)-1;
						break;
					}
					terminal_write(tmp, chunk);
					remaining -= chunk;
					offset += chunk;
				}
				if (frame->eax != (uint32_t)-1) {
					frame->eax = len;
				}
				break;
			}

			if (out->type == PROCESS_FD_PIPE_WRITE) {
				int written = 0;
				if (!process_pipe_write(frame, proc, out->pipe, (uint32_t)buf, len, &written)) {
					break;
				}
				frame->eax = (written < 0) ? (uint32_t)-1 : (uint32_t)written;
				break;
			}

			frame->eax = (uint32_t)-1;
			break;
		}
		case SYSCALL_OPEN: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			char path[PROCESS_FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			fs_inode_t inode;
			if (!fs_stat(path, &inode) || inode.type != 1) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int fd = -1;
			for (int i = 0; i < PROCESS_MAX_FDS; i++) {
				if (!proc->fds[i].used) {
					fd = i;
					proc->fds[i].used = true;
					proc->fds[i].type = PROCESS_FD_FILE;
					proc->fds[i].offset = 0;
					strncpy(proc->fds[i].path, path, sizeof(proc->fds[i].path) - 1);
					proc->fds[i].path[sizeof(proc->fds[i].path) - 1] = '\0';
					proc->fds[i].pipe = NULL;
					break;
				}
			}
			if (fd < 0) {
				for (int i = 0; i < PROCESS_MAX_FDS; i++) {
					if (proc->fds[i].used && proc->fds[i].type == PROCESS_FD_NONE) {
						fd = i;
						proc->fds[i].used = true;
						proc->fds[i].type = PROCESS_FD_FILE;
						proc->fds[i].offset = 0;
						strncpy(proc->fds[i].path, path, sizeof(proc->fds[i].path) - 1);
						proc->fds[i].path[sizeof(proc->fds[i].path) - 1] = '\0';
						proc->fds[i].pipe = NULL;
						break;
					}
				}
			}
			frame->eax = (fd >= 0) ? (uint32_t)fd : (uint32_t)-1;
			break;
		}
		case SYSCALL_READ: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			uint32_t fd = frame->ebx;
			uint8_t *buf = (uint8_t *)frame->ecx;
			uint32_t len = frame->edx;
			if (fd >= PROCESS_MAX_FDS || !proc->fds[fd].used || len == 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			process_fd_t *entry = &proc->fds[fd];
			if (entry->type == PROCESS_FD_PIPE_READ) {
				int read = 0;
				if (!process_pipe_read(frame, proc, entry->pipe, (uint32_t)buf, len, &read)) {
					break;
				}
				frame->eax = (read < 0) ? (uint32_t)-1 : (uint32_t)read;
				break;
			}
			if (entry->type == PROCESS_FD_TTY) {
				uint32_t total = 0;
				uint8_t tmp[64];
				while (total < len) {
					while (!keyboard_has_input()) {
						__asm__ volatile ("hlt");
					}
					uint32_t chunk = 0;
					while (chunk < sizeof(tmp) && total + chunk < len && keyboard_has_input()) {
						tmp[chunk++] = (uint8_t)keyboard_getchar();
					}
					if (chunk == 0) {
						break;
					}
					if (!copy_user_out(buf + total, chunk, tmp, chunk)) {
						frame->eax = (uint32_t)-1;
						break;
					}
					total += chunk;
					if (!keyboard_has_input()) {
						break;
					}
				}
				if (frame->eax != (uint32_t)-1) {
					frame->eax = total;
				}
				break;
			}
			if (entry->type != PROCESS_FD_FILE) {
				frame->eax = (uint32_t)-1;
				break;
			}

			uint32_t remaining = len;
			uint32_t offset = 0;
			uint8_t tmp[256];
			while (remaining > 0) {
				uint32_t chunk = remaining;
				if (chunk > sizeof(tmp)) {
					chunk = sizeof(tmp);
				}
				int read = fs_read_file(entry->path, tmp, chunk, entry->offset);
				if (read < 0) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (read == 0) {
					break;
				}
				if (!copy_user_out(buf + offset, (uint32_t)read, tmp, (uint32_t)read)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				entry->offset += (uint32_t)read;
				remaining -= (uint32_t)read;
				offset += (uint32_t)read;
				if ((uint32_t)read < chunk) {
					break;
				}
			}
			if (frame->eax != (uint32_t)-1) {
				frame->eax = offset;
			}
			break;
		}
		case SYSCALL_CLOSE: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			uint32_t fd = frame->ebx;
			if (fd >= PROCESS_MAX_FDS || !proc->fds[fd].used) {
				frame->eax = (uint32_t)-1;
				break;
			}
			process_fd_close(proc, (int)fd);
			frame->eax = 0;
			break;
		}
		case SYSCALL_STAT: {
			char path[PROCESS_FD_PATH_MAX];
			user_stat_t *out = (user_stat_t *)frame->ecx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!out || !user_range_ok((uint32_t)out, sizeof(user_stat_t))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			fs_inode_t inode;
			if (!fs_stat(path, &inode)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			user_stat_t temp;
			temp.size = inode.size;
			temp.type = inode.type;
			temp.permissions = inode.permissions;
			temp.uid = inode.uid;
			temp.gid = inode.gid;
			temp.reserved = 0;
			temp.atime = inode.atime;
			temp.mtime = inode.mtime;
			temp.ctime = inode.ctime;
			if (!copy_user_out(out, sizeof(temp), &temp, sizeof(temp))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_SEEK: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			uint32_t fd = frame->ebx;
			int32_t offset = (int32_t)frame->ecx;
			uint32_t whence = frame->edx;

			if (fd >= PROCESS_MAX_FDS || !proc->fds[fd].used ||
			    proc->fds[fd].type != PROCESS_FD_FILE) {
				frame->eax = (uint32_t)-1;
				break;
			}

			fs_inode_t inode;
			if (!fs_stat(proc->fds[fd].path, &inode)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			int32_t base = 0;
			if (whence == 1) {
				base = (int32_t)proc->fds[fd].offset;
			} else if (whence == 2) {
				base = (int32_t)inode.size;
			} else if (whence != 0) {
				frame->eax = (uint32_t)-1;
				break;
			}

			int32_t new_off = base + offset;
			if (new_off < 0 || new_off > (int32_t)inode.size) {
				frame->eax = (uint32_t)-1;
				break;
			}

			proc->fds[fd].offset = (uint32_t)new_off;
			frame->eax = (uint32_t)new_off;
			break;
		}
		case SYSCALL_LISTDIR: {
			char path[PROCESS_FD_PATH_MAX];
			user_dirent_t *out = (user_dirent_t *)frame->ecx;
			uint32_t max_entries = frame->edx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!out || max_entries == 0) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (!user_range_ok_mul((uint32_t)out, max_entries, sizeof(user_dirent_t))) {
				frame->eax = (uint32_t)-1;
				break;
			}

			fs_dirent_t entries[64];
			uint32_t cap = max_entries;
			if (cap > 64) {
				cap = 64;
			}

			int count = fs_list_dir(path, entries, (int)cap);
			if (count < 0) {
				frame->eax = (uint32_t)-1;
				break;
			}

			for (int i = 0; i < count; i++) {
				user_dirent_t temp;
				memset(&temp, 0, sizeof(temp));
				strncpy(temp.name, entries[i].name, FS_MAX_FILENAME - 1);
				temp.name[FS_MAX_FILENAME - 1] = '\0';

				char entry_path[PROCESS_FD_PATH_MAX];
				if (strcmp(path, "/") == 0) {
					snprintf(entry_path, sizeof(entry_path), "/%s", entries[i].name);
				} else {
					snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entries[i].name);
				}

				fs_inode_t inode;
				if (fs_stat(entry_path, &inode)) {
					temp.type = inode.type;
					temp.size = inode.size;
				}

				if (!copy_user_out(&out[i], sizeof(user_dirent_t), &temp, sizeof(user_dirent_t))) {
					frame->eax = (uint32_t)-1;
					break;
				}
			}

			if (frame->eax == (uint32_t)-1) {
				break;
			}

			frame->eax = (uint32_t)count;
			break;
		}
		case SYSCALL_MKDIR: {
			char path[PROCESS_FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int res = fs_create_dir(path);
			frame->eax = (res >= 0 || res == -2) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_RM: {
			char path[PROCESS_FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = fs_delete(path) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_TOUCH: {
			char path[PROCESS_FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int res = fs_create_file(path);
			frame->eax = (res >= 0 || res == -2) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_RENAME: {
			char old_path[PROCESS_FD_PATH_MAX];
			char new_name[FS_MAX_FILENAME];
			if (!copy_user_string(old_path, sizeof(old_path), (const char *)frame->ebx) ||
			    !copy_user_string(new_name, sizeof(new_name), (const char *)frame->ecx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = fs_rename(old_path, new_name) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_GETCWD: {
			char *buf = (char *)frame->ebx;
			uint32_t len = frame->ecx;
			if (!buf || len == 0 || !user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			const char *cwd = usermode_get_cwd();
			uint32_t needed = (uint32_t)strlen(cwd) + 1;
			if (len < needed) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!copy_user_out(buf, needed, cwd, needed)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = needed;
			break;
		}
		case SYSCALL_SETCWD: {
			char path[PROCESS_FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			fs_inode_t inode;
			if (!fs_stat(path, &inode) || inode.type != 2) {
				frame->eax = (uint32_t)-1;
				break;
			}
			shell_set_cwd(path);
			frame->eax = 0;
			break;
		}
		case SYSCALL_CLEAR: {
			terminal_initialize();
			frame->eax = 0;
			break;
		}
		case SYSCALL_SETCOLOR: {
			uint32_t fg = frame->ebx;
			uint32_t bg = frame->ecx;
			if (fg > 15 || bg > 15) {
				frame->eax = (uint32_t)-1;
				break;
			}
			uint8_t color = (uint8_t)((bg << 4) | (fg & 0x0F));
			terminal_setcolor(color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_WRITEFILE: {
			char path[PROCESS_FD_PATH_MAX];
			const uint8_t *buf = (const uint8_t *)frame->ecx;
			uint32_t len = frame->edx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (len > 0 && !user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			int res = fs_create_file(path);
			if (res < 0 && res != -2) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (len == 0) {
				frame->eax = 0;
				break;
			}

			uint8_t *tmp = (uint8_t *)kmalloc(len);
			if (!tmp) {
				frame->eax = (uint32_t)-1;
				break;
			}

			uint32_t remaining = len;
			uint32_t offset = 0;
			while (remaining > 0) {
				uint32_t chunk = remaining;
				if (chunk > 256) {
					chunk = 256;
				}
				if (!copy_user_in(tmp + offset, chunk, buf + offset, chunk)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				offset += chunk;
				remaining -= chunk;
			}

			if (frame->eax != (uint32_t)-1) {
				int written = fs_write_file(path, tmp, len, 0);
				frame->eax = (written < 0) ? (uint32_t)-1 : (uint32_t)written;
			}

			kfree(tmp);
			break;
		}
		case SYSCALL_HISTORY_COUNT: {
			int count = shell_history_count();
			frame->eax = (count >= 0) ? (uint32_t)count : (uint32_t)-1;
			break;
		}
		case SYSCALL_HISTORY_GET: {
			uint32_t index = frame->ebx;
			char *buf = (char *)frame->ecx;
			uint32_t len = frame->edx;

			if (!buf || len == 0 || !user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			const char *entry = shell_history_entry((int)index);
			if (!entry) {
				frame->eax = (uint32_t)-1;
				break;
			}

			uint32_t entry_len = (uint32_t)strlen(entry);
			if (len <= entry_len) {
				entry_len = len - 1;
			}
			if (!copy_user_out(buf, entry_len, entry, entry_len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!copy_user_out(buf + entry_len, 1, "\0", 1)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = entry_len;
			break;
		}
		case SYSCALL_GET_TICKS: {
			frame->eax = timer_get_ticks();
			break;
		}
		case SYSCALL_GET_COMMAND_COUNT: {
			frame->eax = shell_command_count();
			break;
		}
		case SYSCALL_GETCHAR: {
			while (!keyboard_has_input()) {
				__asm__ volatile ("hlt");
			}
			frame->eax = (uint32_t)(unsigned char)keyboard_getchar();
			break;
		}
		case SYSCALL_SLEEP_MS: {
			uint32_t ms = frame->ebx;
			if (ms == 0) {
				frame->eax = 0;
				break;
			}
			uint32_t ticks = (ms * 100) / 1000;
			if (ticks == 0 && ms > 0) {
				ticks = 1;
			}
			uint32_t wake = timer_get_ticks() + ticks;
			if (process_sleep_until(frame, wake)) {
				timer_sleep_ms(ms);
				frame->eax = 0;
			}
			break;
		}
		case SYSCALL_ALIAS_SET: {
			char name[ALIAS_NAME_MAX];
			char cmd[ALIAS_CMD_MAX];

			if (!copy_user_string(name, sizeof(name), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!copy_user_string(cmd, sizeof(cmd), (const char *)frame->ecx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = (shell_alias_set(name, cmd) == 0) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_ALIAS_REMOVE: {
			char name[ALIAS_NAME_MAX];
			if (!copy_user_string(name, sizeof(name), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = (shell_alias_remove(name) == 0) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_ALIAS_COUNT: {
			int count = shell_alias_count();
			frame->eax = (count >= 0) ? (uint32_t)count : (uint32_t)-1;
			break;
		}
		case SYSCALL_ALIAS_GET: {
			uint32_t index = frame->ebx;
			char *name = (char *)frame->ecx;
			char *cmd = (char *)frame->edx;

			if (!name || !cmd) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok((uint32_t)name, ALIAS_NAME_MAX) ||
			    !user_range_ok((uint32_t)cmd, ALIAS_CMD_MAX)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			char tmp_name[ALIAS_NAME_MAX];
			char tmp_cmd[ALIAS_CMD_MAX];
			memset(tmp_name, 0, sizeof(tmp_name));
			memset(tmp_cmd, 0, sizeof(tmp_cmd));
			if (shell_alias_get((int)index, tmp_name, sizeof(tmp_name),
			                    tmp_cmd, sizeof(tmp_cmd)) != 0) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (!copy_user_out(name, sizeof(tmp_name), tmp_name, sizeof(tmp_name)) ||
			    !copy_user_out(cmd, sizeof(tmp_cmd), tmp_cmd, sizeof(tmp_cmd))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_TIMER_START: {
			frame->eax = (shell_timer_start() == 0) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_TIMER_STOP: {
			unsigned int elapsed = 0;
			if (shell_timer_stop(&elapsed) < 0) {
				frame->eax = (uint32_t)-1;
			} else {
				frame->eax = elapsed;
			}
			break;
		}
		case SYSCALL_TIMER_STATUS: {
			frame->eax = (uint32_t)shell_timer_status();
			break;
		}
		case SYSCALL_BEEP: {
			uint32_t frequency = frame->ebx;
			uint32_t duration = frame->ecx;
			frame->eax = (speaker_beep(frequency, duration) == 0) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_SPEAKER_START: {
			uint32_t frequency = frame->ebx;
			speaker_start(frequency);
			frame->eax = 0;
			break;
		}
		case SYSCALL_SPEAKER_STOP: {
			speaker_stop();
			frame->eax = 0;
			break;
		}
		case SYSCALL_AUDIO_WRITE: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			const uint8_t *buf = (const uint8_t *)frame->ebx;
			uint32_t len = frame->ecx;
			if (!buf || len == 0) {
				frame->eax = 0;
				break;
			}
			if (!user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			uint32_t total = 0;
			uint8_t tmp[256];
			while (total < len) {
				uint32_t chunk = len - total;
				if (chunk > sizeof(tmp)) {
					chunk = sizeof(tmp);
				}
				if (!copy_user_in(tmp, chunk, buf + total, chunk)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				int written = audio_write(tmp, chunk);
				if (written < 0) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (written == 0) {
					break;
				}
				total += (uint32_t)written;
				if ((uint32_t)written < chunk) {
					break;
				}
			}
			if (frame->eax != (uint32_t)-1) {
				frame->eax = total;
			}
			break;
		}
		case SYSCALL_AUDIO_SET_VOLUME: {
			uint8_t master = (uint8_t)frame->ebx;
			uint8_t pcm = (uint8_t)frame->ecx;
			frame->eax = audio_set_volume(master, pcm) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_AUDIO_GET_VOLUME: {
			uint8_t master = 0;
			uint8_t pcm = 0;
			if (!audio_get_volume(&master, &pcm)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = (uint32_t)master | ((uint32_t)pcm << 8);
			break;
		}
		case SYSCALL_AUDIO_STATUS: {
			frame->eax = audio_is_ready() ? 1u : 0u;
			break;
		}
		case SYSCALL_FS_FREE_BLOCKS: {
			frame->eax = fs_get_free_blocks();
			break;
		}
		case SYSCALL_HEAP_STATS: {
			user_heap_stats_t *out = (user_heap_stats_t *)frame->ebx;
			uint32_t size = frame->ecx;
			heap_stats_t stats;

			if (!out || size < sizeof(user_heap_stats_t)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			kmalloc_get_stats(&stats);
			user_heap_stats_t temp;
			temp.total_size = (uint32_t)stats.total_size;
			temp.used_size = (uint32_t)stats.used_size;
			temp.free_size = (uint32_t)stats.free_size;
			temp.largest_free_block = (uint32_t)stats.largest_free_block;

			if (!copy_user_out(out, size, &temp, sizeof(temp))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_PROCESS_COUNT: {
			frame->eax = process_get_count();
			break;
		}
		case SYSCALL_PROCESS_LIST: {
			user_process_info_t *out = (user_process_info_t *)frame->ebx;
			uint32_t max_entries = frame->ecx;
			if (!out || max_entries == 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok_mul((uint32_t)out, max_entries, sizeof(user_process_info_t))) {
				frame->eax = (uint32_t)-1;
				break;
			}

			uint32_t cap = max_entries;
			if (cap > 32) {
				cap = 32;
			}

			process_info_t list[32];
			uint32_t count = process_list(list, cap);
			for (uint32_t i = 0; i < count; i++) {
				user_process_info_t temp;
				memset(&temp, 0, sizeof(temp));
				temp.pid = list[i].pid;
				temp.state = list[i].state;
				temp.priority = list[i].priority;
				temp.time_slice = list[i].time_slice;
				temp.total_time = list[i].total_time;
				strncpy(temp.name, list[i].name, PROCESS_NAME_MAX - 1);
				temp.name[PROCESS_NAME_MAX - 1] = '\0';
				if (!copy_user_out(&out[i], sizeof(user_process_info_t), &temp, sizeof(temp))) {
					frame->eax = (uint32_t)-1;
					break;
				}
			}

			if (frame->eax == (uint32_t)-1) {
				break;
			}
			frame->eax = count;
			break;
		}
		case SYSCALL_INSTALL_EMBEDDED: {
			char path[PROCESS_FD_PATH_MAX];
			const char *path_ptr = (const char *)frame->ebx;
			if (!path_ptr || !copy_user_string(path, sizeof(path), path_ptr)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = user_program_install_if_embedded(path) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_BRK: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			uint32_t new_end = frame->ebx;
			uint32_t out_end = 0;
			if (!process_brk(proc, new_end, &out_end)) {
				frame->eax = (uint32_t)-1;
			} else {
				frame->eax = out_end;
			}
			break;
		}
		case SYSCALL_PIPE: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			int *fds = (int *)frame->ebx;
			if (!fds || !user_range_ok((uint32_t)fds, sizeof(int) * 2)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int fd_read = -1;
			int fd_write = -1;
			for (int i = 0; i < PROCESS_MAX_FDS; i++) {
				if (!proc->fds[i].used) {
					fd_read = i;
					break;
				}
			}
			if (fd_read >= 0) {
				for (int i = fd_read + 1; i < PROCESS_MAX_FDS; i++) {
					if (!proc->fds[i].used) {
						fd_write = i;
						break;
					}
				}
			}
			if (fd_read < 0 || fd_write < 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			pipe_t *pipe = pipe_create();
			if (!pipe) {
				frame->eax = (uint32_t)-1;
				break;
			}
			process_fd_set_pipe(proc, fd_read, pipe, false);
			process_fd_set_pipe(proc, fd_write, pipe, true);
			int tmp[2] = {fd_read, fd_write};
			if (!copy_user_out(fds, sizeof(tmp), tmp, sizeof(tmp))) {
				process_fd_close(proc, fd_read);
				process_fd_close(proc, fd_write);
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_DUP2: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			int oldfd = (int)frame->ebx;
			int newfd = (int)frame->ecx;
			if (oldfd < 0 || oldfd >= PROCESS_MAX_FDS ||
			    newfd < 0 || newfd >= PROCESS_MAX_FDS) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!proc->fds[oldfd].used) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (oldfd == newfd) {
				frame->eax = (uint32_t)newfd;
				break;
			}
			process_fd_close(proc, newfd);
			proc->fds[newfd] = proc->fds[oldfd];
			proc->fds[newfd].used = true;
			if (proc->fds[newfd].type == PROCESS_FD_PIPE_READ) {
				pipe_retain_read(proc->fds[newfd].pipe);
			} else if (proc->fds[newfd].type == PROCESS_FD_PIPE_WRITE) {
				pipe_retain_write(proc->fds[newfd].pipe);
			}
			frame->eax = (uint32_t)newfd;
			break;
		}
		case SYSCALL_KILL: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			uint32_t pid = frame->ebx;
			int sig = (int)frame->ecx;
			int exit_code = 128 + sig;
			if (pid == 0 || sig <= 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (proc->pid == pid) {
				if (!process_exit_current(frame, exit_code)) {
					syscall_exit_code = exit_code;
					syscall_exit_requested = 1;
				}
				break;
			}
			frame->eax = process_kill_other(pid, exit_code) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_HALT: {
			shell_halt();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DEMO: {
			graphics_demo();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_ANIM: {
			graphics_animation_demo();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_PAINT: {
			const char *cwd = usermode_get_cwd();
			char path[PROCESS_FD_PATH_MAX];
			const char *path_ptr = (const char *)frame->ebx;

			if (path_ptr) {
				if (!copy_user_string(path, sizeof(path), path_ptr)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (path[0] != '\0') {
					graphics_paint_demo_with_dir(path);
					frame->eax = 0;
					break;
				}
			}
			if (cwd && *cwd) {
				graphics_paint_demo_with_dir(cwd);
			} else {
				graphics_paint_demo_with_dir("/");
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_GUI_DESKTOP: {
			desktop_run();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GUI: {
			desktop_run();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GUI_PAINT: {
			char path[PROCESS_FD_PATH_MAX];
			const char *path_ptr = (const char *)frame->ebx;

			if (path_ptr) {
				if (!copy_user_string(path, sizeof(path), path_ptr)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (path[0] != '\0') {
					paint_app_windowed(path);
					frame->eax = 0;
					break;
				}
			}
			paint_app_windowed(NULL);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GUI_CALC: {
			calculator_app();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GUI_FILEMGR: {
			file_manager_app();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_SET_MODE: {
			uint32_t mode = frame->ebx;
			frame->eax = graphics_set_mode((uint8_t)mode) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_GFX_GET_MODE: {
			frame->eax = graphics_get_mode();
			break;
		}
		case SYSCALL_GFX_GET_WIDTH: {
			frame->eax = (uint32_t)graphics_get_width();
			break;
		}
		case SYSCALL_GFX_GET_HEIGHT: {
			frame->eax = (uint32_t)graphics_get_height();
			break;
		}
		case SYSCALL_GFX_CLEAR: {
			uint32_t color = frame->ebx;
			graphics_clear((uint8_t)color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_PUTPIXEL: {
			user_gfx_pixel_t args;
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_putpixel(args.x, args.y, args.color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DRAW_RECT: {
			user_gfx_rect_t args;
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_draw_rect(args.x, args.y, args.width, args.height, args.color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_FILL_RECT: {
			user_gfx_rect_t args;
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_fill_rect(args.x, args.y, args.width, args.height, args.color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DRAW_LINE: {
			user_gfx_line_t args;
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_draw_line(args.x1, args.y1, args.x2, args.y2, args.color);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DRAW_CHAR: {
			user_gfx_char_t args;
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_draw_char(args.x, args.y, args.c, args.fg, args.bg);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_PRINT: {
			user_gfx_print_t args;
			char text[128];
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!copy_user_string(text, sizeof(text), args.text)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			graphics_print(args.x, args.y, text, args.fg, args.bg);
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_BLIT: {
			user_gfx_blit_t args;
			process_t *proc = process_current();
			if (!copy_user_in(&args, sizeof(args), (const void *)frame->ebx, sizeof(args))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!proc || !proc->page_directory || !args.pixels ||
			    args.width <= 0 || args.height <= 0 || args.stride <= 0 ||
			    args.stride < args.width) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok_mul((uint32_t)args.pixels, (uint32_t)args.height,
			                       (uint32_t)args.stride)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!graphics_blit_from_user(proc->page_directory, args.x, args.y, args.width,
			                             args.height, args.stride, (uint32_t)args.pixels)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_FLIP: {
			graphics_flip_buffer();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DOUBLEBUFFER_ENABLE: {
			graphics_enable_double_buffer();
			frame->eax = 0;
			break;
		}
		case SYSCALL_GFX_DOUBLEBUFFER_DISABLE: {
			graphics_disable_double_buffer();
			frame->eax = 0;
			break;
		}
		case SYSCALL_MOUSE_GET_STATE: {
			mouse_state_t state = mouse_get_state();
			if (!copy_user_out((void *)frame->ebx, sizeof(state), &state, sizeof(state))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = 0;
			break;
		}
		case SYSCALL_KEYBOARD_HAS_INPUT: {
			frame->eax = keyboard_has_input() ? 1 : 0;
			break;
		}
		case SYSCALL_KEY_REPEAT: {
			uint32_t delay = frame->ebx;
			uint32_t rate = frame->ecx;
			if (delay > 3 || rate > 31) {
				frame->eax = (uint32_t)-1;
				break;
			}
			keyboard_set_typematic((uint8_t)delay, (uint8_t)rate);
			frame->eax = 0;
			break;
		}
		case SYSCALL_EXEC: {
			if (!syscall_require_process(frame)) {
				break;
			}
			char path[PROCESS_FD_PATH_MAX];
			char args[USERMODE_MAX_ARGS];
			uint32_t args_len = frame->edx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (args_len > 0) {
				if (!frame->ecx) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (args_len >= USERMODE_MAX_ARGS) {
					args_len = USERMODE_MAX_ARGS - 1;
				}
				if (!user_range_ok(frame->ecx, args_len)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (!copy_user_in(args, args_len, (const void *)frame->ecx, args_len)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				args[args_len] = '\0';
			} else {
				args_len = 0;
				args[0] = '\0';
			}
			process_t *proc = process_current();
			if (!proc || !process_exec(proc, path, args, args_len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			process_activate(proc);
			proc->frame.eax = 0;
			uint32_t kernel_esp = frame->esp;
			memcpy(frame, &proc->frame, sizeof(*frame));
			frame->esp = kernel_esp;
			frame->eax = 0;
			break;
		}
		case SYSCALL_GETARGS: {
			process_t *proc = syscall_require_process(frame);
			if (!proc) {
				break;
			}
			char *buf = (char *)frame->ebx;
			uint32_t len = frame->ecx;
			if (len > 0 && (!buf || !user_range_ok((uint32_t)buf, len))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			char tmp[USERMODE_MAX_ARGS];
			uint32_t total = process_get_args(proc, tmp, sizeof(tmp));
			uint32_t to_copy = total;
			if (to_copy > len) {
				to_copy = len;
			}
			if (to_copy > 0) {
				if (!copy_user_out(buf, to_copy, tmp, to_copy)) {
					frame->eax = (uint32_t)-1;
					break;
				}
			}
			frame->eax = total;
			break;
		}
		case SYSCALL_EXIT:
			if (!process_exit_current(frame, (int)frame->ebx)) {
				syscall_exit_code = frame->ebx;
				syscall_exit_requested = 1;
			}
			break;
		case SYSCALL_SPAWN: {
			char path[PROCESS_FD_PATH_MAX];
			char args[USERMODE_MAX_ARGS];
			uint32_t args_len = frame->edx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (args_len > 0) {
				if (!frame->ecx) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (args_len >= USERMODE_MAX_ARGS) {
					args_len = USERMODE_MAX_ARGS - 1;
				}
				if (!user_range_ok(frame->ecx, args_len)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				if (!copy_user_in(args, args_len, (const void *)frame->ecx, args_len)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				args[args_len] = '\0';
			} else {
				args_len = 0;
				args[0] = '\0';
			}

			int pid = process_spawn(path, args, args_len);
			frame->eax = (pid >= 0) ? (uint32_t)pid : (uint32_t)-1;
			break;
		}
		case SYSCALL_FORK: {
			int pid = process_fork(frame);
			frame->eax = (pid >= 0) ? (uint32_t)pid : (uint32_t)-1;
			break;
		}
		case SYSCALL_WAIT: {
			int32_t pid = (int32_t)frame->ebx;
			uint32_t status_ptr = frame->ecx;
			if (status_ptr != 0 && !user_range_ok(status_ptr, sizeof(int))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int out_pid = -1;
			int out_status = -1;
			if (process_wait(frame, pid, status_ptr, &out_pid, &out_status)) {
				if (status_ptr != 0 && out_pid >= 0) {
					if (!copy_user_out((void *)status_ptr, sizeof(out_status),
					                   &out_status, sizeof(out_status))) {
						frame->eax = (uint32_t)-1;
						break;
					}
				}
				frame->eax = (uint32_t)out_pid;
			}
			break;
		}
		default:
			frame->eax = (uint32_t)-1;
			break;
	}
}

void syscall_reset_exit(void) {
	syscall_exit_requested = 0;
	syscall_exit_code = 0;
}

uint32_t syscall_exit_status(void) {
	return syscall_exit_code;
}
