#ifndef USER_APPS_RAND_UTIL_H
#define USER_APPS_RAND_UTIL_H

#include <stdint.h>
#include <unistd.h>

static uint32_t rand_state = 1;

static void rand_seed(uint32_t seed) {
	if (seed == 0) {
		seed = 1;
	}
	rand_state = seed;
}

static uint32_t rand_next(void) {
	rand_state = rand_state * 1103515245u + 12345u;
	return (rand_state / 65536u) % 32768u;
}

static void rand_seed_from_ticks(void) {
	rand_seed(get_ticks());
}

#endif
