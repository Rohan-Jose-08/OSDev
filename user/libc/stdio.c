#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

int putchar(int c) {
	char ch = (char)c;
	return write(&ch, 1);
}

int puts(const char *s) {
	if (!s) {
		return -1;
	}
	write(s, (uint32_t)strlen(s));
	return write("\n", 1);
}

static void buf_putc(char *buf, unsigned int size, unsigned int *pos, char c) {
	if (*pos + 1 < size) {
		buf[*pos] = c;
	}
	(*pos)++;
}

static void buf_puts(char *buf, unsigned int size, unsigned int *pos, const char *s) {
	if (!s) {
		s = "(null)";
	}
	while (*s) {
		buf_putc(buf, size, pos, *s++);
	}
}

static void buf_put_uint(char *buf, unsigned int size, unsigned int *pos, unsigned int value) {
	char tmp[16];
	int idx = 0;

	if (value == 0) {
		buf_putc(buf, size, pos, '0');
		return;
	}

	while (value > 0 && idx < (int)sizeof(tmp)) {
		tmp[idx++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (int i = idx - 1; i >= 0; i--) {
		buf_putc(buf, size, pos, tmp[i]);
	}
}

static void buf_put_int(char *buf, unsigned int size, unsigned int *pos, int value) {
	if (value < 0) {
		buf_putc(buf, size, pos, '-');
		buf_put_uint(buf, size, pos, (unsigned int)(-value));
	} else {
		buf_put_uint(buf, size, pos, (unsigned int)value);
	}
}

int snprintf(char *buf, unsigned int size, const char *fmt, ...) {
	if (!buf || size == 0 || !fmt) {
		return 0;
	}

	unsigned int pos = 0;
	va_list args;
	va_start(args, fmt);

	while (*fmt) {
		if (*fmt != '%') {
			buf_putc(buf, size, &pos, *fmt++);
			continue;
		}

		fmt++;
		if (*fmt == '\0') break;

		if (*fmt == '%') {
			buf_putc(buf, size, &pos, '%');
		} else if (*fmt == 's') {
			const char *s = va_arg(args, const char *);
			buf_puts(buf, size, &pos, s);
		} else if (*fmt == 'd') {
			int v = va_arg(args, int);
			buf_put_int(buf, size, &pos, v);
		} else if (*fmt == 'u') {
			unsigned int v = va_arg(args, unsigned int);
			buf_put_uint(buf, size, &pos, v);
		} else {
			buf_putc(buf, size, &pos, '%');
			buf_putc(buf, size, &pos, *fmt);
		}
		fmt++;
	}

	if (size > 0) {
		unsigned int term = (pos < size) ? pos : size - 1;
		buf[term] = '\0';
	}

	va_end(args);
	return (int)pos;
}
