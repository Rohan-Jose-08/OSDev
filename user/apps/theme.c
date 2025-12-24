#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "path_util.h"

int main(void) {
	char args[64];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *mode = skip_spaces(args);
	if (strcmp(mode, "dark") == 0) {
		setcolor(7, 0);
		write("Theme set to: Dark\n", sizeof("Theme set to: Dark\n") - 1);
		return 0;
	}
	if (strcmp(mode, "blue") == 0) {
		setcolor(11, 1);
		write("Theme set to: Blue\n", sizeof("Theme set to: Blue\n") - 1);
		return 0;
	}
	if (strcmp(mode, "green") == 0) {
		setcolor(10, 0);
		write("Theme set to: Green (Matrix)\n",
		      sizeof("Theme set to: Green (Matrix)\n") - 1);
		return 0;
	}
	if (strcmp(mode, "amber") == 0) {
		setcolor(14, 0);
		write("Theme set to: Amber (Retro)\n",
		      sizeof("Theme set to: Amber (Retro)\n") - 1);
		return 0;
	}
	if (strcmp(mode, "list") == 0) {
		write("\nAvailable themes:\n", sizeof("\nAvailable themes:\n") - 1);
		write("  dark  - Classic dark terminal\n",
		      sizeof("  dark  - Classic dark terminal\n") - 1);
		write("  blue  - Blue ocean theme\n",
		      sizeof("  blue  - Blue ocean theme\n") - 1);
		write("  green - Matrix/hacker theme\n",
		      sizeof("  green - Matrix/hacker theme\n") - 1);
		write("  amber - Retro amber monitor\n",
		      sizeof("  amber - Retro amber monitor\n") - 1);
		write("\nUsage: theme <name>\n",
		      sizeof("\nUsage: theme <name>\n") - 1);
		return 0;
	}

	write("Unknown theme. Use 'theme list' to see available themes.\n",
	      sizeof("Unknown theme. Use 'theme list' to see available themes.\n") - 1);
	return 1;
}
