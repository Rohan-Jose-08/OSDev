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
	int count = history_count();
	if (count <= 0) {
		write("No commands in history.\n",
		      sizeof("No commands in history.\n") - 1);
		return 0;
	}

	for (int i = 0; i < count; i++) {
		char entry[128];
		int len = history_get((uint32_t)i, entry, sizeof(entry));
		if (len < 0) {
			continue;
		}

		print_uint((uint32_t)(i + 1));
		write(". ", 2);
		write(entry, (uint32_t)strlen(entry));
		write("\n", 1);
	}

	return 0;
}
