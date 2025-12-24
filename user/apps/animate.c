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

	char *mode = skip_spaces(args);
	if (strcmp(mode, "spin") == 0) {
		char spinner[] = {'|', '/', '-', '\\'};
		write("\nSpinning: ", sizeof("\nSpinning: ") - 1);
		for (int i = 0; i < 20; i++) {
			write(&spinner[i % 4], 1);
			write("\b", 1);
			sleep_ms(50);
		}
		write("Done!\n\n", sizeof("Done!\n\n") - 1);
		return 0;
	}
	if (strcmp(mode, "progress") == 0) {
		write("\nProgress: [", sizeof("\nProgress: [") - 1);
		for (int i = 0; i <= 20; i++) {
			write("#", 1);
			sleep_ms(80);
		}
		write("] Complete!\n\n", sizeof("] Complete!\n\n") - 1);
		return 0;
	}
	if (strcmp(mode, "dots") == 0) {
		write("\nLoading", sizeof("\nLoading") - 1);
		for (int i = 0; i < 10; i++) {
			write(".", 1);
			sleep_ms(120);
		}
		write(" Done!\n\n", sizeof(" Done!\n\n") - 1);
		return 0;
	}

	write("Available animations: spin, progress, dots\n",
	      sizeof("Available animations: spin, progress, dots\n") - 1);
	write("Usage: animate <type>\n", sizeof("Usage: animate <type>\n") - 1);
	return 1;
}
