#ifndef _USER_DIRENT_H
#define _USER_DIRENT_H

#include <stdint.h>

#define NAME_MAX 28

struct dirent {
	char d_name[NAME_MAX];
	uint32_t d_type;
	uint32_t d_size;
};

#endif
