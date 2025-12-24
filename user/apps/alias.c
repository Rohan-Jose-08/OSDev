#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[160];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = skip_spaces(args);
	if (*cursor == '\0') {
		write("Usage: alias name=command\n", sizeof("Usage: alias name=command\n") - 1);
		return 1;
	}

	char *eq = cursor;
	while (*eq && *eq != '=') {
		eq++;
	}
	if (*eq != '=') {
		write("Usage: alias name=command\n", sizeof("Usage: alias name=command\n") - 1);
		return 1;
	}

	*eq = '\0';
	char *name = cursor;
	char *cmd = eq + 1;
	cmd = skip_spaces(cmd);

	if (*name == '\0' || *cmd == '\0') {
		write("Usage: alias name=command\n", sizeof("Usage: alias name=command\n") - 1);
		return 1;
	}

	if (alias_set(name, cmd) < 0) {
		write("Alias creation failed\n", sizeof("Alias creation failed\n") - 1);
		return 1;
	}

	write("Alias created: ", sizeof("Alias created: ") - 1);
	write(name, (uint32_t)strlen(name));
	write(" = ", 3);
	write(cmd, (uint32_t)strlen(cmd));
	write("\n", 1);
	return 0;
}
