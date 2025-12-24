#include <stdint.h>
#include <unistd.h>

#include "rand_util.h"

int main(void) {
	rand_seed_from_ticks();
	setcolor(10, 0);

	write("\n--- MATRIX MODE ACTIVATED ---\n\n",
	      sizeof("\n--- MATRIX MODE ACTIVATED ---\n\n") - 1);

	for (int line = 0; line < 10; line++) {
		for (int col = 0; col < 40; col++) {
			char c = (char)((rand_next() % 94) + 33);
			write(&c, 1);
		}
		write("\n", 1);
		sleep_ms(120);
	}

	write("\n--- MATRIX MODE DEACTIVATED ---\n\n",
	      sizeof("\n--- MATRIX MODE DEACTIVATED ---\n\n") - 1);
	setcolor(7, 0);
	return 0;
}
