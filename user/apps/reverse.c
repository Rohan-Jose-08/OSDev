#include <stdint.h>
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

	char *text = skip_spaces(args);
	if (!*text) {
		write("Usage: reverse <text>\n", sizeof("Usage: reverse <text>\n") - 1);
		return 1;
	}

	size_t len = strlen(text);
	for (int i = (int)len - 1; i >= 0; i--) {
		write(&text[i], 1);
	}
	write("\n", 1);
	return 0;
}
