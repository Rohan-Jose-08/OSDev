#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/cpu.h>
#include <kernel/io.h>
#include <kernel/editor.h>
#include <kernel/vfs.h>
#include <kernel/mouse.h>
#include <kernel/snake.h>
#include <kernel/graphics_demo.h>
#include <kernel/graphics.h>
#include <kernel/task.h>
#include <kernel/timer.h>


#define MAX_COMMAND_LENGTH 256

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

// Current color theme
static int current_theme = 0;

// Current working directory
static vfs_node_t *current_directory = NULL;

// Simple pseudo-random number generator
static unsigned int rand_seed = 12345;
static unsigned int simple_rand(void) {
	rand_seed = rand_seed * 1103515245 + 12345;
	return (rand_seed / 65536) % 32768;
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

// Forward declarations
static void output_prompt(void);
static void input_line(char* buffer, size_t max_length);
static void execute_command(const char* command);
static void command_help(void);
static void command_clear(void);
static void command_echo(const char* args);
static void command_about(void);
static void command_color(const char* args);
static void command_colors(void);
static void command_calc(const char* args);
static void command_sysinfo(void);
static void command_banner(void);
static void command_uptime(void);
static void command_memory(const char* args);
static void command_reverse(const char* args);
static void command_guess(void);
static void command_art(void);
static void command_strlen(const char* args);
static void command_upper(const char* args);
static void command_lower(const char* args);
static void command_rainbow(const char* args);
static void command_draw(const char* args);
static void command_timer(const char* args);
static void command_rps(void);
static void command_history(void);
static void command_halt(void);
static void command_randcolor(void);
static void command_tictactoe(void);
static void command_hangman(void);
static void command_snake(void);
static void command_alias(const char* args);
static void command_unalias(const char* args);
static void command_aliases(void);
static void command_theme(const char* args);
static void command_fortune(void);
static void command_animate(const char* args);
static void command_beep(void);
static void command_matrix(void);
static void command_cpuinfo(void);
static void command_rdtsc(void);
static void command_regs(void);
static void command_benchmark(void);
static void command_display(const char* args);
static void command_edit(const char* args);
static void command_gfx(void);
static void command_gfxanim(void);
static void command_gfxpaint(void);
static void command_ls(const char* args);
static void command_cat(const char* args);
static void command_rm(const char* args);
static void command_touch(const char* args);
static void command_mkdir(const char* args);
static void command_rmdir(const char* args);
static void command_cd(const char* args);
static void command_pwd(void);
static void command_ps(void);
static void command_kill(const char* args);
static void command_spawn(const char* args);

static int strcmp_local(const char* s1, const char* s2) {
	while (*s1 && (*s1 == *s2)) { s1++; s2++; }
	return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static bool starts_with(const char* str, const char* prefix) {
	while (*prefix) {
		if (*str++ != *prefix++) return false;
	}
	return true;
}

static void strcpy_local(char* dest, const char* src) {
	while (*src) {
		*dest++ = *src++;
	}
	*dest = '\0';
}

// Calculate minimum of three integers
static int min3(int a, int b, int c) {
	int min = a;
	if (b < min) min = b;
	if (c < min) min = c;
	return min;
}

// Calculate Levenshtein distance for typo detection
static int levenshtein_distance(const char* s1, const char* s2) {
	int len1 = strlen(s1);
	int len2 = strlen(s2);
	if (len1 == 0) return len2;
	if (len2 == 0) return len1;
	
	int matrix[32][32]; // Max command length support
	for (int i = 0; i <= len1; i++) matrix[i][0] = i;
	for (int j = 0; j <= len2; j++) matrix[0][j] = j;
	
	for (int i = 1; i <= len1; i++) {
		for (int j = 1; j <= len2; j++) {
			int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
			matrix[i][j] = min3(
				matrix[i-1][j] + 1,
				matrix[i][j-1] + 1,
				matrix[i-1][j-1] + cost
			);
		}
	}
	return matrix[len1][len2];
}

static int parse_int(const char* str) {
	int result = 0;
	bool negative = false;
	if (*str == '-') { negative = true; str++; }
	while (*str >= '0' && *str <= '9') {
		result = result * 10 + (*str - '0');
		str++;
	}
	return negative ? -result : result;
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

void shell_init(void) {
	char command[MAX_COMMAND_LENGTH];
	
	// Initialize current directory to root
	current_directory = vfs_get_root();
	
	command_banner();
	
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
	char path[VFS_MAX_PATH_LEN];
	
	if (current_directory && vfs_get_full_path(current_directory, path, sizeof(path))) {
		printf("myos:%s> ", path);
	} else {
		printf("myos> ");
	}
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
			// Tab completion
			buffer[pos] = '\0';
			const char* commands[] = {"help", "clear", "about", "banner", "colors", "sysinfo", "uptime",
				"guess", "art", "rps", "history", "halt", "randcolor", "echo", "color", "calc", "mem",
				"reverse", "strlen", "upper", "lower", "rainbow", "draw", "timer", "tictactoe", "hangman",
				"alias", "unalias", "aliases", "theme", "fortune", "animate", "beep", "matrix",
				"cpuinfo", "rdtsc", "regs", "benchmark", "edit", "display", "ls", "cat", "rm", "touch"};
			int num_commands = 44;
			
			// Find matching commands
			int matches = 0;
			const char* match = NULL;
			for (int i = 0; i < num_commands; i++) {
				bool is_match = true;
				for (size_t j = 0; j < pos; j++) {
					if (buffer[j] != commands[i][j]) {
						is_match = false;
						break;
					}
				}
				if (is_match && strlen(commands[i]) >= pos) {
					matches++;
					match = commands[i];
				}
			}
			
			if (matches == 1) {
				// Complete the command
				while (pos < strlen(match)) {
					buffer[pos] = match[pos];
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

static void execute_command(const char* command) {
	// Check aliases first
	for (int i = 0; i < alias_count; i++) {
		if (strcmp_local(command, alias_names[i]) == 0) {
			execute_command(alias_commands[i]);
			return;
		}
	}
	
	// Command table - commands without arguments
	static const command_entry_t command_table[] = {
		{"help", command_help, NULL, false},
		{"clear", command_clear, NULL, false},
		{"about", command_about, NULL, false},
		{"banner", command_banner, NULL, false},
		{"colors", command_colors, NULL, false},
		{"sysinfo", command_sysinfo, NULL, false},
		{"uptime", command_uptime, NULL, false},
		{"guess", command_guess, NULL, false},
		{"art", command_art, NULL, false},
		{"rps", command_rps, NULL, false},
		{"history", command_history, NULL, false},
		{"halt", command_halt, NULL, false},
		{"randcolor", command_randcolor, NULL, false},
		{"tictactoe", command_tictactoe, NULL, false},
		{"hangman", command_hangman, NULL, false},
		{"snake", command_snake, NULL, false},
		{"aliases", command_aliases, NULL, false},
		{"fortune", command_fortune, NULL, false},
		{"beep", command_beep, NULL, false},
		{"matrix", command_matrix, NULL, false},
		{"cpuinfo", command_cpuinfo, NULL, false},
		{"rdtsc", command_rdtsc, NULL, false},
		{"regs", command_regs, NULL, false},
		{"benchmark", command_benchmark, NULL, false},
		{"gfx", command_gfx, NULL, false},
		{"gfxanim", command_gfxanim, NULL, false},
		{"gfxpaint", command_gfxpaint, NULL, false},
		{"echo", NULL, command_echo, true},
		{"color", NULL, command_color, true},
		{"calc", NULL, command_calc, true},
		{"mem", NULL, command_memory, true},
		{"reverse", NULL, command_reverse, true},
		{"strlen", NULL, command_strlen, true},
		{"upper", NULL, command_upper, true},
		{"lower", NULL, command_lower, true},
		{"rainbow", NULL, command_rainbow, true},
		{"draw", NULL, command_draw, true},
		{"timer", NULL, command_timer, true},
		{"alias", NULL, command_alias, true},
		{"unalias", NULL, command_unalias, true},
		{"theme", NULL, command_theme, true},
		{"animate", NULL, command_animate, true},
		{"display", NULL, command_display, true},
		{"edit", NULL, command_edit, true},
		{"ls", NULL, command_ls, true},
		{"cat", NULL, command_cat, true},
		{"rm", NULL, command_rm, true},
		{"touch", NULL, command_touch, true},
		{"mkdir", NULL, command_mkdir, true},
		{"rmdir", NULL, command_rmdir, true},
		{"cd", NULL, command_cd, true},
		{"pwd", command_pwd, NULL, false},
		{"ps", command_ps, NULL, false},
		{"kill", NULL, command_kill, true},
		{"spawn", NULL, command_spawn, true},
	};
	
	const int num_commands = sizeof(command_table) / sizeof(command_entry_t);
	
	// Try to find and execute command
	for (int i = 0; i < num_commands; i++) {
		const command_entry_t* cmd = &command_table[i];
		size_t cmd_len = strlen(cmd->name);
		
		if (cmd->requires_arg) {
			// Check if command matches with space after (has arguments)
			if (starts_with(command, cmd->name) && 
			    command[cmd_len] == ' ') {
				cmd->handler_with_arg(command + cmd_len + 1);
				return;
			}
			// Also match exact command name (no arguments - will show usage)
			if (strcmp_local(command, cmd->name) == 0) {
				cmd->handler_with_arg("");
				return;
			}
		} else {
			// Exact match for commands without arguments
			if (strcmp_local(command, cmd->name) == 0) {
				cmd->handler();
				return;
			}
		}
	}
	
	// Unknown command handling
	if (strcmp_local(command, "") != 0) {
		printf("Unknown command: %s\n", command);
		
		// Suggest similar commands using Levenshtein distance
		int best_match = -1;
		int best_distance = 999;
		for (int i = 0; i < num_commands; i++) {
			int dist = levenshtein_distance(command, command_table[i].name);
			if (dist < best_distance && dist <= 2) {
				best_distance = dist;
				best_match = i;
			}
		}
		
		if (best_match >= 0) {
			printf("Did you mean '%s'?\n", command_table[best_match].name);
		} else {
			printf("Type 'help' for available commands.\n");
		}
	}
}

static void command_help(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== MyOS Shell Commands ==========\n");
	terminal_setcolor(old_color);
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("General Commands:\n");
	terminal_setcolor(old_color);
	printf("  strlen <text> - Show string length\n");
	printf("  upper <text>  - Convert to uppercase\n");
	printf("  lower <text>  - Convert to lowercase\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Visual Effects:\n");
	terminal_setcolor(old_color);
	printf("  rainbow <text> - Display text in rainbow colors\n");
	printf("  draw <type>   - Draw patterns (box, line, rainbow)\n");
	printf("  randcolor     - Set random colors\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Games:\n");
	terminal_setcolor(old_color);
	printf("  guess         - Number guessing game\n");
	printf("  rps           - Rock Paper Scissors\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("System:\n");
	terminal_setcolor(old_color);
	printf("  timer <start|stop> - Simple timer\n");
	printf("  history       - Show command history\n");
	printf("  halt          - Halt the system");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Display Commands:\n");
	terminal_setcolor(old_color);
	printf("  display <mode> - Change display mode (80x25, 80x50, 320x200)\n");
	printf("  echo <text>   - Echo text to the screen\n");
	printf("  color <fg> <bg> - Set text colors (0-15)\n");
	printf("  colors        - Show available colors\n");
	printf("  art           - Display ASCII art\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Utilities:\n");
	terminal_setcolor(old_color);
	printf("  calc <expr>   - Simple calculator (e.g., calc 5 + 3)\n");
	printf("  reverse <text> - Reverse text string\n");
	printf("  mem <addr>    - View memory (hex address, e.g., mem 0xB8000)\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Games:\n");
	terminal_setcolor(old_color);
	printf("  guess         - Number guessing game\n");
	printf("  tictactoe     - Play Tic-Tac-Toe\n");
	printf("  hangman       - Play Hangman\n");
	printf("  snake         - Play Snake Game (WASD to move, Q to quit)\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Advanced Features:\n");
	terminal_setcolor(old_color);
	printf("  alias <n=c>   - Create command alias\n");
	printf("  unalias <n>   - Remove alias\n");
	printf("  aliases       - List all aliases\n");
	printf("  theme <name>  - Change color theme (list|dark|blue|green|amber)\n");
	printf("  fortune       - Display random fortune\n");
	printf("  animate <t>   - Show animation (spin|progress|dots)\n");
	printf("  beep          - Make system beep\n");
	printf("  matrix        - Matrix-style animation\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Low-Level/Assembly Commands:\n");
	terminal_setcolor(old_color);
	printf("  cpuinfo       - Detailed CPU information\n");
	printf("  rdtsc         - Read timestamp counter\n");
	printf("  regs          - Display control registers\n");
	printf("  benchmark     - CPU performance benchmark\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Editor & Display:\n");
	terminal_setcolor(old_color);
	printf("  edit <file>     - Text editor (vi-like)\n");
	printf("  display <mode>  - Change display mode (80x25, 80x50, info)\n");
	printf("  Mouse Wheel     - Scroll terminal display up/down\n");
	printf("  Arrow Up/Down   - Navigate command history\n");
	printf("  Type anything   - Return to current view\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("File System:\n");
	terminal_setcolor(old_color);
	printf("  ls [path]     - List directory contents\n");
	printf("  cat <file>    - Display file contents\n");
	printf("  touch <file>  - Create empty file\n");
	printf("  rm <file>     - Delete file\n");
	printf("  mkdir <dir>   - Create directory\n");
	printf("  rmdir <dir>   - Remove empty directory\n");
	printf("  cd [path]     - Change directory\n");
	printf("  pwd           - Print working directory\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Multitasking:\n");
	terminal_setcolor(old_color);
	printf("  ps            - List running tasks/processes\n");
	printf("  spawn <task>  - Create new task (demo1, demo2, demo3)\n");
	printf("  kill <pid>    - Terminate task by PID\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Graphics Mode (Mode 13h - 320x200, 256 colors):\n");
	terminal_setcolor(old_color);
	printf("  gfx           - Graphics demonstration (shapes, colors)\n");
	printf("  gfxanim       - Bouncing ball animation\n");
	printf("  gfxpaint      - Paint demo\n");
	
	printf("\nTip: Press TAB to auto-complete commands!\n");
	printf("\n");
}

static void command_clear(void) {
	terminal_initialize();
}

static void command_echo(const char* args) {
	while (*args == ' ') args++;
	if (*args) printf("%s\n", args);
	else printf("\n");
}

static void command_about(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n================================\n");
	printf("  RohanOS - Operating System\n");
	printf("================================\n");
	terminal_setcolor(old_color);
	printf("\n");
	printf("Version:      1.0\n");
	printf("Architecture: i386 (32-bit)\n");
	printf("Boot Loader:  GRUB Multiboot\n");
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Features:\n");
	terminal_setcolor(old_color);
	printf("  [+] Custom kernel with interrupt handling\n");
	printf("  [+] VGA text mode display (80x25, 16 colors)\n");
	printf("  [+] PS/2 keyboard input support\n");
	printf("  [+] Interactive shell with command processing\n");
	printf("  [+] Color rende ring support\n");
	printf("  [+] Basic arithmetic calculator\n");
	printf("  [+] Memory viewer utility\n");
	printf("  [+] Mini-games (number guessing)\n");
	printf("\n");
	printf("Commands executed: %u\n", command_count);
	printf("\n");
}

static void command_banner(void) {
	unsigned char old_color = terminal_getcolor();
	printf("\n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf(" __  __       ");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf(" ___  ");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	printf(" ____  \n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("|  \\/  |_   _ ");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("/ _ \\ ");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	printf("|  _ \\ \n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("| |\\/| | | | |");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf(" | | |");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	printf("| |_) |\n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("| |  | | |_| |");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf(" |_| |");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	printf("|  _ < \n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("|_|  |_|\\__, |");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("\\___/");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	printf("|_| \\_\\\n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("        |___/                   \n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
	printf("\n        Operating System v1.0\n");
	terminal_setcolor(old_color);
	printf("\n");
	printf("Type 'help' for available commands.\n");
	printf("\n");
}

static void command_colors(void) {
	unsigned char old_color = terminal_getcolor();
	printf("\n");
	printf("Available VGA Colors (0-15):\n");
	printf("\n");
	
	const char* color_names[] = {
		"0  - Black", "1  - Blue", "2  - Green", "3  - Cyan",
		"4  - Red", "5  - Magenta", "6  - Brown", "7  - Light Grey",
		"8  - Dark Grey", "9  - Light Blue", "10 - Light Green", "11 - Light Cyan",
		"12 - Light Red", "13 - Light Magenta", "14 - Yellow", "15 - White"
	};
	
	for (int i = 0; i < 16; i++) {
		terminal_setcolor(vga_entry_color(i, VGA_COLOR_BLACK));
		printf("  %s", color_names[i]);
		terminal_setcolor(old_color);
		printf("  [Sample Text]\n");
	}
	
	printf("\nUsage: color <foreground> <background>\n");
	printf("Example: color 10 0  (green text on black)\n\n");
}

static void command_color(const char* args) {
	int fg = -1, bg = -1;
	const char* ptr = args;
	
	while (*ptr == ' ') ptr++;
	if (*ptr >= '0' && *ptr <= '9') {
		fg = 0;
		while (*ptr >= '0' && *ptr <= '9') {
			fg = fg * 10 + (*ptr - '0');
			ptr++;
		}
	}
	
	while (*ptr == ' ') ptr++;
	if (*ptr >= '0' && *ptr <= '9') {
		bg = 0;
		while (*ptr >= '0' && *ptr <= '9') {
			bg = bg * 10 + (*ptr - '0');
			ptr++;
		}
	}
	
	if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
		printf("Error: Colors must be between 0 and 15\n");
		printf("Usage: color <foreground> <background>\n");
		printf("Type 'colors' to see available colors\n");
		return;
	}
	
	terminal_setcolor(vga_entry_color(fg, bg));
	printf("Color set to foreground=%d, background=%d\n", fg, bg);
}

static void command_calc(const char* args) {
	const char* ptr = args;
	int num1 = 0, num2 = 0;
	char op = 0;
	bool negative1 = false;
	
	while (*ptr == ' ') ptr++;
	if (*ptr == '-') { negative1 = true; ptr++; }
	
	if (*ptr >= '0' && *ptr <= '9') {
		while (*ptr >= '0' && *ptr <= '9') {
			num1 = num1 * 10 + (*ptr - '0');
			ptr++;
		}
		if (negative1) num1 = -num1;
	} else {
		printf("Error: Invalid expression\n");
		printf("Usage: calc <num1> <+|-|*|/> <num2>\n");
		return;
	}
	
	while (*ptr == ' ') ptr++;
	if (*ptr == '+' || *ptr == '-' || *ptr == '*' || *ptr == '/') {
		op = *ptr++;
	} else {
		printf("Error: Invalid operator\n");
		printf("Supported operators: + - * /\n");
		return;
	}
	
	while (*ptr == ' ') ptr++;
	bool negative2 = false;
	if (*ptr == '-') { negative2 = true; ptr++; }
	
	if (*ptr >= '0' && *ptr <= '9') {
		while (*ptr >= '0' && *ptr <= '9') {
			num2 = num2 * 10 + (*ptr - '0');
			ptr++;
		}
		if (negative2) num2 = -num2;
	} else {
		printf("Error: Invalid expression\n");
		return;
	}
	
	int result = 0;
	switch (op) {
		case '+': result = num1 + num2; break;
		case '-': result = num1 - num2; break;
		case '*': result = num1 * num2; break;
		case '/':
			if (num2 == 0) {
				printf("Error: Division by zero!\n");
				return;
			}
			result = num1 / num2;
			break;
	}
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("%d %c %d = %d\n", num1, op, num2, result);
	terminal_setcolor(old_color);
}

static void command_sysinfo(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== System Information ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	printf("CPU Architecture:  i386 (32-bit x86)\n");
	printf("OS Version:        MyOS v1.0\n");
	printf("Kernel Type:       Monolithic\n");
	printf("Boot Protocol:     Multiboot\n");
	printf("\n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Hardware:\n");
	terminal_setcolor(old_color);
	printf("  Display:         VGA Text Mode (80x25)\n");
	printf("  Colors:          16 colors (4-bit)\n");
	printf("  Input:           PS/2 Keyboard\n");
	printf("  Interrupts:      Enabled (IRQ 1)\n");
	printf("\n");
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Statistics:\n");
	terminal_setcolor(old_color);
	printf("  Commands run:    %u\n", command_count);
	printf("  Shell cycles:    %u\n", tick_count);
	printf("\n");
}

static void command_uptime(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n=== System Uptime ===\n");
	terminal_setcolor(old_color);
	printf("Shell cycles: %u\n", tick_count);
	printf("Commands run: %u\n", command_count);
	printf("Status: Running\n\n");
}

static void command_memory(const char* args) {
	while (*args == ' ') args++;
	
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

static void command_reverse(const char* args) {
	while (*args == ' ') args++;
	
	if (!*args) {
		printf("Usage: reverse <text>\n");
		return;
	}
	
	// Calculate length
	int len = 0;
	const char* ptr = args;
	while (*ptr++) len++;
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
	
	// Print reversed
	for (int i = len - 1; i >= 0; i--) {
		printf("%c", args[i]);
	}
	printf("\n");
	
	terminal_setcolor(old_color);
}

static void command_guess(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
	printf("\n========== Number Guessing Game ==========\n");
	terminal_setcolor(old_color);
	
	int target = (simple_rand() % 100) + 1;
	int attempts = 0;
	char guess_buf[MAX_COMMAND_LENGTH];
	
	printf("\nI'm thinking of a number between 1 and 100.\n");
	printf("Can you guess it? (Type 'quit' to exit)\n\n");
	
	while (true) {
		printf("Your guess: ");
		input_line(guess_buf, MAX_COMMAND_LENGTH);
		
		if (strcmp_local(guess_buf, "quit") == 0) {
			printf("Game cancelled. The number was %d.\n\n", target);
			return;
		}
		
		int guess = parse_int(guess_buf);
		attempts++;
		
		if (guess < 1 || guess > 100) {
			printf("Please enter a number between 1 and 100.\n");
			continue;
		}
		
		if (guess == target) {
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
			printf("\n*** CORRECT! ***\n");
			terminal_setcolor(old_color);
			printf("You found it in %d attempts!\n\n", attempts);
			return;
		} else if (guess < target) {
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
			printf("Too low! ");
			terminal_setcolor(old_color);
		} else {
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
			printf("Too high! ");
			terminal_setcolor(old_color);
		}
		
		int diff = (guess > target) ? (guess - target) : (target - guess);
		if (diff <= 5) printf("You're very close!\n");
		else if (diff <= 15) printf("You're getting warm!\n");
		else printf("Try again!\n");
	}
}

static void command_art(void) {
	unsigned char old_color = terminal_getcolor();
	int art_choice = simple_rand() % 3;
	
	printf("\n");
	
	if (art_choice == 0) {
		// Computer
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
		printf("     .---.        \n");
		printf("    /     \\       \n");
		printf("    \\.@-@./       \n");
		printf("    /`\\_/`\\       \n");
		printf("   //  _  \\\\      \n");
		printf("  | \\     )|_     \n");
		printf(" /`\\_`>  <_/ \\    \n");
		printf(" \\__/'---'\\__/    \n");
		printf("   COMPUTER!      \n");
	} else if (art_choice == 1) {
		// Rocket
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("        /\\        \n");
		printf("       /  \\       \n");
		printf("      |    |      \n");
		terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
		printf("      | OS |      \n");
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("      |    |      \n");
		printf("     /|    |\\     \n");
		printf("    / |    | \\    \n");
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
		printf("   /  '    '  \\   \n");
		printf("  / .'      '. \\  \n");
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("    ROCKET!       \n");
	} else {
		// Robot
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("   _____          \n");
		printf("  |     |         \n");
		printf("  | O O |         \n");
		printf("  |  >  |         \n");
		printf("  |_____|         \n");
		printf("  _|_|_|_         \n");
		printf(" |       |        \n");
		printf(" |       |        \n");
		printf(" |_______|        \n");
		printf("  |     |         \n");
		printf("  |     |         \n");
		printf(" _|     |_        \n");
		printf("   ROBOT!         \n");
	}
	
	terminal_setcolor(old_color);
	printf("\n");
}

static void command_strlen(const char* args) {
	while (*args == ' ') args++;
	if (!*args) {
		printf("Usage: strlen <text>\n");
		return;
	}
	
	int len = 0;
	const char* ptr = args;
	while (*ptr++) len++;
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("String length: %d characters\n", len);
	terminal_setcolor(old_color);
}

static void command_upper(const char* args) {
	while (*args == ' ') args++;
	if (!*args) {
		printf("Usage: upper <text>\n");
		return;
	}
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	
	while (*args) {
		char c = *args++;
		if (c >= 'a' && c <= 'z') {
			printf("%c", c - 32);
		} else {
			printf("%c", c);
		}
	}
	printf("\n");
	terminal_setcolor(old_color);
}

static void command_lower(const char* args) {
	while (*args == ' ') args++;
	if (!*args) {
		printf("Usage: lower <text>\n");
		return;
	}
	
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
	
	while (*args) {
		char c = *args++;
		if (c >= 'A' && c <= 'Z') {
			printf("%c", c + 32);
		} else {
			printf("%c", c);
		}
	}
	printf("\n");
	terminal_setcolor(old_color);
}

static void command_rainbow(const char* args) {
	while (*args == ' ') args++;
	if (!*args) {
		printf("Usage: rainbow <text>\n");
		return;
	}
	
	unsigned char old_color = terminal_getcolor();
	int colors[] = {VGA_COLOR_RED, VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_BROWN, 
	                VGA_COLOR_LIGHT_GREEN, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_BLUE, 
	                VGA_COLOR_LIGHT_MAGENTA};
	int color_index = 0;
	
	while (*args) {
		terminal_setcolor(vga_entry_color(colors[color_index % 7], VGA_COLOR_BLACK));
		printf("%c", *args++);
		if (*args != ' ') color_index++;
	}
	printf("\n");
	terminal_setcolor(old_color);
}

static void command_draw(const char* args) {
	while (*args == ' ') args++;
	unsigned char old_color = terminal_getcolor();
	
	if (strcmp_local(args, "box") == 0) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
		printf("\n+--------------------------------------------------+\n");
		printf("|                                                  |\n");
		printf("|            MyOS - Operating System               |\n");
		printf("|                                                  |\n");
		printf("+--------------------------------------------------+\n\n");
	} else if (strcmp_local(args, "line") == 0) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("\n================================================\n\n");
	} else if (strcmp_local(args, "rainbow") == 0) {
		printf("\n");
		int colors[] = {VGA_COLOR_RED, VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_BROWN, 
		                VGA_COLOR_LIGHT_GREEN, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_BLUE, 
		                VGA_COLOR_LIGHT_MAGENTA};
		for (int i = 0; i < 7; i++) {
			terminal_setcolor(vga_entry_color(colors[i], VGA_COLOR_BLACK));
			printf("========");
		}
		printf("\n\n");
	} else {
		printf("Usage: draw <box|line|rainbow>\n");
	}
	
	terminal_setcolor(old_color);
}

static void command_timer(const char* args) {
	while (*args == ' ') args++;
	unsigned char old_color = terminal_getcolor();
	
	if (strcmp_local(args, "start") == 0) {
		if (timer_running) {
			printf("Timer is already running! Use 'timer stop' first.\n");
		} else {
			timer_start = tick_count;
			timer_running = true;
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
			printf("Timer started!\n");
			terminal_setcolor(old_color);
		}
	} else if (strcmp_local(args, "stop") == 0) {
		if (!timer_running) {
			printf("Timer is not running! Use 'timer start' first.\n");
		} else {
			unsigned int elapsed = tick_count - timer_start;
			timer_running = false;
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
			printf("Timer stopped! Elapsed cycles: %u\n", elapsed);
			terminal_setcolor(old_color);
		}
	} else {
		printf("Usage: timer <start|stop>\n");
	}
}

static void command_rps(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
	printf("\n========== Rock Paper Scissors ==========\n");
	terminal_setcolor(old_color);
	
	printf("\n1. Rock\n2. Paper\n3. Scissors\n");
	printf("\nYour choice (1-3): ");
	
	char choice_buf[MAX_COMMAND_LENGTH];
	input_line(choice_buf, MAX_COMMAND_LENGTH);
	
	int player = parse_int(choice_buf);
	if (player < 1 || player > 3) {
		printf("Invalid choice!\n\n");
		return;
	}
	
	int computer = (simple_rand() % 3) + 1;
	const char* choices[] = {"", "Rock", "Paper", "Scissors"};
	
	printf("You chose: %s\n", choices[player]);
	printf("Computer chose: %s\n\n", choices[computer]);
	
	if (player == computer) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
		printf("It's a tie!\n");
	} else if ((player == 1 && computer == 3) || 
	           (player == 2 && computer == 1) || 
	           (player == 3 && computer == 2)) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("You win!\n");
	} else {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
		printf("Computer wins!\n");
	}
	terminal_setcolor(old_color);
	printf("\n");
}

static void command_history(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Command History ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	if (history_count == 0) {
		printf("No commands in history.\n\n");
		return;
	}
	
	for (int i = 0; i < history_count; i++) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("%d. ", i + 1);
		terminal_setcolor(old_color);
		printf("%s\n", history_buffer[i]);
	}
	printf("\n");
}

static void command_halt(void) {
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
	
	// Halt the CPU
	while (true) {
		__asm__ volatile ("cli; hlt");
	}
}

static void command_randcolor(void) {
	int fg = (simple_rand() % 15) + 1; // 1-15, avoid black text
	int bg = simple_rand() % 8; // 0-7, darker backgrounds
	
	terminal_setcolor(vga_entry_color(fg, bg));
	printf("Random colors applied! (fg=%d, bg=%d)\n", fg, bg);
}

static void command_tictactoe(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Tic-Tac-Toe ==========\n");
	terminal_setcolor(old_color);
	
	char board[9] = {'1','2','3','4','5','6','7','8','9'};
	int player = 1; // 1 = X, 2 = O
	int moves = 0;
	
	while (moves < 9) {
		// Draw board
		printf("\n");
		for (int i = 0; i < 9; i += 3) {
			printf("  %c | %c | %c\n", board[i], board[i+1], board[i+2]);
			if (i < 6) printf(" -----------\n");
		}
		
		// Check for winner
		int win_lines[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
		for (int i = 0; i < 8; i++) {
			if (board[win_lines[i][0]] == board[win_lines[i][1]] && 
			    board[win_lines[i][1]] == board[win_lines[i][2]]) {
				terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
				printf("\nPlayer %d wins!\n\n", player == 1 ? 2 : 1);
				terminal_setcolor(old_color);
				return;
			}
		}
		
		char mark = (player == 1) ? 'X' : 'O';
		printf("\nPlayer %d (%c), enter position: ", player, mark);
		
		// Get input
		while (!keyboard_has_input()) __asm__ volatile ("hlt");
		char input = keyboard_getchar();
		printf("%c\n", input);
		
		if (input >= '1' && input <= '9') {
			int pos = input - '1';
			if (board[pos] != 'X' && board[pos] != 'O') {
				board[pos] = mark;
				moves++;
				player = (player == 1) ? 2 : 1;
			} else {
				printf("Position already taken!\n");
			}
		} else {
			printf("Invalid input! Use 1-9.\n");
		}
	}
	
	printf("\nGame Over - It's a draw!\n\n");
	terminal_setcolor(old_color);
}

static void command_hangman(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Hangman ==========\n");
	terminal_setcolor(old_color);
	
	const char* words[] = {"KERNEL", "SYSTEM", "MEMORY", "TERMINAL", "COMPUTER", "PROGRAM"};
	const char* word = words[simple_rand() % 6];
	int word_len = strlen(word);
	char guessed[32] = {0};
	int wrong_guesses = 0;
	int max_wrong = 6;
	
	while (wrong_guesses < max_wrong) {
		// Show current state
		printf("\nWord: ");
		bool complete = true;
		for (int i = 0; i < word_len; i++) {
			bool found = false;
			for (int j = 0; j < 26; j++) {
				if (guessed[j] == word[i]) {
					found = true;
					break;
				}
			}
			if (found) {
				printf("%c ", word[i]);
			} else {
				printf("_ ");
				complete = false;
			}
		}
		
		if (complete) {
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
			printf("\n\nYou won! The word was: %s\n\n", word);
			terminal_setcolor(old_color);
			return;
		}
		
		printf("\nWrong guesses: %d/%d\n", wrong_guesses, max_wrong);
		printf("Guessed: ");
		for (int i = 0; i < 26 && guessed[i]; i++) {
			printf("%c ", guessed[i]);
		}
		printf("\nGuess a letter: ");
		
		while (!keyboard_has_input()) __asm__ volatile ("hlt");
		char input = keyboard_getchar();
		printf("%c\n", input);
		
		if (input >= 'a' && input <= 'z') input -= 32; // Uppercase
		
		if (input >= 'A' && input <= 'Z') {
			// Check if already guessed
			bool already = false;
			for (int i = 0; i < 26; i++) {
				if (guessed[i] == input) {
					already = true;
					break;
				}
			}
			
			if (already) {
				printf("Already guessed that letter!\n");
				continue;
			}
			
			// Add to guessed
			for (int i = 0; i < 26; i++) {
				if (!guessed[i]) {
					guessed[i] = input;
					break;
				}
			}
			
			// Check if in word
			bool in_word = false;
			for (int i = 0; i < word_len; i++) {
				if (word[i] == input) {
					in_word = true;
					break;
				}
			}
			
			if (!in_word) {
				wrong_guesses++;
				printf("Wrong!\n");
			} else {
				printf("Correct!\n");
			}
		}
	}
	
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
	printf("\nYou lost! The word was: %s\n\n", word);
	terminal_setcolor(old_color);
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

static void command_alias(const char* args) {
	if (alias_count >= MAX_ALIASES) {
		printf("Maximum aliases reached!\n");
		return;
	}
	
	// Parse name=command
	char name[32] = {0};
	char cmd[MAX_COMMAND_LENGTH] = {0};
	int i = 0;
	while (args[i] && args[i] != '=' && i < 31) {
		name[i] = args[i];
		i++;
	}
	
	if (args[i] != '=') {
		printf("Usage: alias name=command\n");
		return;
	}
	
	i++; // Skip '='
	int j = 0;
	while (args[i] && j < MAX_COMMAND_LENGTH - 1) {
		cmd[j++] = args[i++];
	}
	
	strcpy_local(alias_names[alias_count], name);
	strcpy_local(alias_commands[alias_count], cmd);
	alias_count++;
	
	printf("Alias created: %s = %s\n", name, cmd);
}

static void command_unalias(const char* args) {
	for (int i = 0; i < alias_count; i++) {
		if (strcmp_local(alias_names[i], args) == 0) {
			// Shift remaining aliases
			for (int j = i; j < alias_count - 1; j++) {
				strcpy_local(alias_names[j], alias_names[j+1]);
				strcpy_local(alias_commands[j], alias_commands[j+1]);
			}
			alias_count--;
			printf("Alias removed: %s\n", args);
			return;
		}
	}
	printf("Alias not found: %s\n", args);
}

static void command_aliases(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n========== Command Aliases ==========\n");
	terminal_setcolor(old_color);
	printf("\n");
	
	if (alias_count == 0) {
		printf("No aliases defined.\n");
		printf("Use 'alias name=command' to create one.\n");
	} else {
		for (int i = 0; i < alias_count; i++) {
			terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
			printf("%s", alias_names[i]);
			terminal_setcolor(old_color);
			printf(" = %s\n", alias_commands[i]);
		}
	}
	printf("\n");
}

static void command_theme(const char* args) {
	unsigned char old_color = terminal_getcolor();
	
	if (strcmp_local(args, "dark") == 0) {
		current_theme = 0;
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
		printf("Theme set to: Dark\n");
	} else if (strcmp_local(args, "blue") == 0) {
		current_theme = 1;
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));
		printf("Theme set to: Blue\n");
	} else if (strcmp_local(args, "green") == 0) {
		current_theme = 2;
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
		printf("Theme set to: Green (Matrix)\n");
	} else if (strcmp_local(args, "amber") == 0) {
		current_theme = 3;
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
		printf("Theme set to: Amber (Retro)\n");
	} else if (strcmp_local(args, "list") == 0) {
		terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
		printf("\nAvailable themes:\n");
		terminal_setcolor(old_color);
		printf("  dark  - Classic dark terminal\n");
		printf("  blue  - Blue ocean theme\n");
		printf("  green - Matrix/hacker theme\n");
		printf("  amber - Retro amber monitor\n");
		printf("\nUsage: theme <name>\n");
	} else {
		printf("Unknown theme. Use 'theme list' to see available themes.\n");
	}
}

static void command_fortune(void) {
	const char* fortunes[] = {
		"A bug in the code is worth two in the documentation.",
		"The best way to predict the future is to implement it.",
		"Code never lies, comments sometimes do.",
		"Simplicity is the ultimate sophistication.",
		"First, solve the problem. Then, write the code.",
		"The only way to go fast is to go well.",
		"Programs must be written for people to read.",
		"Make it work, make it right, make it fast.",
		"The best code is no code at all.",
		"Any fool can write code that a computer can understand.",
		"Good programmers write good code. Great programmers steal great code.",
		"Debugging is twice as hard as writing the code in the first place."
	};
	
	int idx = simple_rand() % 12;
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
	printf("\n========================================\n");
	printf("  Fortune Cookie\n");
	printf("========================================\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printf("\n%s\n\n", fortunes[idx]);
	terminal_setcolor(old_color);
}

static void command_animate(const char* args) {
	if (strcmp_local(args, "spin") == 0) {
		char spinner[] = {'|', '/', '-', '\\\\'};
		printf("\nSpinning: ");
		for (int i = 0; i < 20; i++) {
			printf("%c\b", spinner[i % 4]);
			for (volatile int j = 0; j < 1000000; j++);
		}
		printf("Done!\n\n");
	} else if (strcmp_local(args, "progress") == 0) {
		printf("\nProgress: [");
		for (int i = 0; i <= 20; i++) {
			printf("#");
			for (volatile int j = 0; j < 2000000; j++);
		}
		printf("] Complete!\n\n");
	} else if (strcmp_local(args, "dots") == 0) {
		printf("\nLoading");
		for (int i = 0; i < 10; i++) {
			printf(".");
			for (volatile int j = 0; j < 2000000; j++);
		}
		printf(" Done!\n\n");
	} else {
		printf("Available animations: spin, progress, dots\n");
		printf("Usage: animate <type>\n");
	}
}

static void command_beep(void) {
	// PC speaker control via port 0x61
	unsigned char tmp = inb(0x61);
	
	// Enable speaker
	outb(0x61, tmp | 0x03);
	for (volatile int i = 0; i < 1000000; i++);
	
	// Disable speaker
	outb(0x61, tmp);
	
	printf("*BEEP*\n");
}

static void command_matrix(void) {
	unsigned char old_color = terminal_getcolor();
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	
	printf("\n--- MATRIX MODE ACTIVATED ---\n\n");
	
	for (int line = 0; line < 10; line++) {
		for (int col = 0; col < 40; col++) {
			char c = (simple_rand() % 94) + 33;
			printf("%c", c);
		}
		printf("\n");
		for (volatile int i = 0; i < 1000000; i++);
	}
	
	printf("\n--- MATRIX MODE DEACTIVATED ---\n\n");
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
	char abs_path[VFS_MAX_PATH_LEN];
	if (args[0] == '/') {
		// Already absolute
		strncpy(abs_path, args, VFS_MAX_PATH_LEN - 1);
		abs_path[VFS_MAX_PATH_LEN - 1] = '\0';
	} else {
		// Build absolute path from current directory
		if (!current_directory || !vfs_get_full_path(current_directory, abs_path, sizeof(abs_path))) {
			printf("edit: cannot determine current directory\n");
			return;
		}
		
		size_t abs_len = strlen(abs_path);
		if (abs_len > 0 && abs_path[abs_len - 1] != '/') {
			if (abs_len + 1 >= VFS_MAX_PATH_LEN) {
				printf("edit: path too long\n");
				return;
			}
			abs_path[abs_len++] = '/';
			abs_path[abs_len] = '\0';
		}
		
		if (abs_len + strlen(args) >= VFS_MAX_PATH_LEN) {
			printf("edit: path too long\n");
			return;
		}
		
		strcat(abs_path, args);
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

// VFS command implementations
static void command_ls(const char* args) {
	vfs_node_t *dir;
	
	
	if (args && strlen(args) > 0) {
		dir = vfs_resolve_relative_path(args, current_directory);
		if (!dir) {
			printf("ls: cannot access '%s': No such file or directory\n", args);
			return;
		}
	} else {
		dir = current_directory;
		if (!dir) {
			printf("ls: current directory not set\n");
			return;
		}
	}
	
	if (dir->type != VFS_DIRECTORY) {
		printf("ls: Not a directory\n");
		return;
	}
	
	vfs_node_t *children[VFS_MAX_CHILDREN];
	int count = vfs_list_dir(dir, children, VFS_MAX_CHILDREN);
	
	
	if (count <= 0) {
		return;
	}
	
	for (int i = 0; i < count; i++) {
		if (!children[i]) {
			continue;
		}
		if (children[i]->type == VFS_DIRECTORY) {
			printf("[DIR]  %s\n", children[i]->name);
		} else {
			printf("[FILE] %s (%u bytes)\n", children[i]->name, children[i]->size);
		}
	}
}

static void command_cat(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("cat: missing file operand\n");
		printf("Usage: cat <filename>\n");
		return;
	}
	
	uint8_t buffer[4096];
	int bytes_read = vfs_read_path_relative(args, current_directory, buffer, sizeof(buffer) - 1, 0);
	
	if (bytes_read < 0) {
		printf("cat: %s: No such file or directory\n", args);
		return;
	}
	
	buffer[bytes_read] = '\0';
	printf("%s\n", (char*)buffer);
}

static void command_rm(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("rm: missing operand\n");
		printf("Usage: rm <filename>\n");
		return;
	}
	
	// Parse path to get parent and filename
	char path_copy[VFS_MAX_PATH_LEN];
	strncpy(path_copy, args, VFS_MAX_PATH_LEN - 1);
	path_copy[VFS_MAX_PATH_LEN - 1] = '\0';
	
	char *last_slash = strrchr(path_copy, '/');
	if (!last_slash) {
		printf("rm: invalid path\n");
		return;
	}
	
	*last_slash = '\0';
	const char *filename = last_slash + 1;
	const char *dir_path = path_copy[0] == '\0' ? "/" : path_copy;
	
	vfs_node_t *parent = vfs_resolve_path(dir_path);
	if (!parent) {
		printf("rm: cannot remove '%s': No such file or directory\n", args);
		return;
	}
	
	if (vfs_delete(parent, filename) == 0) {
		printf("Removed '%s'\n", args);
	} else {
		printf("rm: cannot remove '%s': No such file or directory\n", args);
	}
}

static void command_touch(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("touch: missing file operand\n");
		printf("Usage: touch <filename>\n");
		return;
	}
	
	// Check if file already exists
	vfs_node_t *existing = vfs_resolve_relative_path(args, current_directory);
	if (existing) {
		printf("File '%s' already exists\n", args);
		return;
	}
	
	// Create empty file
	const uint8_t empty[] = "";
	if (vfs_write_path_relative(args, current_directory, empty, 0) >= 0) {
		printf("Created file '%s'\n", args);
	} else {
		printf("touch: cannot create '%s': No such file or directory\n", args);
	}
}

static void command_mkdir(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("mkdir: missing operand\n");
		printf("Usage: mkdir <directory>\n");
		return;
	}
	
	// Parse path to get parent and directory name
	char path_copy[VFS_MAX_PATH_LEN];
	strncpy(path_copy, args, VFS_MAX_PATH_LEN - 1);
	path_copy[VFS_MAX_PATH_LEN - 1] = '\0';
	
	char *last_slash = strrchr(path_copy, '/');
	if (!last_slash) {
		printf("mkdir: invalid path\n");
		return;
	}
	
	*last_slash = '\0';
	const char *dirname = last_slash + 1;
	const char *parent_path = path_copy[0] == '\0' ? "/" : path_copy;
	
	vfs_node_t *parent = vfs_resolve_path(parent_path);
	if (!parent) {
		printf("mkdir: cannot create directory '%s': No such file or directory\n", args);
		return;
	}
	
	if (vfs_mkdir(parent, dirname)) {
		printf("Created directory '%s'\n", args);
	} else {
		printf("mkdir: cannot create directory '%s': File exists\n", args);
	}
}

static void command_rmdir(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("rmdir: missing operand\n");
		printf("Usage: rmdir <directory>\n");
		return;
	}
	
	// Parse path to get parent and directory name
	char path_copy[VFS_MAX_PATH_LEN];
	strncpy(path_copy, args, VFS_MAX_PATH_LEN - 1);
	path_copy[VFS_MAX_PATH_LEN - 1] = '\0';
	
	char *last_slash = strrchr(path_copy, '/');
	if (!last_slash) {
		printf("rmdir: invalid path\n");
		return;
	}
	
	*last_slash = '\0';
	const char *dirname = last_slash + 1;
	const char *parent_path = path_copy[0] == '\0' ? "/" : path_copy;
	
	vfs_node_t *parent = vfs_resolve_path(parent_path);
	if (!parent) {
		printf("rmdir: failed to remove '%s': No such file or directory\n", args);
		return;
	}
	
	vfs_node_t *dir = vfs_find_child(parent, dirname);
	if (!dir) {
		printf("rmdir: failed to remove '%s': No such file or directory\n", args);
		return;
	}
	
	if (dir->type != VFS_DIRECTORY) {
		printf("rmdir: failed to remove '%s': Not a directory\n", args);
		return;
	}
	
	if (dir->child_count > 0) {
		printf("rmdir: failed to remove '%s': Directory not empty\n", args);
		return;
	}
	
	if (vfs_delete(parent, dirname) == 0) {
		printf("Removed directory '%s'\n", args);
	} else {
		printf("rmdir: failed to remove '%s'\n", args);
	}
}

static void command_cd(const char* args) {
	vfs_node_t *target;
	
	if (!args || strlen(args) == 0 || strcmp_local(args, "/") == 0) {
		// Go to root
		target = vfs_get_root();
	} else if (strcmp_local(args, "..") == 0) {
		// Go to parent directory
		if (current_directory && current_directory->parent) {
			target = current_directory->parent;
		} else {
			target = current_directory; // Already at root
		}
	} else if (strcmp_local(args, ".") == 0) {
		// Stay in current directory
		return;
	} else {
		// Resolve path (relative or absolute)
		target = vfs_resolve_relative_path(args, current_directory);
	}
	
	if (!target) {
		printf("cd: %s: No such file or directory\n", args);
		return;
	}
	
	if (target->type != VFS_DIRECTORY) {
		printf("cd: %s: Not a directory\n", args);
		return;
	}
	
	current_directory = target;
}

static void command_pwd(void) {
	char path_buffer[VFS_MAX_PATH_LEN];
	if (vfs_get_full_path(current_directory, path_buffer, VFS_MAX_PATH_LEN)) {
		printf("%s\n", path_buffer);
	} else {
		printf("/\n");
	}
}

// Graphics mode commands
static void command_gfx(void) {
	printf("Starting graphics demonstration...\n");
	graphics_demo();
}

static void command_gfxanim(void) {
	printf("Starting graphics animation...\n");
	graphics_animation_demo();
}

static void command_gfxpaint(void) {
	printf("Starting paint demo...\n");
	graphics_paint_demo_with_dir(current_directory);
}

// Demo task functions
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

// Process list command
static void command_ps(void) {
	task_list();
}

// Kill process command
static void command_kill(const char* args) {
	if (!args || strlen(args) == 0) {
		printf("Usage: kill <pid>\n");
		return;
	}
	
	// Simple atoi implementation
	uint32_t pid = 0;
	while (*args >= '0' && *args <= '9') {
		pid = pid * 10 + (*args - '0');
		args++;
	}
	
	if (pid == 0) {
		printf("Invalid PID\n");
		return;
	}
	
	if (task_kill(pid)) {
		printf("Task %u killed\n", pid);
	} else {
		printf("Task %u not found\n", pid);
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
		printf("Unknown task: %s\n", args);
		printf("Available: demo1, demo2, demo3\n");
		return;
	}
	
	if (!task) {
		printf("Failed to create task\n");
	}
}
