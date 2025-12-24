#include <stdlib.h>
#include <stdint.h>
#include "syscall.h"

void exit(int code) {
	syscall3(SYSCALL_EXIT, (uint32_t)code, 0, 0);
	for (;;) { }
}

int atoi(const char *s) {
	int value = 0;
	int sign = 1;
	if (!s) {
		return 0;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		value = value * 10 + (*s - '0');
		s++;
	}
	return value * sign;
}

int abs(int value) {
	return value < 0 ? -value : value;
}
