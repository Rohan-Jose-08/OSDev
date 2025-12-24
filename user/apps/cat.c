#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "path_util.h"

static int parse_uint(const char *s, uint32_t *out) {
	uint32_t value = 0;
	if (!s || !out) {
		return -1;
	}
	if (*s < '0' || *s > '9') {
		return -1;
	}
	while (*s >= '0' && *s <= '9') {
		value = value * 10 + (uint32_t)(*s - '0');
		s++;
	}
	*out = value;
	return 0;
}

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total == 0) {
		puts("Usage: cat [-o offset] <file>");
		return 1;
	}

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = skip_spaces(args);

	uint32_t offset = 0;
	int have_offset = 0;

	if (cursor[0] == '-' && cursor[1] == 'o' && cursor[2] == ' ') {
		cursor += 3;
		cursor = skip_spaces(cursor);
		if (parse_uint(cursor, &offset) != 0) {
			puts("cat: invalid offset");
			return 1;
		}
		while (*cursor >= '0' && *cursor <= '9') cursor++;
		cursor = skip_spaces(cursor);
		have_offset = 1;
	}

	if (*cursor == '\0') {
		puts("Usage: cat [-o offset] <file>");
		return 1;
	}

	char *path = cursor;
	while (*cursor && *cursor != ' ') {
		cursor++;
	}
	if (*cursor) {
		*cursor = '\0';
	}

	char full_path[128];
	if (resolve_path(path, full_path, sizeof(full_path)) < 0) {
		puts("cat: invalid path");
		return 1;
	}

	struct stat info;
	if (stat(full_path, &info) < 0 || info.st_type != S_IFREG) {
		puts("cat: stat failed");
		return 1;
	}

	int fd = open(full_path);
	if (fd < 0) {
		puts("cat: open failed");
		return 1;
	}

	if (have_offset) {
		if (lseek(fd, (int)offset, SEEK_SET) < 0) {
			puts("cat: seek failed");
			close(fd);
			return 1;
		}
	}

	char buf[128];
	for (;;) {
		int n = read(fd, buf, sizeof(buf));
		if (n <= 0) {
			break;
		}
		write(buf, (uint32_t)n);
	}

	close(fd);
	return 0;
}
