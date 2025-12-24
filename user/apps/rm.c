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
		puts("Usage: rm <file>");
		return 1;
	}

	char path[128];
	if (resolve_path(arg, path, sizeof(path)) < 0) {
		puts("rm: invalid path");
		return 1;
	}

	if (rm(path) < 0) {
		puts("rm: cannot remove");
		return 1;
	}

	write("Removed '", sizeof("Removed '") - 1);
	write(arg, (uint32_t)strlen(arg));
	write("'\n", 2);
	return 0;
}
