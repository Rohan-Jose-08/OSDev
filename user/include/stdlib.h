#ifndef _USER_STDLIB_H
#define _USER_STDLIB_H

#include <stddef.h>

int atoi(const char *s);
void exit(int code);
int abs(int value);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

#endif
