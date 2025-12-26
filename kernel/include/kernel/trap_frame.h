#ifndef _KERNEL_TRAP_FRAME_H
#define _KERNEL_TRAP_FRAME_H

#include <stdint.h>

typedef struct {
	uint32_t gs, fs, es, ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t eip, cs, eflags, useresp, userss;
} __attribute__((packed)) trap_frame_t;

#endif
