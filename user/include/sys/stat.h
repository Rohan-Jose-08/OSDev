#ifndef _USER_SYS_STAT_H
#define _USER_SYS_STAT_H

#include <stdint.h>

struct stat {
	uint32_t st_size;
	uint32_t st_type;
};

#define S_IFREG 1
#define S_IFDIR 2

int stat(const char *path, struct stat *out);

#endif
