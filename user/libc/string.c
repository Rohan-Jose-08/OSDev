#include <string.h>
#include <stddef.h>

size_t strlen(const char *s) {
	size_t len = 0;
	if (!s) {
		return 0;
	}
	while (s[len] != '\0') {
		len++;
	}
	return len;
}

void *memcpy(void *dest, const void *src, size_t n) {
	char *d = (char *)dest;
	const char *s = (const char *)src;
	for (size_t i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

void *memset(void *dest, int value, size_t n) {
	unsigned char *d = (unsigned char *)dest;
	for (size_t i = 0; i < n; i++) {
		d[i] = (unsigned char)value;
	}
	return dest;
}

int strcmp(const char *a, const char *b) {
	while (*a && (*a == *b)) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

char *strncpy(char *dest, const char *src, size_t n) {
	size_t i = 0;

	if (!dest || n == 0) {
		return dest;
	}
	if (!src) {
		dest[0] = '\0';
		return dest;
	}

	for (; i < n && src[i]; i++) {
		dest[i] = src[i];
	}
	for (; i < n; i++) {
		dest[i] = '\0';
	}
	return dest;
}

char *strcpy(char *dest, const char *src) {
	char *d = dest;
	if (!dest) {
		return dest;
	}
	if (!src) {
		dest[0] = '\0';
		return dest;
	}
	while ((*d++ = *src++)) {
	}
	return dest;
}

char *strcat(char *dest, const char *src) {
	char *d = dest;
	if (!dest) {
		return dest;
	}
	if (!src) {
		return dest;
	}
	while (*d) {
		d++;
	}
	while ((*d++ = *src++)) {
	}
	return dest;
}

char *strchr(const char *s, int c) {
	if (!s) {
		return NULL;
	}
	while (*s) {
		if (*s == (char)c) {
			return (char *)s;
		}
		s++;
	}
	return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
	const char *last = NULL;
	if (!s) {
		return NULL;
	}
	while (*s) {
		if (*s == (char)c) {
			last = s;
		}
		s++;
	}
	if (c == 0) {
		return (char *)s;
	}
	return (char *)last;
}
