#ifndef _KERNEL_GDT_H
#define _KERNEL_GDT_H

#include <stdint.h>
#include <stddef.h>

// GDT selectors
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
#define GDT_TSS         0x28

// Initialize GDT and load TSS.
void gdt_init(void);

// Update kernel stack used on ring transitions.
void tss_set_kernel_stack(uint32_t stack_top);

// Expose descriptor tables for KPTI mapping.
void gdt_get_range(uintptr_t *base, size_t *size);
void tss_get_range(uintptr_t *base, size_t *size);

#endif
