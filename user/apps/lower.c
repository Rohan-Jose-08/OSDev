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

	char *text = skip_spaces(args);
	if (!*text) {
		write("Usage: lower <text>\n", sizeof("Usage: lower <text>\n") - 1);
		return 1;
	}

	while (*text) {
		char c = *text++;
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c + 32);
		}
		write(&c, 1);
	}
	write("\n", 1);
	return 0;
}
