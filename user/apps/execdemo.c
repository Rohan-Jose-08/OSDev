#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total == 0) {
		puts("Usage: execdemo <path> [args]");
		return 1;
	}

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *path_arg = next_token(&cursor);
	if (!path_arg) {
		puts("execdemo: missing path");
		return 1;
	}

	char path[128];
	if (resolve_path(path_arg, path, sizeof(path)) < 0) {
		puts("execdemo: invalid path");
		return 1;
	}

	char *exec_args = skip_spaces(cursor);
	if (exec_args && *exec_args == '\0') {
		exec_args = 0;
	}

	uint32_t exec_len = exec_args ? (uint32_t)strlen(exec_args) : 0;
	if (exec(path, exec_args, exec_len) < 0) {
		puts("execdemo: exec failed");
		return 1;
	}

	return 0;
}
