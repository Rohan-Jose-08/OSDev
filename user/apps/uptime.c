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

	write_line("\n=== System Uptime ===\n");
	write_line("Timer ticks: ");
	print_uint(ticks);
	write_line("\nUptime (sec): ");
	print_uint(seconds);
	write_line("\nCommands run: ");
	print_uint(get_command_count());
	write_line("\n\n");
	return 0;
}
