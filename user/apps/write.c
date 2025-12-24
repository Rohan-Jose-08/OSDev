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
	char args[160];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *file_arg = next_token(&cursor);
	if (!file_arg) {
		write("Usage: write <file> <text>\n",
		      sizeof("Usage: write <file> <text>\n") - 1);
		return 1;
	}

	char *content = skip_spaces(cursor);
	if (!*content) {
		write("Usage: write <file> <text>\n",
		      sizeof("Usage: write <file> <text>\n") - 1);
		return 1;
	}

	char path[128];
	if (resolve_path(file_arg, path, sizeof(path)) < 0) {
		write("write: invalid path\n", sizeof("write: invalid path\n") - 1);
		return 1;
	}

	uint32_t len = (uint32_t)strlen(content);
	int written = writefile(path, content, len);
	if (written < 0) {
		write("write: write failed\n", sizeof("write: write failed\n") - 1);
		return 1;
	}

	write("Wrote ", sizeof("Wrote ") - 1);
	print_uint((uint32_t)written);
	write(" bytes to ", sizeof(" bytes to ") - 1);
	write(file_arg, (uint32_t)strlen(file_arg));
	write("\n", 1);
	return 0;
}
