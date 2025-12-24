#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *arg = next_token(&cursor);
	if (!arg) {
		puts("Usage: mkdir <dir>");
		return 1;
	}

	char path[128];
	if (resolve_path(arg, path, sizeof(path)) < 0) {
		puts("mkdir: invalid path");
		return 1;
	}

	if (mkdir(path) < 0) {
		puts("mkdir: failed to create directory");
		return 1;
	}

	write("Created directory: ", sizeof("Created directory: ") - 1);
	write(arg, (uint32_t)strlen(arg));
	write("\n", 1);
	return 0;
}
