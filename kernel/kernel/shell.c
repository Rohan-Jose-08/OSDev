#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>

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

// Simple pseudo-random number generator
static unsigned int rand_seed = 12345;
static unsigned int simple_rand(void) {
	rand_seed = rand_seed * 1103515245 + 12345;
	return (rand_seed / 65536) % 32768;
}

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
	command_banner();
	
	while (true) {
		tick_count++;
		output_prompt();
		input_line(command, MAX_COMMAND_LENGTH);
		if (strlen(command) > 0) {
			command_count++;
			execute_command(command);
		}
	}
}

static void output_prompt(void) {
	printf("myos> ");
}

static void input_line(char* buffer, size_t max_length) {
	size_t pos = 0;
	while (true) {
		while (!keyboard_has_input()) __asm__ volatile ("hlt");
		char c = keyboard_getchar();
		if (c == '\n') {
			buffer[pos] = '\0';
			printf("\n");
			return;
		} else if (c == '\b') {
			if (pos > 0) {
				pos--;
				printf("\b \b");
			}
		} else if (c >= 32 && c < 127 && pos < max_length - 1) {
			buffer[pos++] = c;
		}
	}
}

static void execute_command(const char* command) {
	while (*command == ' ') command++;
	
	if (strcmp_local(command, "help") == 0) command_help();
	else if (strcmp_local(command, "clear") == 0) command_clear();
	else if (strcmp_local(command, "about") == 0) command_about();
	else if (strcmp_local(command, "colors") == 0) command_colors();
	else if (strcmp_local(command, "sysinfo") == 0) command_sysinfo();
	else if (strcmp_local(command, "banner") == 0) command_banner();
	else if (strcmp_local(command, "uptime") == 0) command_uptime();
	else if (strcmp_local(command, "guess") == 0) command_guess();
	else if (strcmp_local(command, "art") == 0) command_art();
	else if (starts_with(command, "echo ")) command_echo(command + 5);
	else if (starts_with(command, "color ")) command_color(command + 6);
	else if (starts_with(command, "calc ")) command_calc(command + 5);
	else if (starts_with(command, "mem ")) command_memory(command + 4);
	else if (starts_with(command, "reverse ")) command_reverse(command + 8);
	else if (strcmp_local(command, "") != 0) {
		printf("Unknown command: %s\n", command);
		printf("Type 'help' for available commands.\n");
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
	printf("  help          - Display this help message\n");
	printf("  clear         - Clear the screen\n");
	printf("  about         - Display information about this OS\n");
	printf("  banner        - Display welcome banner\n");
	printf("  sysinfo       - Display system information\n");
	printf("  uptime        - Show system uptime\n");
	
	printf("\n");
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printf("Display Commands:\n");
	terminal_setcolor(old_color);
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
	printf("  MyOS - Operating System\n");
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
	printf("  [+] Color rendering support\n");
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
