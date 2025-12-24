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
	uint32_t ticks = get_ticks();
	uint32_t seconds = ticks / 100;

	write_line("\n========== System Information ==========\n\n");
	write_line("CPU Architecture:  i386 (32-bit x86)\n");
	write_line("OS Version:        MyOS v1.0\n");
	write_line("Kernel Type:       Monolithic\n");
	write_line("Boot Protocol:     Multiboot\n\n");
	write_line("Hardware:\n");
	write_line("  Display:         VGA Text Mode (80x25)\n");
	write_line("  Colors:          16 colors (4-bit)\n");
	write_line("  Input:           PS/2 Keyboard\n");
	write_line("  Interrupts:      Enabled (IRQ 0/1)\n\n");
	write_line("Statistics:\n");
	write_line("  Commands run:    ");
	print_uint(get_command_count());
	write_line("\n  Timer ticks:     ");
	print_uint(ticks);
	write_line("\n  Uptime (sec):    ");
	print_uint(seconds);
	write_line("\n\n");
	return 0;
}
