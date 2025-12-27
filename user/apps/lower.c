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
		char buf[128];
		for (;;) {
			int n = read(0, buf, sizeof(buf));
			if (n <= 0) {
				break;
			}
			for (int i = 0; i < n; i++) {
				char c = buf[i];
				if (c >= 'A' && c <= 'Z') {
					buf[i] = (char)(c + 32);
				}
			}
			write(buf, (uint32_t)n);
		}
		return 0;
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
