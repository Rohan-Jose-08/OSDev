#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "rand_util.h"

static void write_line(const char *line) {
	write(line, (uint32_t)strlen(line));
}

int main(void) {
	rand_seed_from_ticks();
	uint32_t choice = rand_next() % 3;

	write_line("\n");
	if (choice == 0) {
		setcolor(11, 0);
		write_line("     .---.        \n");
		write_line("    /     \\       \n");
		write_line("    \\.@-@./       \n");
		write_line("    /`\\_/`\\       \n");
		write_line("   //  _  \\\\      \n");
		write_line("  | \\     )|_     \n");
		write_line(" /`\\_`>  <_/ \\    \n");
		write_line(" \\__/'---'\\__/    \n");
		write_line("   COMPUTER!      \n");
	} else if (choice == 1) {
		setcolor(12, 0);
		write_line("        /\\        \n");
		write_line("       /  \\       \n");
		write_line("      |    |      \n");
		setcolor(15, 0);
		write_line("      | OS |      \n");
		setcolor(12, 0);
		write_line("      |    |      \n");
		write_line("     /|    |\\     \n");
		write_line("    / |    | \\    \n");
		setcolor(14, 0);
		write_line("   /  '    '  \\   \n");
		write_line("  / .'      '. \\  \n");
		setcolor(12, 0);
		write_line("    ROCKET!       \n");
	} else {
		setcolor(10, 0);
		write_line("   _____          \n");
		write_line("  |     |         \n");
		write_line("  | O O |         \n");
		write_line("  |  >  |         \n");
		write_line("  |_____|         \n");
		write_line("  _|_|_|_         \n");
		write_line(" |       |        \n");
		write_line(" |       |        \n");
		write_line(" |_______|        \n");
		write_line("  |     |         \n");
		write_line("  |     |         \n");
		write_line(" _|     |_        \n");
		write_line("   ROBOT!         \n");
	}

	setcolor(7, 0);
	write_line("\n");
	return 0;
}
