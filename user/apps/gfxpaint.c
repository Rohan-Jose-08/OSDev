#include <stdint.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *path_arg = skip_spaces(args);
	if (*path_arg == '\0') {
		if (gfx_paint(NULL) < 0) {
			return 1;
		}
		return 0;
	}

	char path[128];
	if (resolve_path(path_arg, path, sizeof(path)) < 0) {
		return 1;
	}

	if (gfx_paint(path) < 0) {
		return 1;
	}
	return 0;
}
