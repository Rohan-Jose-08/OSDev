#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

static void usage(void) {
	write("Usage: mixer [master] [pcm]\n",
	      sizeof("Usage: mixer [master] [pcm]\n") - 1);
}

static uint8_t clamp_volume(int value) {
	if (value < 0) {
		return 0;
	}
	if (value > 100) {
		return 100;
	}
	return (uint8_t)value;
}

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *master_arg = next_token(&cursor);
	char *pcm_arg = next_token(&cursor);
	char *extra = next_token(&cursor);
	if (extra) {
		usage();
		return 1;
	}

	if (!master_arg) {
		uint8_t master = 0;
		uint8_t pcm = 0;
		if (audio_get_volume(&master, &pcm) < 0) {
			write("mixer: audio device unavailable\n",
			      sizeof("mixer: audio device unavailable\n") - 1);
			return 1;
		}
		char line[64];
		int n = snprintf(line, sizeof(line), "Master: %u  PCM: %u\n", master, pcm);
		if (n > 0) {
			write(line, (uint32_t)n);
		}
		return 0;
	}

	int master_val = atoi(master_arg);
	int pcm_val = master_val;
	if (pcm_arg) {
		pcm_val = atoi(pcm_arg);
	}

	uint8_t master = clamp_volume(master_val);
	uint8_t pcm = clamp_volume(pcm_val);
	if (audio_set_volume(master, pcm) < 0) {
		write("mixer: audio device unavailable\n",
		      sizeof("mixer: audio device unavailable\n") - 1);
		return 1;
	}

	char line[64];
	int n = snprintf(line, sizeof(line), "Master: %u  PCM: %u\n", master, pcm);
	if (n > 0) {
		write(line, (uint32_t)n);
	}
	return 0;
}
