#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/cpu.h>
#include <kernel/editor.h>
#include <kernel/mouse.h>
#include <kernel/snake.h>
#include <kernel/graphics.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/ata.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/pagings.h>
#include <kernel/net.h>
#include <kernel/usermode.h>
#include <kernel/user_programs.h>
#include <kernel/process.h>

// Path length constant (previously from vfs.h)
#define MAX_PATH_LEN 512


#define MAX_COMMAND_LENGTH 256
#define UDP_SHELL_MAX 512
#define PIPELINE_MAX 6

enum vga_color {
	VGA_COLOR_BLACK = 0, VGA_COLOR_BLUE = 1, VGA_COLOR_GREEN = 2, VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4, VGA_COLOR_MAGENTA = 5, VGA_COLOR_BROWN = 6, VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8, VGA_COLOR_LIGHT_BLUE = 9, VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11, VGA_COLOR_LIGHT_RED = 12, VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14, VGA_COLOR_WHITE = 15
};

static inline unsigned char vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | (bg << 4);
}

static unsigned int command_count = 0;
static unsigned int tick_count = 0;
static bool timer_running = false;
static unsigned int timer_start = 0;

// Command history
#define HISTORY_SIZE 10
static char history_buffer[HISTORY_SIZE][MAX_COMMAND_LENGTH];
static int history_count = 0;
static int history_index = 0;

// Command aliases
#define MAX_ALIASES 10
static char alias_names[MAX_ALIASES][32];
static char alias_commands[MAX_ALIASES][MAX_COMMAND_LENGTH];
static int alias_count = 0;

// Current working directory (for disk filesystem)
static char current_dir_path[256] = "/";

static int strcmp_local(const char* s1, const char* s2);
static void strcpy_local(char* dest, const char* src);

void shell_set_cwd(const char *path) {
	if (!path || path[0] == '\0') {
		return;
	}
	strncpy(current_dir_path, path, sizeof(current_dir_path) - 1);
	current_dir_path[sizeof(current_dir_path) - 1] = '\0';
	usermode_set_cwd(current_dir_path);
}

int shell_history_count(void) {
	return history_count;
}

const char *shell_history_entry(int index) {
	if (index < 0 || index >= history_count) {
		return NULL;
	}
	return history_buffer[index];
}

unsigned int shell_command_count(void) {
	return command_count;
}

unsigned int shell_tick_count(void) {
	return tick_count;
}

int shell_timer_start(void) {
	if (timer_running) {
		return -1;
	}
	timer_start = timer_get_ticks();
	timer_running = true;
	return 0;
}

int shell_timer_stop(unsigned int *elapsed) {
	if (!timer_running) {
		return -1;
	}
	unsigned int delta = timer_get_ticks() - timer_start;
	timer_running = false;
	if (elapsed) {
		*elapsed = delta;
	}
	return 0;
}

int shell_timer_status(void) {
	return timer_running ? 1 : 0;
}

int shell_alias_set(const char *name, const char *cmd) {
	if (!name || !cmd || !*name) {
		return -1;
	}
	if (alias_count >= MAX_ALIASES) {
		return -1;
	}
	size_t name_len = strlen(name);
	size_t cmd_len = strlen(cmd);
	if (name_len >= sizeof(alias_names[0]) || cmd_len >= sizeof(alias_commands[0])) {
		return -1;
	}
	strcpy_local(alias_names[alias_count], name);
	strcpy_local(alias_commands[alias_count], cmd);
	alias_count++;
	return 0;
}

int shell_alias_remove(const char *name) {
	if (!name || !*name) {
		return -1;
	}
	for (int i = 0; i < alias_count; i++) {
		if (strcmp_local(alias_names[i], name) == 0) {
			for (int j = i; j < alias_count - 1; j++) {
				strcpy_local(alias_names[j], alias_names[j + 1]);
				strcpy_local(alias_commands[j], alias_commands[j + 1]);
			}
			alias_count--;
			return 0;
		}
	}
	return -1;
}

int shell_alias_count(void) {
	return alias_count;
}

int shell_alias_get(int index, char *name, size_t name_len, char *cmd, size_t cmd_len) {
	if (index < 0 || index >= alias_count || !name || !cmd || name_len == 0 || cmd_len == 0) {
		return -1;
	}
	strncpy(name, alias_names[index], name_len - 1);
	name[name_len - 1] = '\0';
	strncpy(cmd, alias_commands[index], cmd_len - 1);
	cmd[cmd_len - 1] = '\0';
	return 0;
}

void shell_halt(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("\n========================================\n");
	printf("     System Halted - Goodbye!    \n");
	printf("========================================\n\n");
	terminal_setcolor(old_color);

	printf("Total commands executed: %u\n", command_count);
	printf("Total shell cycles: %u\n\n", tick_count);

	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
	printf("It is now safe to turn off your computer.\n\n");
	terminal_setcolor(old_color);

	while (true) {
		__asm__ volatile ("cli; hlt");
	}
}

// Command handler types
typedef void (*command_handler_t)(void);
typedef void (*command_handler_arg_t)(const char*);

// Command table entry
typedef struct {
	const char* name;
	command_handler_t handler;
	command_handler_arg_t handler_with_arg;
	bool requires_arg;
} command_entry_t;

static const char *builtin_commands[] = {
	"help",
	"display",
	"edit",
	"mem",
	"dma",
	"netinfo",
	"arp",
	"ping",
	"udpsend",
	"udplisten",
	"snake",
	"cpuinfo",
	"rdtsc",
	"regs",
	"benchmark",
	"ps",
	"kill",
	"spawn",
	"stacktest",
	"diskfmt",
	"diskmount",
	"diskls",
	"diskwrite",
	"diskread",
};
static const size_t builtin_command_count =
	sizeof(builtin_commands) / sizeof(builtin_commands[0]);

// Forward declarations
static void output_prompt(void);
static void input_line(char* buffer, size_t max_length);
static void execute_command(const char* command);
static bool execute_pipeline(const char* command);
static bool split_command(const char *command, char *name, size_t name_len, const char **args_out);
static bool run_user_program(const char *name, const char *args);
static bool resolve_user_program_path(const char *name, char *out, size_t out_size);
static void resolve_run_path(char *out, size_t out_size, const char *path);
static void command_help(const char* args);
static void command_memory(const char* args);
static void command_netinfo(void);
static void command_arp(void);
static void command_ping(const char* args);
static void command_udpsend(const char* args);
static void command_udplisten(const char* args);
static void command_snake(void);
static void command_cpuinfo(void);
static void command_rdtsc(void);
static void command_regs(void);
static void command_benchmark(void);
static void command_display(const char* args);
static void command_edit(const char* args);
static void command_ps(void);
static void command_tasks(void);
static void command_kill(const char* args);
static void command_killtask(const char* args);
static void command_spawn(const char* args);
static void command_stacktest(void);
static void command_dma(const char* args);
static void command_diskfmt(const char* args);
static void command_diskmount(const char* args);
static void command_diskls(const char* args);
static void command_diskwrite(const char* args);
static void command_diskread(const char* args);
static bool is_builtin_command(const char *name);

static int strcmp_local(const char* s1, const char* s2) {
	while (*s1 && (*s1 == *s2)) { s1++; s2++; }
	return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int strncmp_local(const char* s1, const char* s2, size_t n) {
	for (size_t i = 0; i < n; i++) {
		unsigned char c1 = (unsigned char)s1[i];
		unsigned char c2 = (unsigned char)s2[i];
		if (c1 != c2) {
			return c1 - c2;
		}
		if (c1 == '\0') {
			return 0;
		}
	}
	return 0;
}

static void strcpy_local(char* dest, const char* src) {
	while (*src) {
		*dest++ = *src++;
	}
	*dest = '\0';
}

static bool is_builtin_command(const char *name) {
	if (!name || !*name) {
		return false;
	}
	for (size_t i = 0; i < builtin_command_count; i++) {
		if (strcmp_local(name, builtin_commands[i]) == 0) {
			return true;
		}
	}
	return false;
}

static unsigned int parse_hex(const char* str) {
	unsigned int result = 0;
	if (*str == '0' && (*(str+1) == 'x' || *(str+1) == 'X')) str += 2;
	while (*str) {
		char c = *str++;
		if (c >= '0' && c <= '9') result = (result << 4) | (c - '0');
		else if (c >= 'a' && c <= 'f') result = (result << 4) | (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') result = (result << 4) | (c - 'A' + 10);
		else break;
	}
	return result;
}

static void print_ipv4(const uint8_t ip[4]) {
	printf("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static bool parse_ipv4(const char *str, uint8_t out[4], const char **rest) {
	int part = 0;
	unsigned int value = 0;
	bool has_digit = false;

	if (!str || !out) {
		return false;
	}

	while (*str && *str != ' ') {
		char c = *str;
		if (c >= '0' && c <= '9') {
			has_digit = true;
			value = value * 10 + (unsigned int)(c - '0');
			if (value > 255) {
				return false;
			}
		} else if (c == '.') {
			if (!has_digit || part >= 3) {
				return false;
			}
			out[part++] = (uint8_t)value;
			value = 0;
			has_digit = false;
		} else {
			return false;
		}
		str++;
	}

	if (!has_digit || part != 3) {
		return false;
	}
	out[part] = (uint8_t)value;
	if (rest) {
		*rest = str;
	}
	return true;
}

static bool parse_uint(const char *str, unsigned int *out, const char **rest) {
	unsigned int value = 0;
	bool has_digit = false;

	if (!str || !out) {
		return false;
	}

	while (*str >= '0' && *str <= '9') {
		has_digit = true;
		value = value * 10 + (unsigned int)(*str - '0');
		str++;
	}

	if (!has_digit) {
		return false;
	}

	*out = value;
	if (rest) {
		*rest = str;
	}
	return true;
}

static bool split_command(const char *command, char *name, size_t name_len, const char **args_out) {
	if (!command || !name || name_len == 0) {
		return false;
	}
	const char *cursor = command;
	while (*cursor == ' ') {
		cursor++;
	}
	if (*cursor == '\0') {
		return false;
	}
	size_t i = 0;
	while (*cursor && *cursor != ' ' && i + 1 < name_len) {
		name[i++] = *cursor++;
	}
	name[i] = '\0';
	while (*cursor && *cursor != ' ') {
		cursor++;
	}
	while (*cursor == ' ') {
		cursor++;
	}
	if (args_out) {
		*args_out = cursor;
	}
	return true;
}

static bool ensure_user_program_available(const char *path) {
	fs_inode_t inode;
	if (fs_stat(path, &inode) && inode.type == 1) {
		return true;
	}
	return user_program_install_if_embedded(path);
}

static bool resolve_user_program_path(const char *name, char *out, size_t out_size) {
	if (!name || !*name || !out || out_size == 0) {
		return false;
	}
	out[0] = '\0';

	bool has_slash = false;
	for (const char *p = name; *p; p++) {
		if (*p == '/') {
			has_slash = true;
			break;
		}
	}

	size_t name_len = strlen(name);
	bool has_elf = name_len > 4 && strcmp(name + name_len - 4, ".elf") == 0;

	if (has_slash) {
		resolve_run_path(out, out_size, name);
		return ensure_user_program_available(out);
	}

	if (has_elf) {
		snprintf(out, out_size, "/bin/%s", name);
		if (ensure_user_program_available(out)) {
			return true;
		}
		resolve_run_path(out, out_size, name);
		return ensure_user_program_available(out);
	}

	snprintf(out, out_size, "/bin/%s.elf", name);
	if (ensure_user_program_available(out)) {
		return true;
	}

	resolve_run_path(out, out_size, name);
	if (ensure_user_program_available(out)) {
		return true;
	}

	snprintf(out, out_size, "/bin/%s", name);
	return ensure_user_program_available(out);
}

static bool run_user_attempt(const char *path, const char *args) {
	if (!ensure_user_program_available(path)) {
		return false;
	}
	uint32_t exit_code = usermode_last_exit_code();
	if (!usermode_run_elf_with_args(path, args)) {
		if (exit_code != 0) {
			if (exit_code >= 128) {
				printf("User program crashed (exception %u)\n", exit_code - 128);
			}
			return true;
		}
		return false;
	}
	exit_code = usermode_last_exit_code();
	if (exit_code >= 128) {
		printf("User program crashed (exception %u)\n", exit_code - 128);
	}
	return true;
}

static bool run_user_program(const char *name, const char *args) {
	if (!name || !*name) {
		return false;
	}
	if (!args) {
		args = "";
	}

	char resolved[MAX_PATH_LEN];
	bool has_slash = false;
	for (const char *p = name; *p; p++) {
		if (*p == '/') {
			has_slash = true;
			break;
		}
	}

	size_t name_len = strlen(name);
	bool has_elf = name_len > 4 && strcmp(name + name_len - 4, ".elf") == 0;

	if (has_slash) {
		resolve_run_path(resolved, sizeof(resolved), name);
		return run_user_attempt(resolved, args);
	}

	if (has_elf) {
		snprintf(resolved, sizeof(resolved), "/bin/%s", name);
		if (run_user_attempt(resolved, args)) {
			return true;
		}
		resolve_run_path(resolved, sizeof(resolved), name);
		return run_user_attempt(resolved, args);
	}

	snprintf(resolved, sizeof(resolved), "/bin/%s.elf", name);
	if (run_user_attempt(resolved, args)) {
		return true;
	}

	resolve_run_path(resolved, sizeof(resolved), name);
	if (run_user_attempt(resolved, args)) {
		return true;
	}

	snprintf(resolved, sizeof(resolved), "/bin/%s", name);
	return run_user_attempt(resolved, args);
}

static void resolve_run_path(char *out, size_t out_size, const char *path) {
	if (!out || out_size == 0) {
		return;
	}
	out[0] = '\0';

	if (!path || *path == '\0') {
		return;
	}

	if (path[0] == '/') {
		strncpy(out, path, out_size - 1);
		out[out_size - 1] = '\0';
		return;
	}

	if (strcmp(current_dir_path, "/") == 0) {
		snprintf(out, out_size, "/%s", path);
	} else {
		snprintf(out, out_size, "%s/%s", current_dir_path, path);
	}
}

void shell_init(void) {
	char command[MAX_COMMAND_LENGTH];
	
	// Initialize current directory to root
	shell_set_cwd("/");
	
	if (!run_user_program("banner", "")) {
		printf("\nRohanOS\nType 'help' for commands.\n\n");
	}
	
	while (true) {
		tick_count++;
		output_prompt();
		input_line(command, MAX_COMMAND_LENGTH);
		if (strlen(command) > 0) {
			command_count++;
			
			// Add to history
			if (history_count < HISTORY_SIZE) {
				strcpy_local(history_buffer[history_count], command);
				history_count++;
			} else {
				// Shift history and add new command
				for (int i = 0; i < HISTORY_SIZE - 1; i++) {
					strcpy_local(history_buffer[i], history_buffer[i + 1]);
				}
				strcpy_local(history_buffer[HISTORY_SIZE - 1], command);
			}
			history_index = history_count; // Reset to end of history
			
			execute_command(command);
		}
	}
}

static void output_prompt(void) {
	printf("myos:%s> ", current_dir_path);
}

static void input_line(char* buffer, size_t max_length) {
	size_t pos = 0;  // Length of buffer
	size_t cursor_pos = 0;  // Cursor position in buffer
	static int8_t last_scroll = 0;
	
	// Store the starting position of input (after the prompt)
	size_t start_row = terminal_get_row();
	size_t start_col = terminal_get_column();
	
	while (true) {
		// Check for mouse scroll events
		mouse_state_t mouse = mouse_get_state();
		if (mouse.scroll != last_scroll) {
			if (mouse.scroll < 0) {
				terminal_scroll_up();
			} else if (mouse.scroll > 0) {
				terminal_scroll_down();
			}
			last_scroll = mouse.scroll;
		}
		
		if (!keyboard_has_input()) {
			__asm__ volatile ("hlt");
			continue;
		}
		
		unsigned char c = keyboard_getchar();
		if (c == '\n') {
			buffer[pos] = '\0';
			printf("\n");
			return;
		} else if (c == '\t') {
			// Tab completion (builtins + /bin)
			buffer[pos] = '\0';
			char match_buf[MAX_COMMAND_LENGTH];
			size_t match_len = 0;
			int matches = 0;

			for (size_t i = 0; i < builtin_command_count; i++) {
				const char *name = builtin_commands[i];
				size_t name_len = strlen(name);
				if (name_len < pos) {
					continue;
				}
				if (strncmp_local(buffer, name, pos) == 0) {
					matches++;
					if (matches == 1) {
						if (name_len >= sizeof(match_buf)) {
							name_len = sizeof(match_buf) - 1;
						}
						memcpy(match_buf, name, name_len);
						match_buf[name_len] = '\0';
						match_len = name_len;
					}
				}
			}

			fs_context_t *fs = fs_get_context();
			if (fs && fs->mounted) {
				fs_dirent_t entries[64];
				int count = fs_list_dir("/bin", entries, 64);
				if (count > 0) {
					for (int i = 0; i < count; i++) {
						const char *name = entries[i].name;
						size_t name_len = strlen(name);
						if (name_len <= 4 || strcmp(name + name_len - 4, ".elf") != 0) {
							continue;
						}
						size_t trimmed_len = name_len - 4;
						if (trimmed_len < pos) {
							continue;
						}
						if (strncmp_local(buffer, name, pos) == 0) {
							matches++;
							if (matches == 1) {
								if (trimmed_len >= sizeof(match_buf)) {
									trimmed_len = sizeof(match_buf) - 1;
								}
								memcpy(match_buf, name, trimmed_len);
								match_buf[trimmed_len] = '\0';
								match_len = trimmed_len;
							}
						}
					}
				}
			}

			if (matches == 1 && match_len > pos) {
				while (pos < match_len) {
					buffer[pos] = match_buf[pos];
					printf("%c", buffer[pos]);
					pos++;
				}
				cursor_pos = pos;
			}
		} else if (c == '\b') {
			if (cursor_pos > 0) {
				// Delete character before cursor
				cursor_pos--;
				// Shift characters after cursor left
				for (size_t i = cursor_pos; i < pos; i++) {
					buffer[i] = buffer[i + 1];
				}
				pos--;
				
				// Redraw from cursor to end
				printf("\b");
				for (size_t i = cursor_pos; i < pos; i++) {
					printf("%c", buffer[i]);
				}
				printf(" \b");
				// Move cursor back to correct position
				for (size_t i = cursor_pos; i < pos; i++) {
					printf("\b");
				}
			}
		} else if (c == 0x80) { // Up arrow - previous history
			if (history_count > 0) {
				if (history_index > 0) {
					history_index--;
				} else {
					history_index = history_count - 1;
				}
				// Clear current line
				for (size_t i = 0; i < pos; i++) printf("\b \b");
				// Copy history to buffer
				strcpy_local(buffer, history_buffer[history_index]);
				pos = strlen(buffer);
				cursor_pos = pos;
				printf("%s", buffer);
			}
		} else if (c == 0x81) { // Down arrow - next history
			if (history_count > 0) {
				history_index = (history_index + 1) % history_count;
				// Clear current line
				for (size_t i = 0; i < pos; i++) printf("\b \b");
				// Copy history to buffer
				strcpy_local(buffer, history_buffer[history_index]);
				pos = strlen(buffer);
				cursor_pos = pos;
				printf("%s", buffer);
			}
		} else if (c == 0x82) { // Left arrow
			if (cursor_pos > 0) {
				cursor_pos--;
				// Calculate absolute cursor position
				size_t abs_col = start_col + cursor_pos;
				size_t abs_row = start_row + (abs_col / terminal_get_width());
				abs_col = abs_col % terminal_get_width();
				terminal_update_cursor(abs_col, abs_row);
			}
		} else if (c == 0x83) { // Right arrow
			if (cursor_pos < pos) {
				cursor_pos++;
				// Calculate absolute cursor position
				size_t abs_col = start_col + cursor_pos;
				size_t abs_row = start_row + (abs_col / terminal_get_width());
				abs_col = abs_col % terminal_get_width();
				terminal_update_cursor(abs_col, abs_row);
			}
		} else if (c >= 32 && c < 127 && pos < max_length - 1) {
			// Insert character at cursor position
			// Shift characters after cursor right
			for (size_t i = pos; i > cursor_pos; i--) {
				buffer[i] = buffer[i - 1];
			}
			buffer[cursor_pos] = c;
			pos++;
			
			// Redraw from cursor to end
			for (size_t i = cursor_pos; i < pos; i++) {
				printf("%c", buffer[i]);
			}
			cursor_pos++;
			// Move cursor back to correct position
			for (size_t i = cursor_pos; i < pos; i++) {
				printf("\b");
			}
		}
	}
}

static bool execute_pipeline(const char* command) {
	if (!command || !strchr(command, '|')) {
		return false;
	}

	char buffer[MAX_COMMAND_LENGTH];
	strncpy(buffer, command, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	char *segments[PIPELINE_MAX];
	int count = 0;
	char *cursor = buffer;
	while (*cursor && count < PIPELINE_MAX) {
		while (*cursor == ' ') {
			cursor++;
		}
		if (*cursor == '\0') {
			break;
		}
		segments[count++] = cursor;
		char *bar = strchr(cursor, '|');
		if (!bar) {
			break;
		}
		*bar = '\0';
		cursor = bar + 1;
	}
	if (count == PIPELINE_MAX && strchr(cursor, '|')) {
		printf("Pipeline too long.\n");
		return true;
	}

	for (int i = 0; i < count; i++) {
		char *seg = segments[i];
		while (*seg == ' ') {
			seg++;
		}
		size_t len = strlen(seg);
		while (len > 0 && seg[len - 1] == ' ') {
			seg[--len] = '\0';
		}
		if (len == 0) {
			printf("Invalid pipeline segment.\n");
			return true;
		}
		segments[i] = seg;
	}

	if (count < 2) {
		return false;
	}

	char names[PIPELINE_MAX][MAX_COMMAND_LENGTH];
	const char *args[PIPELINE_MAX];
	char paths[PIPELINE_MAX][MAX_PATH_LEN];
	for (int i = 0; i < count; i++) {
		if (!split_command(segments[i], names[i], sizeof(names[i]), &args[i])) {
			printf("Invalid pipeline command.\n");
			return true;
		}
		if (is_builtin_command(names[i])) {
			printf("Pipelines only support user programs.\n");
			return true;
		}
		if (!resolve_user_program_path(names[i], paths[i], sizeof(paths[i]))) {
			printf("Unknown command in pipeline: %s\n", names[i]);
			return true;
		}
	}

	process_t *procs[PIPELINE_MAX] = {0};
	for (int i = 0; i < count; i++) {
		uint32_t args_len = (uint32_t)strlen(args[i]);
		procs[i] = process_spawn_proc(paths[i], args[i], args_len);
		if (!procs[i]) {
			printf("Failed to spawn pipeline command: %s\n", names[i]);
			for (int j = 0; j < i; j++) {
				process_kill_other(procs[j]->pid, 1);
			}
			return true;
		}
	}

	pipe_t *pipes[PIPELINE_MAX - 1] = {0};
	for (int i = 0; i < count - 1; i++) {
		pipes[i] = pipe_create();
		if (!pipes[i]) {
			printf("Failed to allocate pipe.\n");
			for (int k = 0; k < i; k++) {
				if (pipes[k]) {
					pipe_release_read(pipes[k]);
				}
			}
			for (int j = 0; j < count; j++) {
				if (procs[j]) {
					process_kill_other(procs[j]->pid, 1);
				}
			}
			return true;
		}
	}

	for (int i = 0; i < count; i++) {
		if (i > 0) {
			process_fd_set_pipe(procs[i], 0, pipes[i - 1], false);
		}
		if (i < count - 1) {
			process_fd_set_pipe(procs[i], 1, pipes[i], true);
		}
	}

	if (!usermode_run_ready()) {
		printf("Failed to run pipeline.\n");
	}
	return true;
}

static void execute_command(const char* command) {
	// Check aliases first
	for (int i = 0; i < alias_count; i++) {
		if (strcmp_local(command, alias_names[i]) == 0) {
			execute_command(alias_commands[i]);
			return;
		}
	}

	if (execute_pipeline(command)) {
		return;
	}
	
	static const command_entry_t command_table[] = {
		{"help", NULL, command_help, true},
		{"display", NULL, command_display, true},
		{"edit", NULL, command_edit, true},
		{"mem", NULL, command_memory, true},
		{"dma", NULL, command_dma, true},
		{"netinfo", command_netinfo, NULL, false},
		{"arp", command_arp, NULL, false},
		{"ping", NULL, command_ping, true},
		{"udpsend", NULL, command_udpsend, true},
		{"udplisten", NULL, command_udplisten, true},
		{"snake", command_snake, NULL, false},
		{"cpuinfo", command_cpuinfo, NULL, false},
		{"rdtsc", command_rdtsc, NULL, false},
		{"regs", command_regs, NULL, false},
		{"benchmark", command_benchmark, NULL, false},
		{"ps", command_ps, NULL, false},
		{"tasks", command_tasks, NULL, false},
		{"kill", NULL, command_kill, true},
		{"killtask", NULL, command_killtask, true},
		{"spawn", NULL, command_spawn, true},
		{"stacktest", command_stacktest, NULL, false},
		{"diskfmt", NULL, command_diskfmt, true},
		{"diskmount", NULL, command_diskmount, true},
		{"diskls", NULL, command_diskls, true},
		{"diskwrite", NULL, command_diskwrite, true},
		{"diskread", NULL, command_diskread, true},
	};

	char name[MAX_COMMAND_LENGTH];
	const char *args = "";
	if (!split_command(command, name, sizeof(name), &args)) {
		return;
	}

	const int num_commands = sizeof(command_table) / sizeof(command_entry_t);
	for (int i = 0; i < num_commands; i++) {
		const command_entry_t* cmd = &command_table[i];
		if (strcmp_local(name, cmd->name) != 0) {
			continue;
		}
		if (cmd->requires_arg) {
			cmd->handler_with_arg(args);
		} else {
			cmd->handler();
		}
		return;
	}

	if (run_user_program(name, args)) {
		return;
	}

	printf("Unknown command: %s\n", name);
	printf("Type 'help' for available commands.\n");
}

static void command_help(const char* args) {
	bool kernel_only = false;
	bool ran_user_help = false;
	if (args) {
		while (*args == ' ') {
			args++;
		}
		if (strcmp_local(args, "kernel") == 0) {
			kernel_only = true;
		}
	}

	if (!kernel_only) {
		ran_user_help = run_user_program("help", args ? args : "");
		if (!ran_user_help) {
			printf("User-mode help not available.\n\n");
		}
	}

	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n=== Kernel Commands ===\n");
	terminal_setcolor(old_color);
	printf("\n");
	printf("  display <mode>   - Set display mode or show info\n");
	printf("  edit <file>      - Text editor\n");
	printf("  mem [addr|heap]  - Heap stats or memory dump\n");
	printf("  dma <on|off|toggle|status> - Toggle ATA DMA (saved to /etc/boot.cfg)\n");
	printf("  netinfo          - Show network configuration\n");
	printf("  arp              - Show ARP table\n");
	printf("  ping <ip>        - Send ICMP echo request\n");
	printf("  udpsend <ip> <port> <text> - Send UDP payload\n");
	printf("  udplisten <port> [timeout_ms] - Wait for UDP packet\n");
	printf("  snake            - Play Snake (kernel demo)\n");
	printf("  cpuinfo          - Detailed CPU info\n");
	printf("  rdtsc            - Read timestamp counter\n");
	printf("  regs             - Show control registers\n");
	printf("  benchmark        - CPU benchmark\n");
	printf("  ps               - List user processes\n");
	printf("  tasks            - List kernel threads\n");
	printf("  kill <pid>       - Terminate user process\n");
	printf("  killtask <tid>   - Terminate kernel thread\n");
	printf("  spawn <demo>     - Spawn demo kernel thread (demo1|demo2|demo3)\n");
	printf("  stacktest        - Trigger kernel stack overflow (guard page)\n");
	printf("  fault            - Trigger user-mode page fault test\n");
	printf("  diskfmt <n>      - Format drive (0-3)\n");
	printf("  diskmount <n>    - Mount drive (0-3)\n");
	printf("  diskls           - List files on disk\n");
	printf("  diskwrite <f> <text> - Write file to disk\n");
	printf("  diskread <f>     - Read file from disk\n");
	printf("\nTip: use \"help kernel\" to skip user-mode help.\n\n");
	printf("Pipelines: user programs can be chained with '|'.\n\n");
}

static void command_memory(const char* args) {
	while (*args == ' ') args++;
	
	// If no args or "heap", show heap statistics
	if (*args == '\0' || strcmp(args, "heap") == 0) {
		unsigned char old_color = terminal_getcolor();
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
		printf("\n");
		terminal_setcolor(old_color);
		kmalloc_print_stats();
		printf("\n");
		return;
	}
	
	// Otherwise show memory dump at address
	unsigned int addr = parse_hex(args);
	unsigned char* mem = (unsigned char*)addr;
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n=== Memory Viewer ===\n");
	terminal_setcolor(old_color);
	printf("Address: 0x%X\n\n", addr);
	
	// Display 4 rows of 16 bytes
	for (int row = 0; row < 4; row++) {
		printf("0x%X: ", addr + (row * 16));
		
		// Hex values
		for (int col = 0; col < 16; col++) {
			unsigned char byte = mem[row * 16 + col];
			printf("%X ", byte >> 4);
			printf("%X ", byte & 0x0F);
		}
		
		printf(" ");
		
		// ASCII representation
		for (int col = 0; col < 16; col++) {
			unsigned char byte = mem[row * 16 + col];
			if (byte >= 32 && byte < 127) {
				printf("%c", byte);
			} else {
				printf(".");
			}
		}
		printf("\n");
	}
	printf("\n");
}

static void command_netinfo(void) {
	net_print_info();
}

static void command_arp(void) {
	net_print_arp_table();
}

static void command_ping(const char* args) {
	while (*args == ' ') {
		args++;
	}
	if (*args == '\0') {
		printf("Usage: ping <ip> [count]\n");
		return;
	}

	uint8_t ip[4];
	const char *rest = args;
	if (!parse_ipv4(args, ip, &rest)) {
		printf("Invalid IP address.\n");
		return;
	}

	while (*rest == ' ') {
		rest++;
	}

	unsigned int count = 4;
	if (*rest) {
		unsigned int value = 0;
		bool has_digit = false;
		while (*rest >= '0' && *rest <= '9') {
			has_digit = true;
			value = value * 10 + (unsigned int)(*rest - '0');
			rest++;
		}
		if (has_digit && value > 0) {
			count = value;
		}
	}

	if (count > 16) {
		count = 16;
	}

	printf("PING ");
	print_ipv4(ip);
	printf(" (%u packets)\n", count);

	for (unsigned int i = 0; i < count; i++) {
		uint32_t rtt_ms = 0;
		bool ok = net_ping(ip, 1000, &rtt_ms);
		if (ok) {
			printf("Reply from ");
			print_ipv4(ip);
			printf(": time=%u ms\n", rtt_ms);
		} else {
			printf("Request timed out.\n");
		}
		if (i + 1 < count) {
			timer_sleep_ms(1000);
		}
	}
}

static void command_udpsend(const char* args) {
	while (*args == ' ') {
		args++;
	}
	if (*args == '\0') {
		printf("Usage: udpsend <ip> <port> <text>\n");
		return;
	}

	uint8_t ip[4];
	const char *rest = args;
	if (!parse_ipv4(args, ip, &rest)) {
		printf("Invalid IP address.\n");
		return;
	}

	while (*rest == ' ') {
		rest++;
	}

	unsigned int port = 0;
	if (!parse_uint(rest, &port, &rest) || port > 65535) {
		printf("Invalid port.\n");
		return;
	}

	while (*rest == ' ') {
		rest++;
	}
	if (*rest == '\0') {
		printf("Usage: udpsend <ip> <port> <text>\n");
		return;
	}

	size_t len = strlen(rest);
	if (len > UDP_SHELL_MAX) {
		len = UDP_SHELL_MAX;
	}

	if (!net_udp_send(ip, (uint16_t)port, (const uint8_t *)rest, (uint16_t)len)) {
		printf("UDP send failed.\n");
		return;
	}

	printf("Sent %u bytes to ", (unsigned int)len);
	print_ipv4(ip);
	printf(":%u\n", port);
}

static void command_udplisten(const char* args) {
	while (*args == ' ') {
		args++;
	}
	if (*args == '\0') {
		printf("Usage: udplisten <port> [timeout_ms]\n");
		return;
	}

	unsigned int port = 0;
	const char *rest = args;
	if (!parse_uint(rest, &port, &rest) || port > 65535) {
		printf("Invalid port.\n");
		return;
	}

	while (*rest == ' ') {
		rest++;
	}

	unsigned int timeout_ms = 5000;
	if (*rest) {
		unsigned int value = 0;
		if (parse_uint(rest, &value, &rest)) {
			timeout_ms = value;
		}
	}

	if (!net_udp_listen((uint16_t)port)) {
		printf("Failed to listen on UDP port %u.\n", port);
		return;
	}
	printf("Listening on UDP port %u...\n", port);

	uint32_t remaining = timeout_ms;
	uint8_t payload[UDP_SHELL_MAX];
	uint16_t payload_len = UDP_SHELL_MAX;
	uint8_t src_ip[4] = {0};
	uint16_t src_port = 0;

	while (remaining > 0) {
		payload_len = UDP_SHELL_MAX;
		if (net_udp_recv((uint16_t)port, payload, &payload_len, src_ip, &src_port)) {
			printf("UDP ");
			print_ipv4(src_ip);
			printf(":%u %u bytes: ", src_port, payload_len);
			for (uint16_t i = 0; i < payload_len; i++) {
				unsigned char c = payload[i];
				if (c >= 32 && c < 127) {
					printf("%c", c);
				} else {
					printf(".");
				}
			}
			printf("\n");
			return;
		}
		timer_sleep_ms(10);
		if (remaining > 10) {
			remaining -= 10;
		} else {
			remaining = 0;
		}
	}

	printf("UDP listen timed out.\n");
}

static void command_snake(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Snake Game ==========\n");
	terminal_setcolor(old_color);
	printf("Use WASD to move, Q or ESC to quit\n");
	printf("Press any key to start...\n");
	
	// Wait for key
	keyboard_clear_buffer();
	while (!keyboard_has_input()) {
		__asm__ volatile ("hlt");
	}
	keyboard_getchar();
	
	// Start game
	snake_game();
	
	terminal_setcolor(old_color);
}

static void command_cpuinfo(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== CPU Information ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	cpu_info_t info;
	cpu_detect(&info);
	cpu_print_info(&info);
	
	printf("\n");
}

static void command_rdtsc(void) {
	unsigned char old_color = terminal_getcolor();
	
	if (!cpu_has_feature(CPUID_FEAT_EDX_TSC)) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("\nTSC not supported on this CPU!\n\n");
		terminal_setcolor(old_color);
		return;
	}
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Timestamp Counter ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	uint64_t tsc1 = rdtsc();
	printf("TSC Value: 0x%08X%08X\n", (uint32_t)(tsc1 >> 32), (uint32_t)tsc1);
	
	// Measure some time
	for (volatile int i = 0; i < 10000000; i++);
	
	uint64_t tsc2 = rdtsc();
	printf("After delay: 0x%08X%08X\n", (uint32_t)(tsc2 >> 32), (uint32_t)tsc2);
	
	uint64_t diff = tsc2 - tsc1;
	printf("Cycles elapsed: %u\n", (uint32_t)diff);
	printf("\n");
}

static void command_regs(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Control Registers ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	uint32_t cr0 = read_cr0();
	uint32_t cr2 = read_cr2();
	uint32_t cr3 = read_cr3();
	uint32_t cr4 = read_cr4();
	uint32_t eflags = read_eflags();
	
	printf("CR0: 0x%08X\n", cr0);
	printf("  PE (Protected Mode):     %s\n", (cr0 & CR0_PE) ? "Enabled" : "Disabled");
	printf("  PG (Paging):             %s\n", (cr0 & CR0_PG) ? "Enabled" : "Disabled");
	printf("  WP (Write Protect):      %s\n", (cr0 & CR0_WP) ? "Enabled" : "Disabled");
	printf("  CD (Cache Disable):      %s\n", (cr0 & CR0_CD) ? "Disabled" : "Enabled");
	
	printf("\nCR2 (Page Fault Addr): 0x%08X\n", cr2);
	printf("CR3 (Page Directory):  0x%08X\n", cr3);
	
	printf("\nCR4: 0x%08X\n", cr4);
	printf("  PSE (Page Size Ext):     %s\n", (cr4 & CR4_PSE) ? "Enabled" : "Disabled");
	printf("  PAE (Phys Addr Ext):     %s\n", (cr4 & CR4_PAE) ? "Enabled" : "Disabled");
	printf("  PGE (Page Global):       %s\n", (cr4 & CR4_PGE) ? "Enabled" : "Disabled");
	
	printf("\nEFLAGS: 0x%08X\n", eflags);
	printf("  CF (Carry):              %s\n", (eflags & (1 << 0)) ? "Set" : "Clear");
	printf("  ZF (Zero):               %s\n", (eflags & (1 << 6)) ? "Set" : "Clear");
	printf("  SF (Sign):               %s\n", (eflags & (1 << 7)) ? "Set" : "Clear");
	printf("  IF (Interrupt Enable):   %s\n", (eflags & (1 << 9)) ? "Enabled" : "Disabled");
	
	printf("\n");
}

static void command_benchmark(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== CPU Benchmark ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	if (!cpu_has_feature(CPUID_FEAT_EDX_TSC)) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("TSC not available - cannot benchmark!\n\n");
		terminal_setcolor(old_color);
		return;
	}
	
	// Integer arithmetic benchmark
	printf("Running integer arithmetic test...\n");
	uint64_t start = rdtsc();
	volatile int sum = 0;
	for (int i = 0; i < 1000000; i++) {
		sum += i;
	}
	uint64_t end = rdtsc();
	printf("  1M iterations: %u cycles\n", (uint32_t)(end - start));
	
	// Memory access benchmark
	printf("Running memory access test...\n");
	volatile char test_array[1024];
	start = rdtsc();
	for (int i = 0; i < 10000; i++) {
		for (int j = 0; j < 1024; j++) {
			test_array[j] = (char)j;
		}
	}
	end = rdtsc();
	printf("  10K * 1KB writes: %u cycles\n", (uint32_t)(end - start));
	
	// Division benchmark
	printf("Running division test...\n");
	start = rdtsc();
	volatile int result = 0;
	for (int i = 1; i < 10000; i++) {
		result = 1000000 / i;
	}
	end = rdtsc();
	printf("  10K divisions: %u cycles\n", (uint32_t)(end - start));
	
	// Atomic operations benchmark
	printf("Running atomic operations test...\n");
	int atomic_var = 0;
	start = rdtsc();
	for (int i = 0; i < 100000; i++) {
		atomic_inc(&atomic_var);
	}
	end = rdtsc();
	printf("  100K atomic incs: %u cycles\n", (uint32_t)(end - start));
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("\nBenchmark complete!\n\n");
	terminal_setcolor(old_color);
}

static void command_edit(const char* args) {
	while (*args == ' ') args++;
	
	if (!*args || strcmp_local(args, "") == 0) {
		printf("Usage: edit <filename>\n");
		printf("\nEditor commands:\n");
		printf("  ESC    - Enter normal mode\n");
		printf("  i      - Enter insert mode\n");
		printf("  a      - Append (insert after cursor)\n");
		printf("  o      - Open new line below\n");
		printf("  O      - Open new line above\n");
		printf("  x      - Delete character\n");
		printf("  dd     - Delete line\n");
		printf("  h/j/k/l- Move cursor (left/down/up/right)\n");
		printf("  0      - Start of line\n");
		printf("  $      - End of line\n");
		printf("  gg     - First line\n");
		printf("  G      - Last line\n");
		printf("  :w     - Save\n");
		printf("  :q     - Quit\n");
		printf("  :wq    - Save and quit\n");
		printf("  :q!    - Quit without saving\n");
		return;
	}
	
	// Build absolute path if needed
	char abs_path[MAX_PATH_LEN];
	if (args[0] == '/') {
		// Already absolute
		strncpy(abs_path, args, MAX_PATH_LEN - 1);
		abs_path[MAX_PATH_LEN - 1] = '\0';
	} else {
		// Build absolute path from current directory
		if (strcmp(current_dir_path, "/") == 0) {
			snprintf(abs_path, sizeof(abs_path), "/%s", args);
		} else {
			snprintf(abs_path, sizeof(abs_path), "%s/%s", current_dir_path, args);
		}
	}
	
	editor_run(abs_path);
}

static void command_display(const char* args) {
	if (!args) args = "";
	
	unsigned char old_color = terminal_getcolor();
	
	if (strcmp_local(args, "80x25") == 0) {
		terminal_set_mode_80x25();
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("Display mode set to 80x25\n");
		terminal_setcolor(old_color);
	} else if (strcmp_local(args, "80x50") == 0) {
		terminal_set_mode_80x50();
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("Display mode set to 80x50\n");
		terminal_setcolor(old_color);
	} else if (strcmp_local(args, "320x200") == 0) {
		graphics_set_mode(MODE_13H);
		graphics_clear(COLOR_BLACK);
		graphics_print(10, 10, "Graphics mode 320x200 active", COLOR_WHITE, COLOR_BLACK);
		graphics_print(10, 20, "Press ESC to return to text mode", COLOR_YELLOW, COLOR_BLACK);
		while (keyboard_getchar() != 27); // Wait for ESC
		graphics_set_mode(MODE_TEXT);
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("Returned to text mode\n");
		terminal_setcolor(old_color);
	} else if (strcmp_local(args, "320x240") == 0) {
		graphics_set_mode(MODE_320x240);
		graphics_clear(COLOR_BLACK);
		graphics_print(10, 10, "Graphics mode 320x240 active", COLOR_WHITE, COLOR_BLACK);
		graphics_print(10, 20, "Press ESC to return to text mode", COLOR_YELLOW, COLOR_BLACK);
		while (keyboard_getchar() != 27); // Wait for ESC
		graphics_set_mode(MODE_TEXT);
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("Returned to text mode\n");
		terminal_setcolor(old_color);
	} else if (strcmp_local(args, "info") == 0 || strcmp_local(args, "") == 0) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
		printf("\n========== Display Settings ==========\n");
		terminal_setcolor(old_color);
		printf("\n");
		printf("Current mode: %ux%u\n", (unsigned int)terminal_get_width(), (unsigned int)terminal_get_height());
		printf("\n");
		printf("Available modes:\n");
		printf("  Text Modes:\n");
		printf("    80x25   - Standard VGA text mode\n");
		printf("    80x50   - Extended VGA text mode (8-line font)\n");
		printf("  Graphics Modes:\n");
		printf("    320x200 - Mode 13h (256 colors)\n");
		printf("    320x240 - Alias for 320x200\n");
		printf("\n");
		printf("Usage: display <mode>\n");
		printf("Example: display 80x50\n");
		printf("\n");
		printf("Mouse scrolling: Enabled\n");
		printf("  Use mouse wheel to scroll through terminal history\n");
		printf("\n");
	} else {
		printf("Unknown display mode: %s\n", args);
		printf("Available modes: 80x25, 80x50, 320x200\n");
		printf("Type 'display info' for more information.\n");
	}
}

// Demo kernel thread functions
static void demo_task_1(void) {
	for (int i = 0; i < 10; i++) {
		printf("[Task 1] Iteration %d\n", i);
		timer_sleep_ms(500);
	}
	printf("[Task 1] Finished!\n");
	task_exit();
}

static void demo_task_2(void) {
	for (int i = 0; i < 8; i++) {
		printf("[Task 2] Count: %d\n", i);
		timer_sleep_ms(700);
	}
	printf("[Task 2] Done!\n");
	task_exit();
}

static void demo_task_3(void) {
	for (int i = 0; i < 5; i++) {
		printf("[Task 3] Working... %d\n", i);
		timer_sleep_ms(1000);
	}
	printf("[Task 3] Complete!\n");
	task_exit();
}

static const char *process_state_label(uint8_t state) {
	switch (state) {
		case PROCESS_READY: return "READY";
		case PROCESS_RUNNING: return "RUNNING";
		case PROCESS_BLOCKED: return "BLOCKED";
		case PROCESS_ZOMBIE: return "ZOMBIE";
		default: return "UNKNOWN";
	}
}

// Process list command
static void command_ps(void) {
	enum { PROCESS_LIST_MAX = 32 };
	process_info_t list[PROCESS_LIST_MAX];
	uint32_t count = process_list(list, PROCESS_LIST_MAX);

	printf("PID\tState\t\tPrio\tSlice\tTime\tName\n");
	printf("---\t--------\t----\t-----\t----\t--------------------------------\n");
	for (uint32_t i = 0; i < count; i++) {
		const process_info_t *info = &list[i];
		printf("%u\t%s\t%u\t%u\t%u\t%s\n",
		       info->pid,
		       process_state_label(info->state),
		       info->priority,
		       info->time_slice,
		       info->total_time,
		       info->name);
	}
	if (count == 0) {
		printf("(no user processes)\n");
	}
}

// Kernel task list command
static void command_tasks(void) {
	task_list();
}

// Kill process command
static void command_kill(const char* args) {
	unsigned int pid = 0;
	while (args && *args == ' ') {
		args++;
	}
	if (!parse_uint(args, &pid, NULL) || pid == 0) {
		printf("Usage: kill <pid>\n");
		return;
	}

	if (process_kill_other((uint32_t)pid, 128 + 15)) {
		printf("Process %u killed\n", pid);
		return;
	}
	printf("Process %u not found\n", pid);
}

// Kill kernel task command
static void command_killtask(const char* args) {
	unsigned int tid = 0;
	while (args && *args == ' ') {
		args++;
	}
	if (!parse_uint(args, &tid, NULL) || tid == 0) {
		printf("Usage: killtask <tid>\n");
		return;
	}

	if (!task_kill((uint32_t)tid)) {
		printf("Kernel thread %u not found\n", tid);
	}
}

// Spawn task command
static void command_spawn(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: spawn <demo1|demo2|demo3>\n");
		return;
	}
	
	task_t *task = NULL;
	
	if (strcmp(args, "demo1") == 0) {
		task = task_create("Demo Task 1", demo_task_1, 1);
	} else if (strcmp(args, "demo2") == 0) {
		task = task_create("Demo Task 2", demo_task_2, 1);
	} else if (strcmp(args, "demo3") == 0) {
		task = task_create("Demo Task 3", demo_task_3, 1);
	} else {
		printf("Unknown demo: %s\n", args);
		printf("Available: demo1, demo2, demo3\n");
		return;
	}
	
	if (!task) {
		printf("Failed to create kernel thread\n");
	}
}

static void stacktest_task(void) {
	task_t *task = task_current();
	if (!task || task->kernel_stack == 0) {
		printf("Stack test: no task stack\n");
		return;
	}
	uint32_t stack_base = task->kernel_stack - TASK_KERNEL_STACK_SIZE;
	uint32_t guard_addr = stack_base - PAGE_SIZE;
	volatile uint32_t *fault = (volatile uint32_t *)guard_addr;
	*fault = 0xDEADCAFE;
	for (;;) {
		__asm__ volatile ("hlt");
	}
}

static void command_stacktest(void) {
	printf("Stack test: triggering kernel stack overflow (guard page)...\n");
	task_t *task = task_create("stacktest", stacktest_task, 0);
	if (!task) {
		printf("Stack test: failed to create task\n");
		return;
	}
	task_yield();
}

static void persist_dma_setting(bool enabled) {
	fs_context_t *fs = fs_get_context();
	if (!fs || !fs->mounted) {
		printf("Warning: filesystem not mounted; DMA setting not saved\n");
		return;
	}
	fs_create_dir("/etc");
	int create_res = fs_create_file("/etc/boot.cfg");
	if (create_res < 0 && create_res != -2) {
		printf("Warning: failed to create /etc/boot.cfg\n");
		return;
	}
	const char *line = enabled ? "dma=on\n" : "dma=off\n";
	if (fs_write_file("/etc/boot.cfg", (const uint8_t *)line, strlen(line), 0) < 0) {
		printf("Warning: failed to write /etc/boot.cfg\n");
	}
}

static void command_dma(const char* args) {
	if (!args) {
		args = "";
	}
	while (*args == ' ') {
		args++;
	}

	char mode[16];
	size_t idx = 0;
	while (*args && *args != ' ' && idx < sizeof(mode) - 1) {
		mode[idx++] = *args++;
	}
	mode[idx] = '\0';

	if (mode[0] == '\0' || strcmp_local(mode, "status") == 0) {
		printf("ATA DMA is %s\n", ata_dma_is_enabled() ? "enabled" : "disabled");
		printf("Usage: dma <on|off|toggle|status>\n");
		return;
	}

	if (strcmp_local(mode, "on") == 0 || strcmp_local(mode, "enable") == 0) {
		ata_device_t *dev = ata_get_device(0);
		if (dev && !dev->dma_supported) {
			printf("Warning: drive 0 does not report DMA capability\n");
		}
		ata_set_dma_enabled(true);
		persist_dma_setting(true);
		printf("ATA DMA enabled (will validate on next write)\n");
		return;
	}

	if (strcmp_local(mode, "off") == 0 || strcmp_local(mode, "disable") == 0) {
		ata_set_dma_enabled(false);
		persist_dma_setting(false);
		printf("ATA DMA disabled\n");
		return;
	}

	if (strcmp_local(mode, "toggle") == 0) {
		bool enabled = !ata_dma_is_enabled();
		ata_set_dma_enabled(enabled);
		persist_dma_setting(enabled);
		printf("ATA DMA %s (will validate on next write)\n",
		       enabled ? "enabled" : "disabled");
		return;
	}

	printf("Usage: dma <on|off|toggle|status>\n");
}

// Run user-mode ELF program
// Format disk command
static void command_diskfmt(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: diskfmt <drive_number>\n");
		printf("Warning: This will erase all data on the drive!\n");
		return;
	}
	
	uint8_t drive = 0;
	while (*args >= '0' && *args <= '9') {
		drive = drive * 10 + (*args - '0');
		args++;
	}
	
	if (drive >= 4) {
		printf("Invalid drive number (0-3)\n");
		return;
	}
	
	ata_device_t *dev = ata_get_device(drive);
	if (!dev) {
		printf("Drive %u not found\n", drive);
		return;
	}
	
	printf("Formatting drive %u...\n", drive);
	if (fs_format(drive)) {
		printf("Format complete!\n");
	} else {
		printf("Format failed\n");
	}
}

// Mount disk command
static void command_diskmount(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: diskmount <drive_number>\n");
		return;
	}
	
	uint8_t drive = 0;
	while (*args >= '0' && *args <= '9') {
		drive = drive * 10 + (*args - '0');
		args++;
	}
	
	if (drive >= 4) {
		printf("Invalid drive number (0-3)\n");
		return;
	}
	
	ata_device_t *dev = ata_get_device(drive);
	if (!dev) {
		printf("Drive %u not found\n", drive);
		return;
	}
	
	if (fs_mount(drive)) {
		printf("Mounted drive %u\n", drive);
	} else {
		printf("Mount failed. Try formatting with diskfmt first.\n");
	}
}

// List disk files command
static void command_diskls(const char* args) {
	(void)args;

	fs_context_t *fs = fs_get_context();
	if (!fs || !fs->mounted) {
		printf("No filesystem mounted. Use diskmount first.\n");
		return;
	}
	
	fs_dirent_t entries[32];
	int count = fs_list_dir("/", entries, 32);
	
	if (count < 0) {
		printf("Failed to list directory\n");
		return;
	}
	
	if (count == 0) {
		printf("No files found\n");
		return;
	}
	
	printf("Files on disk:\n");
	for (int i = 0; i < count; i++) {
		printf("  %s\n", entries[i].name);
	}
}

// Write file to disk command
static void command_diskwrite(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: diskwrite <filename> <content>\n");
		return;
	}
	
	fs_context_t *fs = fs_get_context();
	if (!fs || !fs->mounted) {
		printf("No filesystem mounted. Use diskmount first.\n");
		return;
	}
	
	// Parse filename
	char filename[32];
	int i = 0;
	while (*args && *args != ' ' && i < 31) {
		filename[i++] = *args++;
	}
	filename[i] = '\0';
	
	// Skip spaces
	while (*args == ' ') args++;
	
	if (*args == '\0') {
		printf("Usage: diskwrite <filename> <content>\n");
		return;
	}
	
	// Create file
	int result = fs_create_file(filename);
	if (result < 0 && result != -2) {
		printf("Failed to create file\n");
		return;
	}
	
	// Write content
	result = fs_write_file(filename, (const uint8_t*)args, strlen(args), 0);
	if (result > 0) {
		printf("Wrote %d bytes to %s\n", result, filename);
	} else {
		printf("Write failed\n");
	}
}

// Read file from disk command
static void command_diskread(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: diskread <filename>\n");
		return;
	}
	
	fs_context_t *fs = fs_get_context();
	if (!fs || !fs->mounted) {
		printf("No filesystem mounted. Use diskmount first.\n");
		return;
	}
	
	uint8_t buffer[512];
	int bytes_read = fs_read_file(args, buffer, sizeof(buffer) - 1, 0);
	
	if (bytes_read < 0) {
		printf("File not found or read error\n");
		return;
	}
	
	if (bytes_read == 0) {
		printf("File is empty\n");
		return;
	}
	
	buffer[bytes_read] = '\0';
	printf("%s\n", buffer);
}
