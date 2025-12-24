#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "rand_util.h"

static void write_line(const char *line) {
	write(line, (uint32_t)strlen(line));
}

int main(void) {
	const char *fortunes[] = {
		"A bug in the code is worth two in the documentation.",
		"The best way to predict the future is to implement it.",
		"Code never lies, comments sometimes do.",
		"Simplicity is the ultimate sophistication.",
		"First, solve the problem. Then, write the code.",
		"The only way to go fast is to go well.",
		"Programs must be written for people to read.",
		"Make it work, make it right, make it fast.",
		"The best code is no code at all.",
		"Any fool can write code that a computer can understand.",
		"Debugging is twice as hard as writing the code in the first place."
	};

	rand_seed_from_ticks();
	uint32_t idx = rand_next() % (uint32_t)(sizeof(fortunes) / sizeof(fortunes[0]));

	setcolor(13, 0);
	write_line("\n========================================\n");
	write_line("  Fortune Cookie\n");
	write_line("========================================\n");
	setcolor(11, 0);
	write_line("\n");
	write_line(fortunes[idx]);
	write_line("\n\n");
	setcolor(7, 0);
	return 0;
}
