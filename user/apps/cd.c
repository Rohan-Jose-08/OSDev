#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

static int set_path(const char *path) {
	if (setcwd(path) < 0) {
		write("cd: directory not found\n",
		      sizeof("cd: directory not found\n") - 1);
		return 1;
	}
	return 0;
}

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
		return set_path("/");
	}

	if (strcmp(arg, ".") == 0) {
		return 0;
	}

	char target[128];

	if (strcmp(arg, "..") == 0) {
		if (getcwd(target, sizeof(target)) < 0) {
			write("cd: failed to read cwd\n",
			      sizeof("cd: failed to read cwd\n") - 1);
			return 1;
		}
		if (strcmp(target, "/") == 0) {
			return 0;
		}

		size_t len = strlen(target);
		while (len > 0 && target[len - 1] != '/') {
			len--;
		}
		if (len <= 1) {
			copy_string(target, sizeof(target), "/");
		} else {
			target[len - 1] = '\0';
		}
		return set_path(target);
	}

	if (resolve_path(arg, target, sizeof(target)) < 0) {
		write("cd: invalid path\n", sizeof("cd: invalid path\n") - 1);
		return 1;
	}

	return set_path(target);
}
