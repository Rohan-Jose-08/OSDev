#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


void idt_init(void);
void idt_get_range(uintptr_t *base, size_t *size);

#endif
