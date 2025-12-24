#include <kernel/syscall.h>
#include <kernel/tty.h>
#include <kernel/fs.h>
#include <kernel/usermode.h>
#include <kernel/elf.h>
#include <kernel/shell.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <kernel/io.h>
#include <kernel/desktop.h>
#include <kernel/graphics.h>
#include <kernel/graphics_demo.h>
#include <kernel/paint.h>
#include <kernel/calculator.h>
#include <kernel/file_manager.h>
#include <kernel/mouse.h>
#include <string.h>

volatile uint32_t syscall_exit_requested = 0;
volatile uint32_t syscall_exit_code = 0;
volatile uint32_t usermode_return_esp = 0;
volatile uint32_t usermode_saved_ebx = 0;
volatile uint32_t usermode_saved_esi = 0;
volatile uint32_t usermode_saved_edi = 0;
volatile uint32_t usermode_saved_ebp = 0;
volatile uint32_t usermode_abort_requested = 0;

#define MAX_FDS 16
#define FD_PATH_MAX 128
#define ALIAS_NAME_MAX 32
#define ALIAS_CMD_MAX 256

typedef struct {
	bool used;
	char path[FD_PATH_MAX];
	uint32_t offset;
} fd_entry_t;

typedef struct {
	uint32_t size;
	uint32_t type;
} user_stat_t;

typedef struct {
	char name[FS_MAX_FILENAME];
	uint32_t type;
	uint32_t size;
} user_dirent_t;

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

static fd_entry_t fd_table[MAX_FDS];
static bool fd_table_initialized = false;

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
	return true;
}

static bool copy_user_out(void *dst, uint32_t dst_len, const void *src, uint32_t src_len) {
	if (!dst || !user_range_ok((uint32_t)dst, dst_len)) {
		return false;
	}
	memcpy(dst, src, src_len);
	return true;
}

static bool copy_user_in(void *dst, uint32_t dst_len, const void *src, uint32_t src_len) {
	if (!src || !user_range_ok((uint32_t)src, src_len)) {
		return false;
	}
	memcpy(dst, src, src_len);
	return true;
}

static void fd_table_reset(void) {
	for (int i = 0; i < MAX_FDS; i++) {
		fd_table[i].used = false;
		fd_table[i].offset = 0;
		fd_table[i].path[0] = '\0';
	}
	fd_table_initialized = true;
}

static bool copy_user_string(char *dst, size_t dst_size, const char *user_ptr) {
	if (!dst || dst_size == 0 || !user_ptr) {
		return false;
	}

	for (size_t i = 0; i < dst_size - 1; i++) {
		uint32_t addr = (uint32_t)(user_ptr + i);
		if (!user_range_ok(addr, 1)) {
			return false;
		}
		char c = *(const char *)addr;
		dst[i] = c;
		if (c == '\0') {
			return true;
		}
	}

	dst[dst_size - 1] = '\0';
	return true;
}

void syscall_dispatch(syscall_frame_t *frame) {
	switch (frame->eax) {
		case SYSCALL_WRITE: {
			const char *buf = (const char *)frame->ebx;
			uint32_t len = frame->ecx;
			if (!buf || len == 0) {
				frame->eax = 0;
				break;
			}
			if (!user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			terminal_write(buf, len);
			frame->eax = len;
			break;
		}
		case SYSCALL_OPEN: {
			if (!fd_table_initialized) {
				fd_table_reset();
			}
			char path[FD_PATH_MAX];
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
			for (int i = 0; i < MAX_FDS; i++) {
				if (!fd_table[i].used) {
					fd = i;
					fd_table[i].used = true;
					fd_table[i].offset = 0;
					strncpy(fd_table[i].path, path, sizeof(fd_table[i].path) - 1);
					fd_table[i].path[sizeof(fd_table[i].path) - 1] = '\0';
					break;
				}
			}
			frame->eax = (fd >= 0) ? (uint32_t)fd : (uint32_t)-1;
			break;
		}
		case SYSCALL_READ: {
			uint32_t fd = frame->ebx;
			uint8_t *buf = (uint8_t *)frame->ecx;
			uint32_t len = frame->edx;
			if (fd >= MAX_FDS || !fd_table[fd].used || len == 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			if (!user_range_ok((uint32_t)buf, len)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int read = fs_read_file(fd_table[fd].path, buf, len, fd_table[fd].offset);
			if (read < 0) {
				frame->eax = (uint32_t)-1;
				break;
			}
			fd_table[fd].offset += (uint32_t)read;
			frame->eax = (uint32_t)read;
			break;
		}
		case SYSCALL_CLOSE: {
			uint32_t fd = frame->ebx;
			if (fd >= MAX_FDS || !fd_table[fd].used) {
				frame->eax = (uint32_t)-1;
				break;
			}
			fd_table[fd].used = false;
			fd_table[fd].path[0] = '\0';
			fd_table[fd].offset = 0;
			frame->eax = 0;
			break;
		}
		case SYSCALL_STAT: {
			char path[FD_PATH_MAX];
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
			out->size = inode.size;
			out->type = inode.type;
			frame->eax = 0;
			break;
		}
		case SYSCALL_SEEK: {
			uint32_t fd = frame->ebx;
			int32_t offset = (int32_t)frame->ecx;
			uint32_t whence = frame->edx;

			if (fd >= MAX_FDS || !fd_table[fd].used) {
				frame->eax = (uint32_t)-1;
				break;
			}

			fs_inode_t inode;
			if (!fs_stat(fd_table[fd].path, &inode)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			int32_t base = 0;
			if (whence == 1) {
				base = (int32_t)fd_table[fd].offset;
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

			fd_table[fd].offset = (uint32_t)new_off;
			frame->eax = (uint32_t)new_off;
			break;
		}
		case SYSCALL_LISTDIR: {
			char path[FD_PATH_MAX];
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

			if (!user_range_ok((uint32_t)out, max_entries * sizeof(user_dirent_t))) {
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

				char entry_path[FD_PATH_MAX];
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
			char path[FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int res = fs_create_dir(path);
			frame->eax = (res >= 0 || res == -2) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_RM: {
			char path[FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			frame->eax = fs_delete(path) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_TOUCH: {
			char path[FD_PATH_MAX];
			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}
			int res = fs_create_file(path);
			frame->eax = (res >= 0 || res == -2) ? 0 : (uint32_t)-1;
			break;
		}
		case SYSCALL_RENAME: {
			char old_path[FD_PATH_MAX];
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
			memcpy(buf, cwd, needed);
			frame->eax = needed;
			break;
		}
		case SYSCALL_SETCWD: {
			char path[FD_PATH_MAX];
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
			char path[FD_PATH_MAX];
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

			int written = fs_write_file(path, buf, len, 0);
			frame->eax = (written >= 0) ? (uint32_t)written : (uint32_t)-1;
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
			memcpy(buf, entry, entry_len);
			buf[entry_len] = '\0';
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
			timer_sleep_ms(frame->ebx);
			frame->eax = 0;
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

			memcpy(name, tmp_name, sizeof(tmp_name));
			memcpy(cmd, tmp_cmd, sizeof(tmp_cmd));
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
			unsigned char tmp = inb(0x61);
			outb(0x61, tmp | 0x03);
			for (volatile int i = 0; i < 1000000; i++) {
			}
			outb(0x61, tmp);
			frame->eax = 0;
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
			char path[FD_PATH_MAX];
			const char *path_ptr = (const char *)frame->ebx;

			if (path_ptr && copy_user_string(path, sizeof(path), path_ptr) && path[0] != '\0') {
				graphics_paint_demo_with_dir(path);
			} else if (cwd && *cwd) {
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
			char path[FD_PATH_MAX];
			const char *path_ptr = (const char *)frame->ebx;

			if (path_ptr && copy_user_string(path, sizeof(path), path_ptr) && path[0] != '\0') {
				paint_app_windowed(path);
			} else {
				paint_app_windowed(NULL);
			}
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
		case SYSCALL_EXEC: {
			if (!fd_table_initialized) {
				fd_table_reset();
			}
			char path[FD_PATH_MAX];
			char args[USERMODE_MAX_ARGS];
			uint32_t args_len = frame->edx;

			if (!copy_user_string(path, sizeof(path), (const char *)frame->ebx)) {
				frame->eax = (uint32_t)-1;
				break;
			}

			if (frame->ecx && args_len > 0) {
				if (args_len >= USERMODE_MAX_ARGS) {
					args_len = USERMODE_MAX_ARGS - 1;
				}
				if (!user_range_ok(frame->ecx, args_len)) {
					frame->eax = (uint32_t)-1;
					break;
				}
				memcpy(args, (const void *)frame->ecx, args_len);
				args[args_len] = '\0';
			} else {
				args_len = 0;
				args[0] = '\0';
			}

			usermode_request_exec(path, args, args_len);
			syscall_exit_requested = 1;
			frame->eax = 0;
			break;
		}
		case SYSCALL_GETARGS: {
			char *buf = (char *)frame->ebx;
			uint32_t len = frame->ecx;
			if (len > 0 && (!buf || !user_range_ok((uint32_t)buf, len))) {
				frame->eax = (uint32_t)-1;
				break;
			}
			uint32_t total = usermode_get_args(buf, len);
			frame->eax = total;
			break;
		}
		case SYSCALL_EXIT:
			syscall_exit_code = frame->ebx;
			syscall_exit_requested = 1;
			break;
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
