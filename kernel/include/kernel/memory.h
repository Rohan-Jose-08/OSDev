#ifndef _KERNEL_MEMORY_H
#define _KERNEL_MEMORY_H

#include <stdint.h>

#define KERNEL_VIRT_BASE 0xC0000000
#define KERNEL_PHYS_BASE 0x00100000

#define KERNEL_PHYS_TO_VIRT(addr) ((void *)((uint32_t)(addr) + KERNEL_VIRT_BASE))
#define KERNEL_VIRT_TO_PHYS(addr) ((uint32_t)(addr) - KERNEL_VIRT_BASE)

static inline void *phys_to_virt(uint32_t phys) {
	return (void *)(phys + KERNEL_VIRT_BASE);
}

static inline uint32_t virt_to_phys(const void *virt) {
	return (uint32_t)virt - KERNEL_VIRT_BASE;
}

#endif
