#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

static void print_uint(uint32_t value) {
	char buf[16];
	int i = 0;

	if (value == 0) {
		write("0", 1);
		return;
	}

	while (value > 0 && i < (int)sizeof(buf) - 1) {
		buf[i++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (int j = i - 1; j >= 0; j--) {
		write(&buf[j], 1);
	}
}

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *mode = skip_spaces(args);
	if (strcmp(mode, "start") == 0) {
		if (timer_start() < 0) {
			write("Timer is already running! Use 'timer stop' first.\n",
			      sizeof("Timer is already running! Use 'timer stop' first.\n") - 1);
			return 1;
		}
		write("Timer started!\n", sizeof("Timer started!\n") - 1);
		return 0;
	}
	if (strcmp(mode, "stop") == 0) {
		int elapsed = timer_stop();
		if (elapsed < 0) {
			write("Timer is not running! Use 'timer start' first.\n",
			      sizeof("Timer is not running! Use 'timer start' first.\n") - 1);
			return 1;
		}
		write("Timer stopped! Elapsed ticks: ", sizeof("Timer stopped! Elapsed ticks: ") - 1);
		print_uint((uint32_t)elapsed);
		write("\n", 1);
		return 0;
	}

	write("Usage: timer <start|stop>\n", sizeof("Usage: timer <start|stop>\n") - 1);
	return 1;
}
