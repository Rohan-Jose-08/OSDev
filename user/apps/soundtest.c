#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

static int tone(uint32_t frequency_hz, uint32_t duration_ms) {
	return beep(frequency_hz, duration_ms);
}

static int play_scale(void) {
	static const uint32_t notes[] = {262, 294, 330, 349, 392, 440, 494, 523};
	for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
		if (tone(notes[i], 140) < 0) {
			return -1;
		}
		sleep_ms(40);
	}
	return 0;
}

static int play_siren(void) {
	for (int i = 0; i < 6; i++) {
		if (tone(880, 120) < 0) {
			return -1;
		}
		sleep_ms(40);
		if (tone(440, 120) < 0) {
			return -1;
		}
		sleep_ms(40);
	}
	return 0;
}

static int morse_element(uint32_t frequency_hz, uint32_t duration_ms) {
	if (tone(frequency_hz, duration_ms) < 0) {
		return -1;
	}
	sleep_ms(100);
	return 0;
}

static int play_sos(void) {
	const uint32_t frequency = 660;

	for (int i = 0; i < 3; i++) {
		if (morse_element(frequency, 100) < 0) {
			return -1;
		}
	}
	sleep_ms(200);

	for (int i = 0; i < 3; i++) {
		if (morse_element(frequency, 300) < 0) {
			return -1;
		}
	}
	sleep_ms(200);

	for (int i = 0; i < 3; i++) {
		if (morse_element(frequency, 100) < 0) {
			return -1;
		}
	}
	return 0;
}

static void usage(void) {
	write("Usage: soundtest [scale|siren|sos]\n",
	      sizeof("Usage: soundtest [scale|siren|sos]\n") - 1);
}

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *mode = next_token(&cursor);
	char *extra = next_token(&cursor);
	if (extra) {
		usage();
		return 1;
	}

	if (!mode || strcmp(mode, "scale") == 0) {
		return (play_scale() == 0) ? 0 : 1;
	}
	if (strcmp(mode, "siren") == 0) {
		return (play_siren() == 0) ? 0 : 1;
	}
	if (strcmp(mode, "sos") == 0) {
		return (play_sos() == 0) ? 0 : 1;
	}

	usage();
	return 1;
}
