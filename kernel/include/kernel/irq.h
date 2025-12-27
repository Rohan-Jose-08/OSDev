#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

#include <stdint.h>

typedef void (*irq_handler_t)(uint8_t irq);

void irq_register(uint8_t irq, irq_handler_t handler);
void irq_unregister(uint8_t irq);
void irq_dispatch(uint8_t irq);

#endif
