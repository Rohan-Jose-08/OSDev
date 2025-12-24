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

	char *mode = skip_spaces(args);
	if (*mode == '\0') {
		write("Usage: draw <box|line|rainbow>\n",
		      sizeof("Usage: draw <box|line|rainbow>\n") - 1);
		return 1;
	}

	if (strcmp(mode, "box") == 0) {
		write("\n+--------------------------------------------------+\n",
		      sizeof("\n+--------------------------------------------------+\n") - 1);
		write("|                                                  |\n",
		      sizeof("|                                                  |\n") - 1);
		write("|            MyOS - Operating System               |\n",
		      sizeof("|            MyOS - Operating System               |\n") - 1);
		write("|                                                  |\n",
		      sizeof("|                                                  |\n") - 1);
		write("+--------------------------------------------------+\n\n",
		      sizeof("+--------------------------------------------------+\n\n") - 1);
	} else if (strcmp(mode, "line") == 0) {
		write("\n================================================\n\n",
		      sizeof("\n================================================\n\n") - 1);
	} else if (strcmp(mode, "rainbow") == 0) {
		write("\n========",
		      sizeof("\n========") - 1);
		for (int i = 0; i < 6; i++) {
			write("========", sizeof("========") - 1);
		}
		write("\n\n", 2);
	} else {
		write("Usage: draw <box|line|rainbow>\n",
		      sizeof("Usage: draw <box|line|rainbow>\n") - 1);
		return 1;
	}

	return 0;
}
