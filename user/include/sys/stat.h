#ifndef _USER_SYS_STAT_H
#define _USER_SYS_STAT_H

#include <stdint.h>

struct stat {
	uint32_t st_size;
	uint32_t st_type;
	uint16_t st_perm;
	uint16_t st_uid;
	uint16_t st_gid;
	uint16_t st_reserved;
	uint32_t st_atime;
	uint32_t st_mtime;
	uint32_t st_ctime;
};

#define S_IFREG 1
#define S_IFDIR 2

int stat(const char *path, struct stat *out);

#endif
