#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void write_line(const char *line) {
	write(line, (uint32_t)strlen(line));
}

int main(void) {
	const char *color_names[] = {
		"0  - Black", "1  - Blue", "2  - Green", "3  - Cyan",
		"4  - Red", "5  - Magenta", "6  - Brown", "7  - Light Grey",
		"8  - Dark Grey", "9  - Light Blue", "10 - Light Green", "11 - Light Cyan",
		"12 - Light Red", "13 - Light Magenta", "14 - Yellow", "15 - White"
	};

	write_line("\nAvailable VGA Colors (0-15):\n\n");

	for (int i = 0; i < 16; i++) {
		setcolor((uint32_t)i, 0);
		write_line("  ");
		write_line(color_names[i]);
		setcolor(7, 0);
		write_line("  [Sample Text]\n");
	}

	write_line("\nUsage: color <foreground> <background>\n");
	write_line("Example: color 10 0  (green text on black)\n\n");
	return 0;
}
