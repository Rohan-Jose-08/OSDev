#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *freq_str = next_token(&cursor);
	char *dur_str = next_token(&cursor);
	char *extra = next_token(&cursor);
	if (extra) {
		write("Usage: beep [frequency_hz] [duration_ms]\n",
		      sizeof("Usage: beep [frequency_hz] [duration_ms]\n") - 1);
		return 1;
	}

	uint32_t freq = 0;
	uint32_t dur = 0;
	if (freq_str) {
		int value = atoi(freq_str);
		if (value < 0) {
			write("Usage: beep [frequency_hz] [duration_ms]\n",
			      sizeof("Usage: beep [frequency_hz] [duration_ms]\n") - 1);
			return 1;
		}
		freq = (uint32_t)value;
	}
	if (dur_str) {
		int value = atoi(dur_str);
		if (value < 0) {
			write("Usage: beep [frequency_hz] [duration_ms]\n",
			      sizeof("Usage: beep [frequency_hz] [duration_ms]\n") - 1);
			return 1;
		}
		dur = (uint32_t)value;
	}

	if (beep(freq, dur) < 0) {
		return 1;
	}
	write("*BEEP*\n", sizeof("*BEEP*\n") - 1);
	return 0;
}
