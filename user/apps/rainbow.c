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
		write("Usage: rainbow <text>\n", sizeof("Usage: rainbow <text>\n") - 1);
		return 1;
	}

	uint32_t colors[] = {4, 12, 14, 10, 11, 9, 13};
	uint32_t color_index = 0;

	while (*text) {
		setcolor(colors[color_index % 7], 0);
		write(text, 1);
		if (*text != ' ') {
			color_index++;
		}
		text++;
	}
	setcolor(7, 0);
	write("\n", 1);
	return 0;
}
