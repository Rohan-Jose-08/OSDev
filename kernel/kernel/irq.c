#include <kernel/irq.h>
#include <kernel/pic.h>

#define IRQ_MAX 16

static irq_handler_t irq_handlers[IRQ_MAX];

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq >= IRQ_MAX) {
        return;
    }
    irq_handlers[irq] = handler;
}

void irq_unregister(uint8_t irq) {
    if (irq >= IRQ_MAX) {
        return;
    }
    irq_handlers[irq] = 0;
}

void irq_dispatch(uint8_t irq) {
    if (irq < IRQ_MAX && irq_handlers[irq]) {
        irq_handlers[irq](irq);
    }
    PIC_sendEOI(irq);
}
