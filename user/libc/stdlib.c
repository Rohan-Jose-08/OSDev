#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
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

typedef long Align;

typedef union header {
	struct {
		union header *next;
		size_t size;
	} s;
	Align x;
} Header;

static Header base;
static Header *freep = NULL;

static Header *morecore(size_t units) {
	if (units < 1024) {
		units = 1024;
	}
	char *p = sbrk((intptr_t)(units * sizeof(Header)));
	if (p == (char *)-1) {
		return NULL;
	}
	Header *hp = (Header *)p;
	hp->s.size = units;
	free((void *)(hp + 1));
	return freep;
}

void free(void *ptr) {
	if (!ptr) {
		return;
	}

	Header *bp = (Header *)ptr - 1;
	if (!freep) {
		base.s.next = &base;
		base.s.size = 0;
		freep = &base;
	}

	Header *p = freep;
	for (; !(bp > p && bp < p->s.next); p = p->s.next) {
		if (p >= p->s.next && (bp > p || bp < p->s.next)) {
			break;
		}
	}

	if (bp + bp->s.size == p->s.next) {
		bp->s.size += p->s.next->s.size;
		bp->s.next = p->s.next->s.next;
	} else {
		bp->s.next = p->s.next;
	}

	if (p + p->s.size == bp) {
		p->s.size += bp->s.size;
		p->s.next = bp->s.next;
	} else {
		p->s.next = bp;
	}

	freep = p;
}

void *malloc(size_t size) {
	if (size == 0) {
		return NULL;
	}

	size_t units = (size + sizeof(Header) - 1) / sizeof(Header) + 1;
	if (!freep) {
		base.s.next = &base;
		base.s.size = 0;
		freep = &base;
	}

	Header *prev = freep;
	for (Header *p = prev->s.next; ; prev = p, p = p->s.next) {
		if (p->s.size >= units) {
			if (p->s.size == units) {
				prev->s.next = p->s.next;
			} else {
				p->s.size -= units;
				p += p->s.size;
				p->s.size = units;
			}
			freep = prev;
			return (void *)(p + 1);
		}
		if (p == freep) {
			Header *more = morecore(units);
			if (!more) {
				return NULL;
			}
		}
	}
}

void *calloc(size_t count, size_t size) {
	if (count == 0 || size == 0) {
		return NULL;
	}
	if (count > (size_t)-1 / size) {
		return NULL;
	}
	size_t total = count * size;
	void *ptr = malloc(total);
	if (ptr) {
		memset(ptr, 0, total);
	}
	return ptr;
}

void *realloc(void *ptr, size_t size) {
	if (!ptr) {
		return malloc(size);
	}
	if (size == 0) {
		free(ptr);
		return NULL;
	}

	Header *bp = (Header *)ptr - 1;
	size_t old_bytes = (bp->s.size - 1) * sizeof(Header);
	if (size <= old_bytes) {
		return ptr;
	}

	void *new_ptr = malloc(size);
	if (!new_ptr) {
		return NULL;
	}
	size_t to_copy = old_bytes < size ? old_bytes : size;
	memcpy(new_ptr, ptr, to_copy);
	free(ptr);
	return new_ptr;
}
