#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total > 0) {
		uint32_t n = total;
		if (n >= sizeof(args)) {
			n = sizeof(args) - 1;
		}
		args[n] = '\0';
		puts("Args:");
		write(args, (uint32_t)strlen(args));
		puts("");
	}

	puts("Hello from user mode!");
	return 0;
}
