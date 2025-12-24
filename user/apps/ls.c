#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
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
	char path[128];
	char *cursor = args;
	char *arg = NULL;

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	if (total > 0) {
		arg = next_token(&cursor);
	}

	if (resolve_path(arg, path, sizeof(path)) < 0) {
		puts("ls: invalid path");
		return 1;
	}

	struct dirent entries[64];
	int count = listdir(path, entries, 64);
	if (count < 0) {
		puts("ls: cannot access path");
		return 1;
	}

	if (count == 0) {
		puts("(empty)");
		return 0;
	}

	for (int i = 0; i < count; i++) {
		if (entries[i].d_type == 2) {
			write("[DIR]  ", sizeof("[DIR]  ") - 1);
			write(entries[i].d_name, (uint32_t)strlen(entries[i].d_name));
			write("\n", 1);
		} else {
			write("[FILE] ", sizeof("[FILE] ") - 1);
			write(entries[i].d_name, (uint32_t)strlen(entries[i].d_name));
			write(" (", 2);
			print_uint(entries[i].d_size);
			write(" bytes)\n", sizeof(" bytes)\n") - 1);
		}
	}

	return 0;
}
