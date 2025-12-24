#ifndef USER_APPS_PATH_UTIL_H
#define USER_APPS_PATH_UTIL_H

#include <stddef.h>
#include <string.h>
#include <unistd.h>

static char *skip_spaces(char *s) {
	if (!s) {
		return s;
	}
	while (*s == ' ') {
		s++;
	}
	return s;
}

static char *next_token(char **cursor) {
	char *s;

	if (!cursor || !*cursor) {
		return NULL;
	}

	s = skip_spaces(*cursor);
	if (*s == '\0') {
		*cursor = s;
		return NULL;
	}

	char *start = s;
	while (*s && *s != ' ') {
		s++;
	}
	if (*s) {
		*s = '\0';
		s++;
	}
	*cursor = s;
	return start;
}

static int copy_string(char *dst, size_t dst_size, const char *src) {
	if (!dst || dst_size == 0 || !src) {
		return -1;
	}
	size_t len = strlen(src);
	if (len + 1 > dst_size) {
		return -1;
	}
	memcpy(dst, src, len + 1);
	return 0;
}

static int resolve_path(const char *arg, char *out, size_t out_size) {
	char cwd[128];
	size_t cwd_len;
	size_t arg_len;

	if (!out || out_size == 0) {
		return -1;
	}

	if (!arg || *arg == '\0') {
		return (getcwd(out, (uint32_t)out_size) < 0) ? -1 : 0;
	}

	if (arg[0] == '/') {
		return copy_string(out, out_size, arg);
	}

	if (getcwd(cwd, sizeof(cwd)) < 0) {
		return -1;
	}

	cwd_len = strlen(cwd);
	arg_len = strlen(arg);

	if (strcmp(cwd, "/") == 0) {
		if (arg_len + 2 > out_size) {
			return -1;
		}
		out[0] = '/';
		memcpy(out + 1, arg, arg_len + 1);
		return 0;
	}

	if (cwd_len + 1 + arg_len + 1 > out_size) {
		return -1;
	}

	memcpy(out, cwd, cwd_len);
	out[cwd_len] = '/';
	memcpy(out + cwd_len + 1, arg, arg_len + 1);
	return 0;
}

#endif
