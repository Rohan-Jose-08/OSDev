#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "path_util.h"

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
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total == 0) {
		puts("Usage: statdemo <file>");
		return 1;
	}

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	char *cursor = args;
	char *arg = next_token(&cursor);
	if (!arg) {
		puts("Usage: statdemo <file>");
		return 1;
	}

	char path[128];
	if (resolve_path(arg, path, sizeof(path)) < 0) {
		puts("statdemo: invalid path");
		return 1;
	}

	struct stat info;
	if (stat(path, &info) < 0) {
		puts("statdemo: stat failed");
		return 1;
	}

	write("Type: ", 6);
	if (info.st_type == S_IFDIR) {
		puts("dir");
	} else {
		puts("file");
	}

	write("Size: ", 6);
	print_uint(info.st_size);
	puts(" bytes");

	write("Perm: ", 6);
	print_uint(info.st_perm);
	puts("");

	write("UID: ", 5);
	print_uint(info.st_uid);
	puts("");

	write("GID: ", 5);
	print_uint(info.st_gid);
	puts("");

	write("Atime: ", 7);
	print_uint(info.st_atime);
	puts("");

	write("Mtime: ", 7);
	print_uint(info.st_mtime);
	puts("");

	write("Ctime: ", 7);
	print_uint(info.st_ctime);
	puts("");

	return 0;
}
