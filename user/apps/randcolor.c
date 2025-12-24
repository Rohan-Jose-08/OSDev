#include <stdint.h>
#include <unistd.h>

#include "rand_util.h"

int main(void) {
	rand_seed_from_ticks();

	uint32_t fg = (rand_next() % 15) + 1;
	uint32_t bg = rand_next() % 8;

	if (setcolor(fg, bg) < 0) {
		return 1;
	}

	write("Random colors applied! (fg=", sizeof("Random colors applied! (fg=") - 1);
	{
		char buf[16];
		int i = 0;
		uint32_t value = fg;
		if (value == 0) {
			write("0", 1);
		} else {
			while (value > 0 && i < (int)sizeof(buf) - 1) {
				buf[i++] = (char)('0' + (value % 10));
				value /= 10;
			}
			for (int j = i - 1; j >= 0; j--) {
				write(&buf[j], 1);
			}
		}
	}
	write(", bg=", sizeof(", bg=") - 1);
	{
		char buf[16];
		int i = 0;
		uint32_t value = bg;
		if (value == 0) {
			write("0", 1);
		} else {
			while (value > 0 && i < (int)sizeof(buf) - 1) {
				buf[i++] = (char)('0' + (value % 10));
				value /= 10;
			}
			for (int j = i - 1; j >= 0; j--) {
				write(&buf[j], 1);
			}
		}
	}
	write(")\n", 2);
	return 0;
}
