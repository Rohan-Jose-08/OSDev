#ifndef _KERNEL_KPTI_H
#define _KERNEL_KPTI_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/trap_frame.h>

struct process;

void kpti_init(void);
void kpti_map_kernel_pages(uint32_t *page_dir, struct process *proc);
void kpti_prepare_return_trap(trap_frame_t *frame);
void kpti_prepare_return_isr(void *frame);

#endif
