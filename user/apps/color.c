#include <stdint.h>
#include <unistd.h>

#include "path_util.h"

static int parse_uint(char **cursor, uint32_t *out) {
	char *s = skip_spaces(*cursor);
	uint32_t value = 0;
	int has_digit = 0;

	while (*s >= '0' && *s <= '9') {
		has_digit = 1;
		value = value * 10 + (uint32_t)(*s - '0');
		s++;
	}

	if (!has_digit) {
		return -1;
	}

	*cursor = s;
	*out = value;
	return 0;
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
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	uint32_t fg = 0;
	uint32_t bg = 0;

	if (parse_uint(&cursor, &fg) != 0 || parse_uint(&cursor, &bg) != 0) {
		write("Usage: color <foreground> <background>\n",
		      sizeof("Usage: color <foreground> <background>\n") - 1);
		return 1;
	}

	if (fg > 15 || bg > 15) {
		write("Error: Colors must be between 0 and 15\n",
		      sizeof("Error: Colors must be between 0 and 15\n") - 1);
		return 1;
	}

	if (setcolor(fg, bg) < 0) {
		write("color: failed to set color\n",
		      sizeof("color: failed to set color\n") - 1);
		return 1;
	}

	write("Color set to foreground=", sizeof("Color set to foreground=") - 1);
	print_uint(fg);
	write(", background=", sizeof(", background=") - 1);
	print_uint(bg);
	write("\n", 1);
	return 0;
}
