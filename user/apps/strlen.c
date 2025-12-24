#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

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

	char *text = skip_spaces(args);
	if (!*text) {
		write("Usage: strlen <text>\n", sizeof("Usage: strlen <text>\n") - 1);
		return 1;
	}

	size_t len = strlen(text);
	write("String length: ", sizeof("String length: ") - 1);
	print_uint((uint32_t)len);
	write(" characters\n", sizeof(" characters\n") - 1);
	return 0;
}
