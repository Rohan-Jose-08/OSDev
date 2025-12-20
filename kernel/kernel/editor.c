#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/fs.h>

#define MAX_LINES 100
#define MAX_LINE_LENGTH 80
#define EDITOR_HEIGHT 23

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

static inline uint16_t vga_entry(unsigned char uc, unsigned char color) {
	return (uint16_t)uc | ((uint16_t)color << 8);
}

typedef enum {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_COMMAND
} EditorMode;

typedef struct {
	char lines[MAX_LINES][MAX_LINE_LENGTH];
	int line_count;
	int cursor_x;
	int cursor_y;
	int scroll_offset;
	EditorMode mode;
	char filename[64];
	bool modified;
	char command_buffer[64];
	int command_pos;
} Editor;

static Editor editor;

// Forward declarations for command handlers
static void cmd_enter_insert_mode(void);
static void cmd_append_mode(void);
static void cmd_open_below(void);
static void cmd_open_above(void);
static void cmd_delete_char(void);
static void cmd_delete_line(void);
static void cmd_enter_command_mode(void);
static void cmd_move_up(void);
static void cmd_move_down(void);
static void cmd_move_left(void);
static void cmd_move_right(void);
static void cmd_line_start(void);
static void cmd_line_end(void);
static void cmd_goto_first_line(void);
static void cmd_goto_last_line(void);

// Command table entry
typedef struct {
	unsigned char key;
	void (*handler)(void);
	bool needs_second_key;
} EditorCommand;

#define CMD_HASH_SIZE 64
static const EditorCommand* cmd_hash_table[CMD_HASH_SIZE] = {NULL};

// Normal mode command table
static const EditorCommand normal_commands[] = {
	{'i', cmd_enter_insert_mode, false},
	{'a', cmd_append_mode, false},
	{'o', cmd_open_below, false},
	{'O', cmd_open_above, false},
	{'x', cmd_delete_char, false},
	{'d', cmd_delete_line, true},
	{':', cmd_enter_command_mode, false},
	{0x80, cmd_move_up, false},
	{'k', cmd_move_up, false},
	{0x81, cmd_move_down, false},
	{'j', cmd_move_down, false},
	{'h', cmd_move_left, false},
	{'l', cmd_move_right, false},
	{'0', cmd_line_start, false},
	{'$', cmd_line_end, false},
	{'g', cmd_goto_first_line, true},
	{'G', cmd_goto_last_line, false},
	{0, NULL, false}
};

static inline int cmd_hash(unsigned char key) {
	return key % CMD_HASH_SIZE;
}

static void init_command_table(void) {
	for (int i = 0; normal_commands[i].handler != NULL; i++) {
		int hash = cmd_hash(normal_commands[i].key);
		while (cmd_hash_table[hash] != NULL) {
			hash = (hash + 1) % CMD_HASH_SIZE;
		}
		cmd_hash_table[hash] = &normal_commands[i];
	}
}

// Lookup command with linear probing
static inline const EditorCommand* cmd_lookup(unsigned char key) {
	int hash = cmd_hash(key);
	int start = hash;
	
	do {
		const EditorCommand* cmd = cmd_hash_table[hash];
		if (cmd == NULL) return NULL;
		if (cmd->key == key) return cmd;
		hash = (hash + 1) % CMD_HASH_SIZE;
	} while (hash != start);
	
	return NULL;
}

static void editor_redraw(void) {
	// Write directly to VGA buffer to avoid flicker
	volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
	uint8_t normal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	uint8_t cursor_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	uint8_t status_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	
	// Draw content area (23 lines)
	for (int i = 0; i < EDITOR_HEIGHT; i++) {
		int line_idx = i + editor.scroll_offset;
		int row_offset = i * 80;
		
		if (line_idx < editor.line_count) {
			// Draw the line
			const char* line = editor.lines[line_idx];
			int col = 0;
			
			while (line[col] && col < 80) {
				// Check if this is cursor position
				uint8_t char_color = normal_color;
				if (line_idx == editor.cursor_y && col == editor.cursor_x && editor.mode != MODE_COMMAND) {
					char_color = cursor_color;
				}
				vga[row_offset + col] = vga_entry(line[col], char_color);
				col++;
			}
			
			// Check if cursor is at end of line
			if (line_idx == editor.cursor_y && editor.cursor_x == col && col < 80 && editor.mode != MODE_COMMAND) {
				vga[row_offset + col] = vga_entry(' ', cursor_color);
				col++;
			}
			
			// Pad rest of line
			while (col < 80) {
				vga[row_offset + col] = vga_entry(' ', normal_color);
				col++;
			}
		} else {
			// Empty line with tilde
			vga[row_offset] = vga_entry('~', normal_color);
			for (int col = 1; col < 80; col++) {
				vga[row_offset + col] = vga_entry(' ', normal_color);
			}
		}
	}
	
	// Draw status bar (line 23)
	int status_row = EDITOR_HEIGHT * 80;
	const char* mode_str = (editor.mode == MODE_INSERT) ? "-- INSERT --" :
	                       (editor.mode == MODE_COMMAND) ? "-- COMMAND --" : "";
	
	int col = 0;
	while (mode_str[col]) {
		vga[status_row + col] = vga_entry(mode_str[col], status_color);
		col++;
	}
	while (col < 80) {
		vga[status_row + col] = vga_entry(' ', status_color);
		col++;
	}
	
	// Draw help/command line (line 24)
	int help_row = (EDITOR_HEIGHT + 1) * 80;
	col = 0;
	
	if (editor.mode == MODE_COMMAND) {
		vga[help_row] = vga_entry(':', normal_color);
		col = 1;
		int i = 0;
		while (editor.command_buffer[i] && col < 80) {
			vga[help_row + col] = vga_entry(editor.command_buffer[i], normal_color);
			col++;
			i++;
		}
	} else {
		// Show filename and modified status
		vga[help_row + col++] = vga_entry('"', normal_color);
		
		int i = 0;
		while (editor.filename[i] && col < 70) {
			vga[help_row + col] = vga_entry(editor.filename[i], normal_color);
			col++;
			i++;
		}
		
		vga[help_row + col++] = vga_entry('"', normal_color);
		
		if (editor.modified) {
			const char* mod_str = " [+]";
			i = 0;
			while (mod_str[i] && col < 80) {
				vga[help_row + col] = vga_entry(mod_str[i], normal_color);
				col++;
				i++;
			}
		}
	}
	
	// Pad rest of line
	while (col < 80) {
		vga[help_row + col] = vga_entry(' ', normal_color);
		col++;
	}
}

static inline void editor_insert_char(char c) {
	if (editor.cursor_y >= editor.line_count) {
		editor.line_count = editor.cursor_y + 1;
		editor.lines[editor.cursor_y][0] = '\0';
	}
	
	char* line = editor.lines[editor.cursor_y];
	int len = strlen(line);
	
	if (len < MAX_LINE_LENGTH - 1) {
		memmove(&line[editor.cursor_x + 1], &line[editor.cursor_x], len - editor.cursor_x + 1);
		line[editor.cursor_x] = c;
		editor.cursor_x++;
		editor.modified = true;
	}
}

static inline void editor_delete_char(void) {
	if (editor.cursor_y >= editor.line_count) return;
	
	char* line = editor.lines[editor.cursor_y];
	int len = strlen(line);
	
	if (editor.cursor_x > 0) {
		// Use memmove for efficient shift left
		memmove(&line[editor.cursor_x - 1], &line[editor.cursor_x], len - editor.cursor_x + 1);
		editor.cursor_x--;
		editor.modified = true;
	} else if (editor.cursor_y > 0) {
		char* prev_line = editor.lines[editor.cursor_y - 1];
		int prev_len = strlen(prev_line);
		
		if (prev_len + len < MAX_LINE_LENGTH) {
			// Use memcpy to append current line to previous
			memcpy(&prev_line[prev_len], line, len + 1);
			
			// Use memmove to shift lines up
			int lines_to_move = editor.line_count - editor.cursor_y - 1;
			if (lines_to_move > 0) {
				memmove(&editor.lines[editor.cursor_y], &editor.lines[editor.cursor_y + 1],
				        lines_to_move * MAX_LINE_LENGTH);
			}
			
			editor.line_count--;
			editor.cursor_y--;
			editor.cursor_x = prev_len;
			editor.modified = true;
		}
	}
}

static void editor_delete_line(void) {
	if (editor.line_count == 0) return;
	
	// Use memmove to shift lines up efficiently
	int lines_to_move = editor.line_count - editor.cursor_y - 1;
	if (lines_to_move > 0) {
		memmove(&editor.lines[editor.cursor_y], &editor.lines[editor.cursor_y + 1],
		        lines_to_move * MAX_LINE_LENGTH);
	}
	
	editor.line_count--;
	if (editor.cursor_y >= editor.line_count && editor.cursor_y > 0) {
		editor.cursor_y--;
	}
	
	editor.cursor_x = 0;
	editor.modified = true;
}

static void editor_new_line(void) {
	if (editor.line_count >= MAX_LINES) return;
	
	if (editor.cursor_y >= editor.line_count) {
		editor.line_count = editor.cursor_y + 1;
		editor.lines[editor.cursor_y][0] = '\0';
	}
	
	char* line = editor.lines[editor.cursor_y];
	int len = strlen(line);
	int rest_len = len - editor.cursor_x;
	
	// Shift lines down using memmove
	int lines_to_move = editor.line_count - editor.cursor_y - 1;
	if (lines_to_move > 0) {
		memmove(&editor.lines[editor.cursor_y + 2], &editor.lines[editor.cursor_y + 1],
		        lines_to_move * MAX_LINE_LENGTH);
	}
	
	// Copy remaining text to new line using memcpy
	if (rest_len > 0) {
		memcpy(editor.lines[editor.cursor_y + 1], &line[editor.cursor_x], rest_len + 1);
	} else {
		editor.lines[editor.cursor_y + 1][0] = '\0';
	}
	
	// Truncate current line
	line[editor.cursor_x] = '\0';
	
	editor.line_count++;
	editor.cursor_y++;
	editor.cursor_x = 0;
	editor.modified = true;
}

static inline void editor_move_cursor(int dx, int dy) {
	editor.cursor_y += dy;
	editor.cursor_x += dx;
	
	// Clamp cursor_y
	if (editor.cursor_y < 0) editor.cursor_y = 0;
	else if (editor.cursor_y >= MAX_LINES) editor.cursor_y = MAX_LINES - 1;
	
	// Clamp cursor_x
	if (editor.cursor_y < editor.line_count) {
		int len = strlen(editor.lines[editor.cursor_y]);
		if (editor.cursor_x < 0) editor.cursor_x = 0;
		else if (editor.cursor_x > len) editor.cursor_x = len;
	} else {
		editor.cursor_x = 0;
	}
	
	// Handle scrolling
	if (editor.cursor_y < editor.scroll_offset) {
		editor.scroll_offset = editor.cursor_y;
	} else if (editor.cursor_y >= editor.scroll_offset + EDITOR_HEIGHT) {
		editor.scroll_offset = editor.cursor_y - EDITOR_HEIGHT + 1;
	}
}

// Command handlers implementation
static void cmd_enter_insert_mode(void) {
	editor.mode = MODE_INSERT;
}

static void cmd_append_mode(void) {
	editor.cursor_x++;
	int len = strlen(editor.lines[editor.cursor_y]);
	if (editor.cursor_x > len) editor.cursor_x = len;
	editor.mode = MODE_INSERT;
}

static void cmd_open_below(void) {
	editor.cursor_x = strlen(editor.lines[editor.cursor_y]);
	editor_new_line();
	editor.mode = MODE_INSERT;
}

static void cmd_open_above(void) {
	if (editor.line_count >= MAX_LINES) return;
	
	// Use memmove to shift lines down
	int lines_to_move = editor.line_count - editor.cursor_y;
	if (lines_to_move > 0) {
		memmove(&editor.lines[editor.cursor_y + 1], &editor.lines[editor.cursor_y],
		        lines_to_move * MAX_LINE_LENGTH);
	}
	
	editor.lines[editor.cursor_y][0] = '\0';
	editor.line_count++;
	editor.cursor_x = 0;
	editor.mode = MODE_INSERT;
}

static void cmd_delete_char(void) {
	char* line = editor.lines[editor.cursor_y];
	int len = strlen(line);
	if (editor.cursor_x < len) {
		memmove(&line[editor.cursor_x], &line[editor.cursor_x + 1], len - editor.cursor_x);
		editor.modified = true;
	}
}

static void cmd_delete_line(void) {
	// Wait for second 'd'
	while (!keyboard_has_input()) __asm__ volatile ("hlt");
	unsigned char c2 = keyboard_getchar();
	if (c2 == 'd') {
		editor_delete_line();
	}
}

static void cmd_enter_command_mode(void) {
	editor.mode = MODE_COMMAND;
	editor.command_pos = 0;
	editor.command_buffer[0] = '\0';
}

static void cmd_move_up(void) {
	editor_move_cursor(0, -1);
}

static void cmd_move_down(void) {
	editor_move_cursor(0, 1);
}

static void cmd_move_left(void) {
	editor_move_cursor(-1, 0);
}

static void cmd_move_right(void) {
	editor_move_cursor(1, 0);
}

static void cmd_line_start(void) {
	editor.cursor_x = 0;
}

static void cmd_line_end(void) {
	if (editor.cursor_y < editor.line_count) {
		editor.cursor_x = strlen(editor.lines[editor.cursor_y]);
	}
}

static void cmd_goto_first_line(void) {
	while (!keyboard_has_input()) __asm__ volatile ("hlt");
	unsigned char c2 = keyboard_getchar();
	if (c2 == 'g') {
		editor.cursor_y = 0;
		editor.cursor_x = 0;
		editor.scroll_offset = 0;
	}
}

static void cmd_goto_last_line(void) {
	if (editor.line_count > 0) {
		editor.cursor_y = editor.line_count - 1;
		editor.cursor_x = 0;
		if (editor.cursor_y >= EDITOR_HEIGHT) {
			editor.scroll_offset = editor.cursor_y - EDITOR_HEIGHT + 1;
		}
	}
}

// Branch prediction hints
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static void editor_save_file(void) {
	// Build file content from lines
	static char file_buffer[MAX_LINES * MAX_LINE_LENGTH];
	size_t offset = 0;
	
	for (int i = 0; i < editor.line_count; i++) {
		size_t line_len = strlen(editor.lines[i]);
		if (offset + line_len + 1 < sizeof(file_buffer)) {
			memcpy(file_buffer + offset, editor.lines[i], line_len);
			offset += line_len;
			if (i < editor.line_count - 1) {
				file_buffer[offset++] = '\n';
			}
		}
	}
	
	// Ensure null termination for empty files
	if (offset == 0 && editor.line_count <= 1 && strlen(editor.lines[0]) == 0) {
		file_buffer[0] = '\0';
	}
	
	// Write to disk filesystem (creates file if it doesn't exist)
	int result = fs_write_file(editor.filename, (const uint8_t*)file_buffer, offset, 0);
	if (result >= 0) {
		editor.modified = false;
	}
}

static void editor_load_file(void) {
	static char file_buffer[MAX_LINES * MAX_LINE_LENGTH];
	
	int bytes_read = fs_read_file(editor.filename, (uint8_t*)file_buffer, sizeof(file_buffer) - 1, 0);
	if (bytes_read <= 0) {
		// File doesn't exist or is empty, start with empty content
		editor.lines[0][0] = '\0';
		editor.line_count = 1;
		editor.modified = false;
		return;
	}
	
	// Ensure null termination
	file_buffer[bytes_read] = '\0';
	
	// Parse file content into lines
	editor.line_count = 0;
	int line_idx = 0;
	int col = 0;
	
	for (int i = 0; i < bytes_read && line_idx < MAX_LINES; i++) {
		if (file_buffer[i] == '\n') {
			editor.lines[line_idx][col] = '\0';
			line_idx++;
			col = 0;
		} else if (file_buffer[i] != '\r' && col < MAX_LINE_LENGTH - 1) {
			editor.lines[line_idx][col++] = file_buffer[i];
		}
	}
	
	// Handle last line
	if (line_idx < MAX_LINES) {
		editor.lines[line_idx][col] = '\0';
		if (col > 0 || bytes_read == 0) {
			line_idx++;
		}
	}
	
	editor.line_count = (line_idx > 0) ? line_idx : 1;
	editor.modified = false;
}

static bool editor_execute_command(void) {
	char cmd = editor.command_buffer[0];
	bool should_quit = false;
	
	// Command dispatch table
	switch (cmd) {
		case 'w':
			editor_save_file();
			editor.mode = MODE_NORMAL;
			
			// Check for :wq
			if (editor.command_buffer[1] == 'q') {
				should_quit = true;
			}
			break;
		case 'q':
			// Quit - check for unsaved changes
			if (editor.modified && editor.command_buffer[1] != '!') {
				// Don't quit if there are unsaved changes and no force flag
				editor.mode = MODE_NORMAL;
				should_quit = false;
			} else {
				// Safe to quit: no unsaved changes or force quit
				should_quit = true;
			}
			break;
		default:
			editor.mode = MODE_NORMAL;
			break;
	}
	
	editor.command_pos = 0;
	editor.command_buffer[0] = '\0';
	return should_quit;
}

static bool editor_handle_input(unsigned char c) {
	// Fast path: most common mode is INSERT
	if (likely(editor.mode == MODE_INSERT)) {
		// Handle special keys first (less common)
		switch (c) {
			case 27:  // ESC
				editor.mode = MODE_NORMAL;
				return true;
			case '\n':
				editor_new_line();
				return true;
			case '\b':
				editor_delete_char();
				return true;
			case 0x80:  // Up arrow
				editor_move_cursor(0, -1);
				return true;
			case 0x81:  // Down arrow
				editor_move_cursor(0, 1);
				return true;
		}
		// Fast path: printable characters (most common in insert mode)
		if (likely(c >= 32 && c < 127)) {
			editor_insert_char(c);
		}
		return true;
	}
	
	if (editor.mode == MODE_COMMAND) {
		switch (c) {
			case '\n': {
				// Execute command - inline check for quit
				char cmd = editor.command_buffer[0];
				bool should_quit = (cmd == 'q') || (cmd == 'w' && editor.command_buffer[1] == 'q');
				editor_execute_command();
				return !should_quit;
			}
			case 27:  // ESC
				editor.mode = MODE_NORMAL;
				editor.command_pos = 0;
				editor.command_buffer[0] = '\0';
				return true;
			case '\b':
				if (editor.command_pos > 0) {
					editor.command_buffer[--editor.command_pos] = '\0';
				}
				return true;
			default:
				if (c >= 32 && c < 127 && editor.command_pos < 63) {
					editor.command_buffer[editor.command_pos++] = c;
					editor.command_buffer[editor.command_pos] = '\0';
				}
				return true;
		}
	}
	
	const EditorCommand* cmd = cmd_lookup(c);
	if (unlikely(cmd != NULL)) {
		cmd->handler();
	}
	
	return true;
}

void editor_run(const char* filename) {
	// Initialize editor
	editor.line_count = 1;
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.scroll_offset = 0;
	editor.mode = MODE_NORMAL;
	editor.modified = false;
	editor.command_pos = 0;
	editor.command_buffer[0] = '\0';
	
	// Copy filename
	int i = 0;
	while (filename[i] && i < 63) {
		editor.filename[i] = filename[i];
		i++;
	}
	editor.filename[i] = '\0';
	
	// Initialize with empty content using memset
	memset(editor.lines, 0, MAX_LINES * MAX_LINE_LENGTH);
	
	// Initialize command hash table
	init_command_table();
	
	// Try to load existing file
	editor_load_file();
	
	unsigned char old_color = terminal_getcolor();
	
	// Clear screen and initial draw
	terminal_initialize();
	editor_redraw();
	
	// Main editor loop
	bool running = true;
	
	while (running) {
		// Wait for input
		while (!keyboard_has_input()) {
			__asm__ volatile ("hlt");
		}
		
		unsigned char c = keyboard_getchar();
		running = editor_handle_input(c);
		
		// Redraw after input (no delay needed with direct VGA write)
		editor_redraw();
	}
	
	// Restore terminal
	terminal_setcolor(old_color);
	terminal_initialize();
	
	printf("\nEditor closed");
	if (editor.modified) {
		printf(" (unsaved changes)");
	}
	printf(".\n\n");
}
