#ifndef USER_APPS_IO_UTIL_H
#define USER_APPS_IO_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

static int read_line(char *buf, size_t max_len) {
	size_t pos = 0;

	if (!buf || max_len == 0) {
		return -1;
	}

	while (1) {
		int c = getchar();
		if (c < 0) {
			continue;
		}

		char ch = (char)c;
		if (ch == '\r' || ch == '\n') {
			write("\n", 1);
			break;
		}
		if (ch == '\b') {
			if (pos > 0) {
				pos--;
				write("\b \b", 3);
			}
			continue;
		}
		if ((unsigned char)ch < 32) {
			continue;
		}
		if (pos + 1 < max_len) {
			buf[pos++] = ch;
			write(&ch, 1);
		}
	}

	buf[pos] = '\0';
	return (int)pos;
}

#endif
