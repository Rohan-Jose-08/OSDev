#include <sys/stat.h>
#include <stdint.h>
#include "syscall.h"

int stat(const char *path, struct stat *out) {
	return syscall3(SYSCALL_STAT, (uint32_t)path, (uint32_t)out, 0);
}
