#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void write_line(const char *line) {
	write(line, (uint32_t)strlen(line));
}

static void print_uint(uint32_t value) {
	char buf[16];
	int i = 0;

	if (value == 0) {
		write("0", 1);
		return;
	}

	while (value > 0 && i < (int)sizeof(buf) - 1) {
		buf[i++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (int j = i - 1; j >= 0; j--) {
		write(&buf[j], 1);
	}
}

int main(void) {
	write_line("\n================================\n");
	write_line("  RohanOS - Operating System\n");
	write_line("================================\n\n");
	write_line("Version:      1.0\n");
	write_line("Architecture: i386 (32-bit)\n");
	write_line("Boot Loader:  GRUB Multiboot\n\n");
	write_line("Features:\n");
	write_line("  [+] Custom kernel with interrupt handling\n");
	write_line("  [+] VGA text mode display (80x25, 16 colors)\n");
	write_line("  [+] PS/2 keyboard input support\n");
	write_line("  [+] Interactive shell with command processing\n");
	write_line("  [+] Basic filesystem and user-mode apps\n\n");

	write_line("Commands executed: ");
	print_uint(get_command_count());
	write_line("\n\n");
	return 0;
}
