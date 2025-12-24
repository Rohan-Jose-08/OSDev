#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *name = skip_spaces(args);
	if (*name == '\0') {
		write("Usage: unalias <name>\n", sizeof("Usage: unalias <name>\n") - 1);
		return 1;
	}

	if (alias_remove(name) < 0) {
		write("Alias not found: ", sizeof("Alias not found: ") - 1);
		write(name, (uint32_t)strlen(name));
		write("\n", 1);
		return 1;
	}

	write("Alias removed: ", sizeof("Alias removed: ") - 1);
	write(name, (uint32_t)strlen(name));
	write("\n", 1);
	return 0;
}
