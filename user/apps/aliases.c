#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void print_uint(uint32_t value) {
	char buf[16];
	int i = 0;

	if (value == 0) {
		write("0", 1);
		return;
	}

	while (value > 0 && i < (int)sizeof(buf) - 1) {
		buf[i++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (int j = i - 1; j >= 0; j--) {
		write(&buf[j], 1);
	}
}

int main(void) {
	int count = alias_count();
	if (count <= 0) {
		write("No aliases defined.\n",
		      sizeof("No aliases defined.\n") - 1);
		return 0;
	}

	write("\n========== Command Aliases ==========\n\n",
	      sizeof("\n========== Command Aliases ==========\n\n") - 1);

	for (int i = 0; i < count; i++) {
		char name[32];
		char cmd[256];
		if (alias_get((uint32_t)i, name, cmd) < 0) {
			continue;
		}
		print_uint((uint32_t)(i + 1));
		write(". ", 2);
		write(name, (uint32_t)strlen(name));
		write(" = ", 3);
		write(cmd, (uint32_t)strlen(cmd));
		write("\n", 1);
	}
	write("\n", 1);
	return 0;
}
