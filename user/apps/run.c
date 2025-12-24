#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total == 0) {
		write("Usage: run <file> [args]\n", sizeof("Usage: run <file> [args]\n") - 1);
		return 1;
	}

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *file_arg = next_token(&cursor);
	if (!file_arg) {
		write("Usage: run <file> [args]\n", sizeof("Usage: run <file> [args]\n") - 1);
		return 1;
	}

	char *exec_args = skip_spaces(cursor);
	if (exec_args && *exec_args == '\0') {
		exec_args = 0;
	}

	char path[128];
	if (resolve_path(file_arg, path, sizeof(path)) < 0) {
		write("run: invalid path\n", sizeof("run: invalid path\n") - 1);
		return 1;
	}

	uint32_t exec_len = exec_args ? (uint32_t)strlen(exec_args) : 0;
	if (exec(path, exec_args, exec_len) < 0) {
		int has_slash = 0;
		for (const char *p = file_arg; *p; p++) {
			if (*p == '/') {
				has_slash = 1;
				break;
			}
		}
		if (!has_slash) {
			char bin_path[128];
			size_t name_len = strlen(file_arg);
			if (name_len + 6 < sizeof(bin_path)) {
				bin_path[0] = '/';
				bin_path[1] = 'b';
				bin_path[2] = 'i';
				bin_path[3] = 'n';
				bin_path[4] = '/';
				memcpy(bin_path + 5, file_arg, name_len);
				bin_path[5 + name_len] = '\0';
				if (exec(bin_path, exec_args, exec_len) == 0) {
					return 0;
				}
			}
		}
		write("run: exec failed\n", sizeof("run: exec failed\n") - 1);
		return 1;
	}

	return 0;
}
